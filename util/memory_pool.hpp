#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace htj2k::util {

class ReusableBuffer {
public:
  void reserve(const std::size_t bytes)
  {
    if (bytes > buffer_.size()) {
      buffer_.resize(bytes);
    }
  }

  [[nodiscard]] std::uint8_t* data() { return buffer_.data(); }
  [[nodiscard]] const std::uint8_t* data() const { return buffer_.data(); }
  [[nodiscard]] std::size_t size() const { return buffer_.size(); }
  void clear() { buffer_.clear(); }
  void resize(const std::size_t bytes) { buffer_.resize(bytes); }
  [[nodiscard]] std::vector<std::uint8_t>& vector() { return buffer_; }
  [[nodiscard]] const std::vector<std::uint8_t>& vector() const { return buffer_; }

private:
  std::vector<std::uint8_t> buffer_;
};

class MemoryPool {
public:
  [[nodiscard]] ReusableBuffer& scratch_bytes() { return scratch_bytes_; }
  [[nodiscard]] ReusableBuffer& scratch_bytes_2() { return scratch_bytes_2_; }
  [[nodiscard]] ReusableBuffer& scratch_bytes_3() { return scratch_bytes_3_; }

private:
  ReusableBuffer scratch_bytes_;
  ReusableBuffer scratch_bytes_2_;
  ReusableBuffer scratch_bytes_3_;
};

}  // namespace htj2k::util
