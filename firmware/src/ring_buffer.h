#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <Arduino.h>

// Bounded circular ring buffer with zero dynamic memory allocation
template <typename T, size_t N>
class RingBuffer {
public:
    RingBuffer() : _head(0), _count(0) {}

    // Add new item to ring buffer (overwrites oldest item if full)
    void add(const T& item) {
        _buffer[_head] = item;
        _head = (_head + 1) % N;
        if (_count < N) {
            _count++;
        }
    }

    // Retrieve item by index (0 = oldest, count()-1 = newest)
    T get(size_t index) const {
        if (index >= _count) return T();
        size_t start = (_count < N) ? 0 : _head;
        size_t actualIdx = (start + index) % N;
        return _buffer[actualIdx];
    }

    // Get most recent item
    T getLatest() const {
        if (_count == 0) return T();
        size_t latestIdx = (_head == 0) ? (N - 1) : (_head - 1);
        return _buffer[latestIdx];
    }

    // Return current number of stored elements
    size_t count() const {
        return _count;
    }

    // Return maximum capacity
    size_t capacity() const {
        return N;
    }

    // Check if buffer is full
    bool isFull() const {
        return _count == N;
    }

    // Clear buffer contents
    void clear() {
        _head = 0;
        _count = 0;
    }

private:
    T _buffer[N];
    size_t _head;
    size_t _count;
};

#endif // RING_BUFFER_H
