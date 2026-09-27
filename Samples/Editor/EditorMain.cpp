#include "pch.h"
#include "EditorLayer.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int show)
{
    PbrApplication app(std::make_unique<AtomEditor::EditorLayer>(), PbrApplication::ParseSmokeFrameLimit(commandLine));
    return GameCore::RunApplication(app, L"Atom Editor", instance, show);
}
