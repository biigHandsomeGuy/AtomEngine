#pragma once
#include <filesystem>

class FileSystem
{
public:
    static inline std::string GetFullPath(const std::string_view assetPath)
    {
        std::filesystem::path fullPath = std::filesystem::path(s_rootDirectoryPath) / std::filesystem::path(assetPath);
        return fullPath.generic_string();
    }

    static inline std::wstring GetFullPath(const std::wstring_view assetPath)
    {
        std::filesystem::path fullPath = std::filesystem::path(Utility::StringToWString(s_rootDirectoryPath)) / std::filesystem::path(assetPath);
        return fullPath.generic_wstring();
    }

    static void Initialize();
    
private:
    static inline std::string s_rootDirectoryPath{};
};
