#include "sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace island {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t RotateRight(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

}  // namespace

Sha256::Sha256() { Reset(); }

void Sha256::Reset() {
    state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    buffer_size_ = 0;
    total_bytes_ = 0;
}

void Sha256::Transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8U) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3U);
        const std::uint32_t s1 =
            RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10U);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::Update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += size;
    while (size > 0) {
        if (buffer_size_ == 0 && size >= buffer_.size()) {
            Transform(bytes);
            bytes += buffer_.size();
            size -= buffer_.size();
            continue;
        }
        const std::size_t take = std::min(size, buffer_.size() - buffer_size_);
        std::memcpy(buffer_.data() + buffer_size_, bytes, take);
        buffer_size_ += take;
        bytes += take;
        size -= take;
        if (buffer_size_ == buffer_.size()) {
            Transform(buffer_.data());
            buffer_size_ = 0;
        }
    }
}

Sha256::Digest Sha256::Finish() {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80;
    if (buffer_size_ > 56) {
        std::memset(buffer_.data() + buffer_size_, 0, buffer_.size() - buffer_size_);
        Transform(buffer_.data());
        buffer_size_ = 0;
    }
    std::memset(buffer_.data() + buffer_size_, 0, 56 - buffer_size_);
    for (std::size_t i = 0; i < 8; ++i) {
        buffer_[56 + i] = static_cast<std::uint8_t>(bit_length >> (56U - 8U * i));
    }
    Transform(buffer_.data());

    Digest digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24U);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16U);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8U);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    Reset();
    return digest;
}

std::string Sha256::FinishHex() { return Hex(Finish()); }

std::string Sha256::Hex(const Digest& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        out += kHex[byte >> 4U];
        out += kHex[byte & 0x0FU];
    }
    return out;
}

std::string Sha256::HexOf(std::string_view data) {
    Sha256 hasher;
    hasher.Update(data);
    return hasher.FinishHex();
}

std::optional<std::string> Sha256::HexOfFile(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::FILE* file = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* file = std::fopen(path.c_str(), "rb");
#endif
    if (file == nullptr) {
        return std::nullopt;
    }
    Sha256 hasher;
    std::vector<char> chunk(1U << 16U);
    bool ok = true;
    for (;;) {
        const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file);
        if (read > 0) {
            hasher.Update(chunk.data(), read);
        }
        if (read < chunk.size()) {
            ok = std::ferror(file) == 0;
            break;
        }
    }
    std::fclose(file);
    if (!ok) {
        return std::nullopt;
    }
    return hasher.FinishHex();
}

}  // namespace island
