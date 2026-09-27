#pragma once
 
#include "ConstantBuffers.h"
#include "Camera.h"
#include "RenderGraph.h"
class GraphicsContext;
class ColorBuffer;
class DepthBuffer;
namespace SSAO
{
    struct ViewResources
    {
        DepthBuffer& Depth;
        ColorBuffer& Normals;
        ColorBuffer& LinearFull;
        ColorBuffer& LinearHalf;
        ColorBuffer& OcclusionFull;
        ColorBuffer& OcclusionHalf;
        ColorBuffer& Blur;
    };

    class ViewPasses
    {
    public:
        explicit ViewPasses(ViewResources resources);
        ViewPasses(const ViewPasses&) = delete;
        ViewPasses& operator=(const ViewPasses&) = delete;
        void Render(GraphicsContext& context, const Math::Camera& camera);

    private:
        ViewResources m_Resources;
        RenderGraph m_Graph;
    };

    void Initialize();

    void Render(GraphicsContext& GfxContext, const Math::Camera& camera);

    void Shutdown();
 };
 
