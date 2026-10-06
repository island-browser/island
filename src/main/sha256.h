#ifndef ISLAND_SHA256_H_
#define ISLAND_SHA256_H_

// CEF-free, standard-library-only SHA-256 (FIPS 180-4). The updater hashes
// downloaded release archives with it, streaming, before anything else
// touches them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace island {

class Sha256 final {
  public:
    static constexpr std::size_t kDigestSize = 32;
    using Digest = std::array<std::uint8_t, kDigestSize>;

    Sha256();

    void Update(const void* data, std::size_t size);
    void Update(std::string_view data) { Update(data.data(), data.size()); }
    // Finishes the hash and returns the digest. The object is reset afterwards
    // and can hash a new message.
    [[nodiscard]] Digest Finish();
    // Finish() as 64 lowercase hex characters.
    [[nodiscard]] std::string FinishHex();

    [[nodiscard]] static std::string Hex(const Digest& digest);
    // One-shot helpers.
    [[nodiscard]] static std::string HexOf(std::string_view data);
    // Hashes a file; std::nullopt when it cannot be opened or read.
    [[nodiscard]] static std::optional<std::string> HexOfFile(const std::filesystem::path& path);

  private:
    void Reset();
    void Transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

}  // namespace island

#endif  // ISLAND_SHA256_H_
