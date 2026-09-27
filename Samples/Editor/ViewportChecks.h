#pragma once
#include <filesystem>
#include "..\PBR\PbrApplication.h"

namespace AtomEditor
{
    ApplicationValidation CheckViewport(const Scene& source, const std::filesystem::path& root);
}
