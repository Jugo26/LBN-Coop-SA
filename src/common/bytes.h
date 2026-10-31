// Little-endian byte (de)serialization helpers. Original code for LBN Coop SA.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lbn {

class ByteWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void f32(float v) { uint32_t b; std::memcpy(&b, &v, 4); u32(b); }
    // Short string: u8 length + bytes (truncated to maxLen, max 255).
    void str(const std::string& s, size_t maxLen = 255) {
        size_t n = s.size();
        if (maxLen > 255) maxLen = 255;
        if (n > maxLen) n = maxLen;
        u8(uint8_t(n));
        buf_.insert(buf_.end(), s.begin(), s.begin() + n);
    }
    const std::vector<uint8_t>& data() const { return buf_; }
    size_t size() const { return buf_.size(); }
private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    size_t remaining() const { return ok_ ? n_ - pos_ : 0; }
    uint8_t u8() { if (!need(1)) return 0; return p_[pos_++]; }
    uint16_t u16() { uint16_t a = u8(); uint16_t b = u8(); return uint16_t(a | (b << 8)); }
    uint32_t u32() { uint32_t a = u16(); uint32_t b = u16(); return a | (b << 16); }
    int16_t i16() { return int16_t(u16()); }
    int32_t i32() { return int32_t(u32()); }
    float f32() { uint32_t b = u32(); float v; std::memcpy(&v, &b, 4); return v; }
    std::string str() {
        uint8_t n = u8();
        if (!need(n)) return std::string();
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }
private:
    bool need(size_t k) {
        if (!ok_ || pos_ + k > n_) { ok_ = false; return false; }
        return true;
    }
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace lbn
