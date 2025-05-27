// decompress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Ball {
    bool wicket;
    int runs;
    std::string extras;
};

Ball decompress_byte(uint8_t b) {
    static const std::map<int,int> inv_run = {{0,0},{1,1},{2,2},{3,3},{4,4},{5,6}};
    static const std::map<int,std::string> inv_extra = {
        {0,"none"},{1,"wide"},{2,"noball"},
        {3,"bye"},{4,"legbye"},{5,"penalty"}
    };

    bool wicket   = (b >> 7) & 0x1;
    int  run_code = (b >> 4) & 0x7;
    int  ex_code  = (b >> 1) & 0x7;

    Ball ball;
    ball.wicket = wicket;
    ball.runs   = inv_run.count(run_code) ? inv_run.at(run_code) : -1;
    ball.extras = inv_extra.count(ex_code) ? inv_extra.at(ex_code) : "none";
    return ball;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_compressed.bin> <output_decompressed.json>\n";
        return 1;
    }

    // 1) read compressed file
    std::ifstream in(argv[1], std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Error opening input file\n";
        return 2;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();

    // 2) decompress into minimal JSON array
    json out = json::array();
    for (uint8_t b : buf) {
        Ball ball = decompress_byte(b);
        json d;
        d["runs"] = {{"batter", ball.runs}, {"extras", 0}};
        if (ball.extras != "none") {
            d["extras"] = {{"type", ball.extras}, {"runs", 1}};
        }
        if (ball.wicket) {
            d["wicket"] = json::object();  // placeholder
        }
        out.push_back(d);
    }

    // 3) write JSON
    std::ofstream o(argv[2]);
    if (!o.is_open()) {
        std::cerr << "Error opening output file\n";
        return 3;
    }
    o << out.dump(2) << std::endl;
    o.close();

    std::cout << "Decompressed to " << buf.size()
              << " deliveries in JSON.\n";
    return 0;
}