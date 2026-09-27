#include "pch.h"
#include "PbrApplication.h"
#include "PbrUiPanels.h"

namespace
{
    class SampleUi final : public PbrApplicationUi
    {
        void Draw(Scene&, PbrRenderer& renderer, const ViewportImage& image) override
        {
            DrawRendererSettings(renderer);
            DrawRenderViewport(image);
        }
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int show)
{
    PbrApplication app(std::make_unique<SampleUi>(), PbrApplication::ParseSmokeFrameLimit(commandLine));
    return GameCore::RunApplication(app, L"Atom PBR Sample", instance, show);
}
