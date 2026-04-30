#include "cjdc_codec.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.json> <output.cjdc>\n";
        return 1;
    }

    try {
        const auto stats = cjdc::encode_file(argv[1], argv[2]);
        const double ratio = stats.original_bytes == 0
                                 ? 0.0
                                 : static_cast<double>(stats.encoded_bytes) /
                                       static_cast<double>(stats.original_bytes);

        std::cout << "Encoded " << stats.ball_count << " balls\n"
                  << "Original: " << stats.original_bytes << " bytes\n"
                  << "CJDC:     " << stats.encoded_bytes << " bytes ("
                  << std::fixed << std::setprecision(2) << ratio * 100.0
                  << "%)\n"
                  << "Ball stream: " << stats.ball_stream_bytes << " bytes\n"
                  << "Strings: " << stats.string_count
                  << ", persons: " << stats.person_count
                  << ", teams: " << stats.team_count << "\n"
                  << "Wicket sidecars: " << stats.wicket_sidecars
                  << ", extended sidecars: " << stats.extended_sidecars << "\n"
                  << "Exact delivery fallbacks: " << stats.exact_delivery_fallbacks
                  << ", raw JSON fallback: " << (stats.raw_json_fallback ? "yes" : "no")
                  << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Compression failed: " << ex.what() << "\n";
        return 2;
    }
}
