#include "cjdc_codec.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.cjdc> <output.json>\n";
        return 1;
    }

    try {
        const auto stats = cjdc::decode_file(argv[1], argv[2]);
        std::cout << "Decoded " << stats.ball_count << " balls\n"
                  << "CJDC:   " << stats.encoded_bytes << " bytes\n"
                  << "Output: " << stats.output_bytes << " bytes\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Decompression failed: " << ex.what() << "\n";
        return 2;
    }
}
