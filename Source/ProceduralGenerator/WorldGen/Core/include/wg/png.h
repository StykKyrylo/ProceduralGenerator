#pragma once
#include <cstdint>
#include <string>

namespace wg {

// Мінімальний PNG-writer (RGB 8 біт, без стиснення, без зовнішніх бібліотек).
// Повертає false, якщо файл не вдалося записати.
bool WritePngRgb(const std::string& path, int width, int height, const uint8_t* rgb);

}  // namespace wg
