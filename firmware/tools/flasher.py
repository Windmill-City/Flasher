#!/usr/bin/env python3
"""Host-side helper for the CH582F Flasher.

The Flasher exposes one CDC ACM port on its Type-C connector:

  * command mode: framed requests (AA 55 ...) for mode selection and ISP
  * bridge mode: raw bytes, entered by SET_MODE

A serial BREAK returns the Flasher to command mode from bridge mode.

Examples:
  python flasher.py --port COM7 info
  python flasher.py --port COM7 master --monitor
  python flasher.py --port COM7 slave --baud 115200 --monitor
  python flasher.py --port COM7 flash ..\\Dongle\\firmware\\build\\rf_uart_app.bin
  python flasher.py --port COM7 flash image.bin --verify
"""

from __future__ import annotations

import argparse
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise

VID = 0x1A86
PID = 0x5721

CMD_PING = 0x01
CMD_INFO = 0x02
CMD_SET_MODE = 0x03
CMD_POWER = 0x04
CMD_ISP_BEGIN = 0x10
CMD_ISP_WRITE = 0x11
CMD_ISP_VERIFY = 0x12
CMD_ISP_FINISH = 0x13
CMD_ISP_OPEN = 0x14
CMD_ISP_RAW = 0x15
CMD_ISP_CLOSE = 0x16

ISP_IDENTIFY = 0xA1
ISP_END = 0xA2
ISP_KEY = 0xA3
ISP_ERASE = 0xA4
ISP_PROGRAM = 0xA5
ISP_VERIFY = 0xA6
ISP_READ_CONFIG = 0xA7
ISP_WRITE_CONFIG = 0xA8

CFG_DEBUG_EN = 1 << 4

# CH570/CH572 keep the code-flash protection marker in the first config word.
# An erased marker (0xFF) makes the bootloader reject every PROGRAM with 0xFE.
CFG_CODE_UNLOCK = 0xA5
CHIP_CH570 = 0x70
CHIP_CH572 = 0x72

MODE_IDLE = 0
MODE_MASTER = 1
MODE_SLAVE = 2

STATUS = {
    0: "OK",
    1: "bad command",
    2: "bad argument",
    3: "no target",
    4: "USB error",
    5: "ISP error",
    6: "busy",
    7: "unsupported",
}


def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    matches = [
        p.device
        for p in list_ports.comports()
        if p.vid == VID and p.pid == PID
    ]
    if not matches:
        raise SystemExit("Flasher CDC port not found; pass --port COMx")
    return matches[0]


class Flasher:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=0, write_timeout=5)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass

    def break_to_command(self, wait: float = 0.25, attempts: int = 4) -> None:
        """Exit bridge mode and confirm the Flasher answers commands.

        Windows can deliver the USB BREAK after a following bulk write, so a
        single BREAK plus a fixed sleep is racy: the first command frame may be
        forwarded to the bridged target and lost. Retry with a PING until the
        Flasher replies.
        """
        for attempt in range(attempts):
            try:
                self.ser.send_break(0.05)
            except Exception:
                pass
            time.sleep(wait if attempt == 0 else max(wait, 0.2))
            self.ser.reset_input_buffer()
            try:
                self.request(CMD_PING, timeout=0.6)
                return
            except TimeoutError:
                continue
        raise TimeoutError("Flasher did not return to command mode")

    def request(self, command: int, payload: bytes = b"", timeout: float = 2.0,
                accepted_status: tuple[int, ...] = (0,)) -> bytes:
        body = bytes((command, len(payload))) + payload
        frame = b"\xAA\x55" + body + bytes((crc8(body),))
        self.ser.write(frame)
        self.ser.flush()

        deadline = time.monotonic() + timeout
        window = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(64)
            if not chunk:
                time.sleep(0.001)
                continue
            window += chunk
            start = window.find(b"\x55\xAA")
            if start < 0:
                if len(window) > 4:
                    window = window[-4:]
                continue
            data = window[start:]
            if len(data) < 5:
                continue
            length = data[3]
            total = 5 + length
            if len(data) < total:
                continue
            if crc8(data[2:4 + length]) != data[4 + length]:
                del window[: start + 1]
                continue
            status = data[4] if length else 0
            if status not in accepted_status:
                raise RuntimeError(STATUS.get(status, f"status {status:#x}"))
            return bytes(data[4:4 + length])
        raise TimeoutError("no response from Flasher")

    def set_mode(self, mode: int, baud: int = 115200) -> bytes:
        payload = bytes((mode,)) + baud.to_bytes(4, "little")
        return self.request(CMD_SET_MODE, payload, timeout=5.0)

    def isp_open(self) -> bytes:
        try:
            return self.request(CMD_ISP_OPEN, timeout=10.0)
        except TimeoutError:
            # Safe to retry: the Flasher aborts the old session and power
            # cycles the target into the bootloader again.
            self.break_to_command()
            return self.request(CMD_ISP_OPEN, timeout=15.0)

    def isp_close(self) -> bytes:
        return self.request(CMD_ISP_CLOSE, timeout=10.0)

    def flash(self, image: bytes, verify: bool, debug: bool = False) -> None:
        if len(image) > 240 * 1024:
            raise RuntimeError("image is larger than the CH572 240 KiB code flash")
        if len(image) % 1024:
            image += b"\xff" * (1024 - len(image) % 1024)

        self.prepare_target(debug)

        info = self.request(CMD_ISP_BEGIN, len(image).to_bytes(4, "little"), timeout=30)
        chip_id = info[1]
        device_type = info[2]
        uid = info[3:11].hex()
        sectors = int.from_bytes(info[11:15], "little")
        print(f"target chip={chip_id:#04x} type={device_type:#04x} uid={uid} erase={sectors} KiB")

        try:
            offset = 0
            chunks: list[tuple[int, bytes]] = []
            while offset < len(image):
                chunk = image[offset:offset + 56]
                self.request(CMD_ISP_WRITE, offset.to_bytes(4, "little") + chunk)
                chunks.append((offset, chunk))
                offset += len(chunk)
                print(f"\rwrite {offset}/{len(image)}", end="", flush=True)
            print()

            if verify:
                for index, (address, chunk) in enumerate(chunks):
                    self.request(CMD_ISP_VERIFY, address.to_bytes(4, "little") + chunk, timeout=10)
                    print(f"\rverify {index + 1}/{len(chunks)}", end="", flush=True)
                print()

            self.request(CMD_ISP_FINISH, timeout=10)
        except BaseException:
            try:
                self.request(CMD_ISP_FINISH, timeout=10)
            except Exception:
                pass
            raise
        print("done")

    def prepare_target(self, debug: bool = False) -> None:
        """Put the target in a state that accepts code-flash writes.

        CH570/CH572 store a code-flash protection marker in the first config
        word. An erased marker (0xFF) makes the bootloader answer every PROGRAM
        with "A5 ... FE", so write the 0xA5/0x5A unlock value and reboot.

        CH58x instead blocks writes while CFG_DEBUG_EN is set; a chip with
        debug enabled answers PROGRAM with "A5 ... FE 00" and every VERIFY with
        "FE 00". WCHISPStudio clears the bit before programming.
        """
        self.isp_open()
        session = IspSession(self, debug)
        session.identify()

        if session.chip_id in (CHIP_CH570, CHIP_CH572):
            if session.config[0] == CFG_CODE_UNLOCK:
                return

            session.send_key()
            fixed = bytearray(session.config)
            fixed[0] = CFG_CODE_UNLOCK
            fixed[1] = 0x5A
            print(f"CFG {session.config.hex()}: enabling code flash access")
            session.write_config(bytes(fixed))
            session.reboot()
            time.sleep(0.2)

            self.isp_open()
            session = IspSession(self, debug)
            session.identify()
            if session.config[0] != CFG_CODE_UNLOCK:
                raise RuntimeError(f"failed to unlock code flash (CFG={session.config.hex()})")
            print(f"CFG now {session.config.hex()}")
            return

        if not (session.user_cfg & CFG_DEBUG_EN):
            return

        fixed = bytearray(session.config)
        fixed[8:12] = (session.user_cfg & ~CFG_DEBUG_EN).to_bytes(4, "little")
        print(f"USER_CFG {session.user_cfg:#010x}: clearing CFG_DEBUG_EN")
        session.write_config(bytes(fixed))
        session.reboot()
        time.sleep(0.2)

        self.isp_open()
        session = IspSession(self, debug)
        session.identify()
        if session.user_cfg & CFG_DEBUG_EN:
            raise RuntimeError(f"failed to clear CFG_DEBUG_EN (USER_CFG={session.user_cfg:#010x})")
        print(f"USER_CFG now {session.user_cfg:#010x}")


class IspSession:
    """Raw WCH bootloader access through the Flasher passthrough (diagnostics)."""

    def __init__(self, flasher: Flasher, debug: bool = False):
        self.flasher = flasher
        self.debug = debug
        self.chip_id = 0
        self.device_type = 0
        self.uid = bytes(8)
        self.config = bytes(12)
        self.user_cfg = 0
        self.key = bytes(8)
        self.key_sum = 0

    def transfer(self, frame: bytes, timeout: float = 6.0) -> bytes:
        if len(frame) > 63:
            raise ValueError("ISP command longer than 63 bytes")
        response = self.flasher.request(
            CMD_ISP_RAW, frame, timeout=timeout, accepted_status=(0, 4)
        )
        if response[0] != 0:
            raise RuntimeError("target did not answer")
        raw = response[1:]
        if self.debug:
            print(f"\n  -> {frame.hex()}\n  <- {raw.hex()}")
        if not raw or raw[0] != frame[0]:
            raise RuntimeError(f"unexpected ISP response {raw.hex()}")
        return raw

    def identify(self) -> None:
        frame = bytes((ISP_IDENTIFY, 0x12, 0x00, 0x00, 0x00)) + b"MCU ISP & WCH.CN"
        raw = self.transfer(frame)
        if len(raw) < 6:
            raise RuntimeError(f"short identify response {raw.hex()}")
        self.chip_id = raw[4]
        self.device_type = raw[5]

        config = self.transfer(bytes((ISP_READ_CONFIG, 0x02, 0x00, 0x1F, 0x00)))
        if len(config) < 30:
            raise RuntimeError(f"short read-config response {config.hex()}")
        self.config = config[4 + 2:4 + 14]
        self.user_cfg = int.from_bytes(self.config[8:12], "little")
        self.uid = config[4 + 18:4 + 26]

        total = sum(self.uid) & 0xFF
        self.key = bytes([total] * 7 + [(total + self.chip_id) & 0xFF])
        self.key_sum = sum(self.key) & 0xFF

        if self.debug:
            print(f"  chip={self.chip_id:#04x} type={self.device_type:#04x} "
                  f"uid={self.uid.hex()} config={self.config.hex()} "
                  f"user_cfg={self.user_cfg:#010x}")

    def send_key(self) -> None:
        """Arm the XOR key the bootloader derives from the UID."""
        raw = self.transfer(bytes((ISP_KEY, 0x1E, 0x00)) + bytes(30))
        if len(raw) < 5 or raw[4] != self.key_sum:
            raise RuntimeError(f"ISP key rejected (expected checksum {self.key_sum:#04x})")

    def write_config(self, config: bytes) -> None:
        if len(config) != 12:
            raise ValueError("config write needs the 12 raw config bytes")
        frame = bytes((ISP_WRITE_CONFIG, 2 + len(config), 0x00, 0x07, 0x00)) + config
        self.transfer(frame, timeout=10.0)

    def reboot(self) -> None:
        try:
            self.transfer(bytes((ISP_END, 0x01, 0x00, 0x01)), timeout=2.0)
        except Exception:
            pass


def monitor(ser: serial.Serial) -> None:
    print("bridge active; Ctrl+C to exit (a BREAK is sent on exit)", file=sys.stderr)

    def reader() -> None:
        while True:
            data = ser.read(256)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            else:
                time.sleep(0.001)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        while True:
            data = sys.stdin.buffer.read(1)
            if not data:
                time.sleep(0.05)
                continue
            ser.write(data)
            ser.flush()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            ser.send_break(0.05)
        except Exception:
            pass
        time.sleep(0.1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="CDC COM port (auto-detected by VID/PID otherwise)")
    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("info")
    sub.add_parser("reset", help="send a BREAK to leave bridge mode")

    master = sub.add_parser("master")
    master.add_argument("--monitor", action="store_true")

    slave = sub.add_parser("slave")
    slave.add_argument("--baud", type=int, default=115200)
    slave.add_argument("--monitor", action="store_true")

    power = sub.add_parser("power")
    power.add_argument("state", choices=("off", "app", "isp"))

    flash = sub.add_parser("flash")
    flash.add_argument("image")
    flash.add_argument("--verify", action="store_true")
    flash.add_argument("--debug", action="store_true")

    args = parser.parse_args()
    port = find_port(args.port)
    flasher = Flasher(port)

    try:
        if args.action == "reset":
            flasher.break_to_command()
            print("bridge released")
            return

        flasher.break_to_command()

        if args.action == "info":
            info = flasher.request(CMD_INFO)
            print(f"protocol {info[1]}.{info[2]} mode={info[3]} target={info[4]} "
                  f"vid={int.from_bytes(info[5:7], 'little'):04x} "
                  f"pid={int.from_bytes(info[7:9], 'little'):04x}")

        elif args.action == "master":
            response = flasher.set_mode(MODE_MASTER)
            print(f"master mode, target={response[2]}, "
                  f"vid={int.from_bytes(response[3:5], 'little'):04x} "
                  f"pid={int.from_bytes(response[5:7], 'little'):04x}")
            if args.monitor:
                monitor(flasher.ser)
            else:
                print("port left in raw bridge mode; use a serial terminal or --monitor")

        elif args.action == "slave":
            response = flasher.set_mode(MODE_SLAVE, args.baud)
            print(f"slave UART mode at {args.baud} baud, target={response[2]}")
            if args.monitor:
                monitor(flasher.ser)
            else:
                print("port left in raw bridge mode; use a serial terminal or --monitor")

        elif args.action == "power":
            on, boot = {"off": (0, 0), "app": (1, 0), "isp": (1, 1)}[args.state]
            flasher.request(CMD_POWER, bytes((on, boot)))
            print(f"power {args.state}")

        elif args.action == "flash":
            with open(args.image, "rb") as handle:
                image = handle.read()
            flasher.flash(image, args.verify, args.debug)

    finally:
        flasher.close()


if __name__ == "__main__":
    main()
