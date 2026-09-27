#pragma once

#include <filesystem>
#include <string>
#include <string_view>

inline std::filesystem::path PathFromUtf8(std::string_view text)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::u8path(text.begin(), text.end());
#endif
}

inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
