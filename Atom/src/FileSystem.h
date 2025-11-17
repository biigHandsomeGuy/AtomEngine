#pragma once

class FileSystem
{
public:
    static inline std::string GetFullPath(const std::string_view assetPath)
    {
        return std::move(s_rootDirectoryPath + assetPath.data());
    }

    static inline std::wstring GetFullPath(const std::wstring_view assetPath)
    {
        return std::move(Utility::StringToWString(s_rootDirectoryPath) + assetPath.data());
    }

    static void Initialize();
    
private:
    static inline std::string s_rootDirectoryPath{};
};