#include "pch.h"
#include "Util.h"
#include "GraphicsCore.h"
using Microsoft::WRL::ComPtr;


std::wstring Utility::StringToWString(const std::string_view& inputString)
{
    std::wstring result{};
    const std::string input{ inputString };

    const int32_t length = ::MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, NULL, 0);
    if (length > 0)
    {
        result.resize(size_t(length) - 1);
        MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, result.data(), length);
    }

    return std::move(result);
}

std::string Utility::WStringToString(const std::wstring_view& inputWString)
{
    std::string result{};
    const std::wstring input{ inputWString };

    const int32_t length = ::WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, NULL, 0, NULL, NULL);
    if (length > 0)
    {
        result.resize(size_t(length) - 1);
        WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, result.data(), length, NULL, NULL);
    }

    return std::move(result);
}
