#include "project/Dataset/Sha256.h"

#include <limits>

namespace project::dataset {
namespace {

constexpr std::array<std::uint32_t, 64U> kRoundConstants{{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
}};

constexpr std::uint32_t rotateRight(std::uint32_t value,
                                    std::uint32_t bits) noexcept {
    return (value >> bits) | (value << (32U - bits));
}

std::uint32_t readBigEndian(const std::uint8_t* input) noexcept {
    return (static_cast<std::uint32_t>(input[0]) << 24U) |
           (static_cast<std::uint32_t>(input[1]) << 16U) |
           (static_cast<std::uint32_t>(input[2]) << 8U) |
           static_cast<std::uint32_t>(input[3]);
}

void writeBigEndian(std::uint32_t value, std::uint8_t* output) noexcept {
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}

}  // namespace

Sha256::Sha256() noexcept {
    reset();
}

void Sha256::reset() noexcept {
    state_ = {{0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
               0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U}};
    block_.fill(0U);
    totalBytes_ = 0U;
    blockLength_ = 0U;
    finished_ = false;
}

void Sha256::transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        words[index] = readBigEndian(block + index * 4U);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
        const std::uint32_t s0 =
            rotateRight(words[index - 15U], 7U) ^
            rotateRight(words[index - 15U], 18U) ^
            (words[index - 15U] >> 3U);
        const std::uint32_t s1 =
            rotateRight(words[index - 2U], 17U) ^
            rotateRight(words[index - 2U], 19U) ^
            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 +
                       words[index - 7U] + s1;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];

    for (std::size_t index = 0U; index < words.size(); ++index) {
        const std::uint32_t sum1 =
            rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temporary1 =
            h + sum1 + choice + kRoundConstants[index] + words[index];
        const std::uint32_t sum0 =
            rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
}

bool Sha256::update(const std::uint8_t* data, std::size_t length) noexcept {
    if (finished_ || (data == nullptr && length != 0U) ||
        length > std::numeric_limits<std::uint64_t>::max() - totalBytes_) {
        return false;
    }
    totalBytes_ += static_cast<std::uint64_t>(length);
    for (std::size_t index = 0U; index < length; ++index) {
        block_[blockLength_++] = data[index];
        if (blockLength_ == block_.size()) {
            transform(block_.data());
            blockLength_ = 0U;
        }
    }
    return true;
}

bool Sha256::finish(
    std::array<std::uint8_t, kDigestSize>& digest) noexcept {
    if (finished_ || totalBytes_ >
                         std::numeric_limits<std::uint64_t>::max() / 8U) {
        return false;
    }
    const std::uint64_t bitLength = totalBytes_ * 8U;
    block_[blockLength_++] = 0x80U;
    if (blockLength_ > 56U) {
        while (blockLength_ < block_.size()) {
            block_[blockLength_++] = 0U;
        }
        transform(block_.data());
        blockLength_ = 0U;
    }
    while (blockLength_ < 56U) {
        block_[blockLength_++] = 0U;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        block_[56U + index] =
            static_cast<std::uint8_t>(bitLength >> ((7U - index) * 8U));
    }
    transform(block_.data());
    for (std::size_t index = 0U; index < state_.size(); ++index) {
        writeBigEndian(state_[index], digest.data() + index * 4U);
    }
    finished_ = true;
    return true;
}

}  // namespace project::dataset
