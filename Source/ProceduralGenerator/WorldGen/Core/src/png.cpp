#include "wg/png.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace wg {
namespace {

std::array<uint32_t, 256> MakeCrcTable() {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static const auto table = MakeCrcTable();
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void WriteChunk(FILE* f, const char type[4], const std::vector<uint8_t>& body) {
    std::vector<uint8_t> head;
    PutBe32(head, static_cast<uint32_t>(body.size()));
    fwrite(head.data(), 1, 4, f);
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), body.begin(), body.end());
    fwrite(td.data(), 1, td.size(), f);
    std::vector<uint8_t> crc;
    PutBe32(crc, Crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
    fwrite(crc.data(), 1, 4, f);
}

}  // namespace

bool WritePngRgb(const std::string& path, int width, int height, const uint8_t* rgb) {
    #if defined(_MSC_VER)
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0) f = nullptr;
    #else
        FILE* f = std::fopen(path.c_str(), "wb");
    #endif
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, f);

    std::vector<uint8_t> ihdr;
    PutBe32(ihdr, static_cast<uint32_t>(width));
    PutBe32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // RGB
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    WriteChunk(f, "IHDR", ihdr);

    // Сирі дані: на кожен рядок байт фільтра 0.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgb + static_cast<size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
    }

    // zlib-потік зі «stored»-блоками (без стиснення).
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    while (pos < raw.size() || raw.empty()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
        if (last) break;
    }
    PutBe32(z, (b << 16) | a);
    WriteChunk(f, "IDAT", z);
    WriteChunk(f, "IEND", {});
    std::fclose(f);
    return true;
}

}  // namespace wg
