#include "utils/ByteBuffer.h"
#include <cstring>

ByteBuffer::ByteBuffer(std::size_t capacity) {
    buffer_.reserve(capacity);
}

void ByteBuffer::clear() {
    buffer_.clear();
    read_pos_ = 0;
}

void ByteBuffer::reserve(std::size_t capacity) {
    buffer_.reserve(capacity);
}

const std::vector<uint8_t>& ByteBuffer::data() const {
    return buffer_;
}

std::vector<uint8_t>& ByteBuffer::data() {
    return buffer_;
}

void ByteBuffer::write(const void* src, std::size_t len) {
    if (!src || len == 0) {
        return;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(src);
    buffer_.insert(buffer_.end(), bytes, bytes + len);
}

bool ByteBuffer::read(void* dst, std::size_t len) {
    if (!dst || len == 0) {
        return false;
    }
    if (read_pos_ + len > buffer_.size()) {
        return false;
    }
    std::memcpy(dst, buffer_.data() + read_pos_, len);
    read_pos_ += len;
    return true;
}

std::size_t ByteBuffer::size() const {
    return buffer_.size();
}

std::size_t ByteBuffer::remaining() const {
    return buffer_.size() - read_pos_;
}
