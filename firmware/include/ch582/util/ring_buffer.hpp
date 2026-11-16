#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ch582
{

// Single-producer, single-consumer lock-free ring. The extra slot separates
// the full and empty states.
template <std::size_t N>
class RingBuffer
{
  public:
    static constexpr std::size_t capacity = N;

    std::size_t                  size() const noexcept
    {
        const std::size_t head = m_write;
        const std::size_t tail = m_read;
        return head >= tail ? head - tail : (kSlots - tail) + head;
    }

    std::size_t free() const noexcept
    {
        return capacity - size();
    }

    bool empty() const noexcept
    {
        return m_read == m_write;
    }

    void clear() noexcept
    {
        m_read  = 0;
        m_write = 0;
    }

    std::size_t write(const void* src, std::size_t length) noexcept
    {
        if (src == nullptr || length == 0)
        {
            return 0;
        }
        if (length > free())
        {
            return 0;
        }

        std::size_t       head  = m_write;
        const std::size_t first = kSlots - head;
        if (length <= first)
        {
            std::memcpy(&m_pool[head], src, length);
            head += length;
            if (head == kSlots)
            {
                head = 0;
            }
        }
        else
        {
            std::memcpy(&m_pool[head], src, first);
            std::memcpy(&m_pool[0], static_cast<const std::uint8_t*>(src) + first, length - first);
            head = length - first;
        }

        m_write = head;
        return length;
    }

    std::size_t read(void* dst, std::size_t length) noexcept
    {
        if (dst == nullptr || length == 0)
        {
            return 0;
        }

        std::size_t       tail      = m_read;
        const std::size_t available = size();
        if (available == 0)
        {
            return 0;
        }
        if (length > available)
        {
            length = available;
        }

        const std::size_t first = kSlots - tail;
        if (length <= first)
        {
            std::memcpy(dst, &m_pool[tail], length);
            tail += length;
            if (tail == kSlots)
            {
                tail = 0;
            }
        }
        else
        {
            std::memcpy(dst, &m_pool[tail], first);
            std::memcpy(static_cast<std::uint8_t*>(dst) + first, &m_pool[0], length - first);
            tail = length - first;
        }

        m_read = tail;
        return length;
    }

  private:
    static constexpr std::size_t kSlots = N + 1;

    std::uint8_t                 m_pool[kSlots]{};
    volatile std::size_t         m_write = 0;
    volatile std::size_t         m_read  = 0;
};

} // namespace ch582
