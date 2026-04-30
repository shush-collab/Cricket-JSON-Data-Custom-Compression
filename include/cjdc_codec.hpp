#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cjdc {

struct EncodeStats {
    std::size_t original_bytes = 0;
    std::size_t encoded_bytes = 0;
    std::size_t ball_count = 0;
    std::size_t ball_stream_bytes = 0;
    std::size_t string_count = 0;
    std::size_t person_count = 0;
    std::size_t team_count = 0;
    std::size_t wicket_sidecars = 0;
    std::size_t extended_sidecars = 0;
    std::size_t exact_delivery_fallbacks = 0;
    bool raw_json_fallback = false;
};

struct DecodeStats {
    std::size_t encoded_bytes = 0;
    std::size_t output_bytes = 0;
    std::size_t ball_count = 0;
};

EncodeStats encode_file(const std::filesystem::path& input_json,
                        const std::filesystem::path& output_cjdc);

DecodeStats decode_file(const std::filesystem::path& input_cjdc,
                        const std::filesystem::path& output_json);

}  // namespace cjdc
