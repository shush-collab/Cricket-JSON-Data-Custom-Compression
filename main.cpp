#include <cstdint>
#include <cassert>
#include <iostream>

enum class Extras : uint8_t {
    None   = 0b00,
    Wide   = 0b01,
    NoBall = 0b10,
    Bye    = 0b11
};

struct BallData {
    uint8_t runs;
    bool wicket;
    Extras extras;
};

uint8_t packBall(const BallData& b) {
    assert(b.runs <= 7);
    uint8_t byte = 0;
    byte |= (b.runs & 0b111);
    byte |= (uint8_t(b.wicket) << 3);
    byte |= (uint8_t(b.extras) << 4);
    return byte;
}

BallData unpackBall(uint8_t byte) {
    BallData b;
    b.runs    = byte & 0b111;
    b.wicket  = bool((byte >> 3) & 0b1);
    b.extras  = Extras((byte >> 4) & 0b11);
    return b;
}

int main() {
    BallData original { 4, true, Extras::NoBall };
    uint8_t compressed = packBall(original);

    BallData recovered = unpackBall(compressed);
    std::cout << "Original: runs=" << int(original.runs)
              << ", wicket=" << original.wicket
              << ", extras=" << int(original.extras) << "\n";
    std::cout << "Recovered: runs=" << int(recovered.runs)
              << ", wicket=" << recovered.wicket
              << ", extras=" << int(recovered.extras) << "\n";
    return 0;
}
