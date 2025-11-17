#include "pch.h"
#include "FileSystem.h"
#include <filesystem>

void FileSystem::Initialize()
{
    auto currentDirectory = std::filesystem::current_path();

    // The asset directory is one folder within the root directory.
    while (!std::filesystem::exists(currentDirectory / "Assets"))
    {
        if (currentDirectory.has_parent_path())
        {
            currentDirectory = currentDirectory.parent_path();
        }
        else
        {
            ASSERT("Assets Directory not found!");
        }
    }

    auto assetsDirectory = currentDirectory / "Assets";

    if (!std::filesystem::is_directory(assetsDirectory))
    {
        ASSERT("Assets Directory that was located is not a directory!");
    }

    s_rootDirectoryPath = currentDirectory.string() + "/";

}