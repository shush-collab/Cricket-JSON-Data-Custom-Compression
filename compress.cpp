// compress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// bit‐layout:
// [7]    wicket flag
// [6–4]  run code (0→0,1→1,2→2,3→3,4→4,6→5, others→7)
// [3–1]  extras code (none=0, wide=1, noball=2, bye=3, legbye=4, penalty=5)
// [0]    reserved (0)

uint8_t compress_ball(const json& d) {
    static const std::map<int,int> run_codes = {{0,0},{1,1},{2,2},{3,3},{4,4},{6,5}};
    static const std::map<std::string,int> extra_codes = {
        {"wide",1},{"noball",2},{"bye",3},{"legbye",4},{"penalty",5}
    };

    int run = d["runs"]["batter"].get<int>();
    int run_code = run_codes.count(run) ? run_codes.at(run) : 7;

    std::string etype = d.value("extras", json::object())
                         .value("type", "");
    int extra_code = extra_codes.count(etype) ? extra_codes.at(etype) : 0;

    bool is_wicket = d.find("wicket") != d.end();

    return (uint8_t)(
        (is_wicket ? 1U<<7 : 0) |
        (run_code  << 4) |
        (extra_code<< 1)
    );
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_match.json> <output_compressed.bin>\n";
        return 1;
    }

    std::ifstream in(argv[1]);
    if (!in.is_open()) {
        std::cerr << "Error opening input file\n";
        return 2;
    }
    json j; in >> j;

    std::vector<const json*> deliveries;
    for (auto& inning : j["innings"]) {
        for (auto& over : inning["overs"]) {
            for (auto& d : over["deliveries"]) {
                deliveries.push_back(&d);
            }
        }
    }

    std::vector<uint8_t> buf;
    buf.reserve(deliveries.size());
    for (auto dptr : deliveries) {
        buf.push_back(compress_ball(*dptr));
    }

    std::ofstream out(argv[2], std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Error opening output file\n";
        return 3;
    }
    out.write(reinterpret_cast<char*>(buf.data()), buf.size());
    out.close();

    std::cout << "Compressed " << deliveries.size()
              << " balls into " << buf.size() << " bytes.\n";
    return 0;
}
