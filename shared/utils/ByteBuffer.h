#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class ByteBuffer {
public:
    ByteBuffer() = default;
    explicit ByteBuffer(std::size_t capacity);

    void clear();
    void reserve(std::size_t capacity);

    const std::vector<uint8_t>& data() const;
    std::vector<uint8_t>& data();

    void write(const void* src, std::size_t len);
    bool read(void* dst, std::size_t len);

    std::size_t size() const;
    std::size_t remaining() const;

private:
    std::vector<uint8_t> buffer_;
    std::size_t read_pos_ = 0;
};
