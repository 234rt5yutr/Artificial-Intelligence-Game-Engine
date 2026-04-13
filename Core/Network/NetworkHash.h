#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Core {
namespace Network {

    constexpr uint64_t NETWORK_FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t NETWORK_FNV_PRIME = 1099511628211ULL;

    inline uint64_t HashBytesFNV1a(const void* data, size_t size, uint64_t seed = NETWORK_FNV_OFFSET_BASIS) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t index = 0; index < size; ++index) {
            hash ^= static_cast<uint64_t>(bytes[index]);
            hash *= NETWORK_FNV_PRIME;
        }
        return hash;
    }

    template <typename TValue>
    inline uint64_t HashValueFNV1a(const TValue& value, uint64_t seed = NETWORK_FNV_OFFSET_BASIS) {
        return HashBytesFNV1a(&value, sizeof(TValue), seed);
    }

    inline uint64_t HashStringFNV1a(std::string_view text, bool normalizeLowercase = false) {
        uint64_t hash = NETWORK_FNV_OFFSET_BASIS;
        for (char character : text) {
            unsigned char safeCharacter = static_cast<unsigned char>(character);
            if (normalizeLowercase) {
                safeCharacter = static_cast<unsigned char>(std::tolower(safeCharacter));
            }
            hash ^= static_cast<uint64_t>(safeCharacter);
            hash *= NETWORK_FNV_PRIME;
        }
        return hash;
    }

    inline uint64_t HashCombineFNV1a(uint64_t left, uint64_t right) {
        if (left == 0 && right == 0) {
            return 0;
        }

        uint64_t hash = NETWORK_FNV_OFFSET_BASIS;
        hash = HashValueFNV1a(left, hash);
        hash = HashValueFNV1a(right, hash);
        return hash;
    }

    inline uint32_t HashStringTo32(std::string_view text, bool normalizeLowercase = true) {
        const uint64_t hash64 = HashStringFNV1a(text, normalizeLowercase);
        return static_cast<uint32_t>((hash64 >> 32U) ^ hash64);
    }

} // namespace Network
} // namespace Core

