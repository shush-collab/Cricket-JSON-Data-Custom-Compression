#include <cstdint>
#include <cassert>
#include <iostream>

// Enums for clarity
enum class Extras : uint8_t {
    None   = 0b00,
    Wide   = 0b01,
    NoBall = 0b10,
    Bye    = 0b11
};

struct BallData {
    uint8_t runs;      // 0–7 (7 = "7+")
    bool wicket;       // true if wicket fell
    Extras extras;     // type of extra
};

// Packs a BallData struct into a single byte
uint8_t packBall(const BallData& b) {
    assert(b.runs <= 7);                // fits in 3 bits
    uint8_t byte = 0;
    byte |= (b.runs & 0b111);           // bits 0–2
    byte |= (uint8_t(b.wicket) << 3);   // bit 3
    byte |= (uint8_t(b.extras) << 4);   // bits 4–5
    // bits 6–7 remain zero
    return byte;
}

// Unpacks a byte back into BallData
BallData unpackBall(uint8_t byte) {
    BallData b;
    b.runs    = byte & 0b111;                    // bits 0–2
    b.wicket  = bool((byte >> 3) & 0b1);         // bit 3
    b.extras  = Extras((byte >> 4) & 0b11);      // bits 4–5
    return b;
}

// Example usage
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
