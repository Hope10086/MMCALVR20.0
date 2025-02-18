#pragma once

#include "gl_render_utils/render_pipeline.h"
#include <cstdint>
#include <memory>
#include <vector>

#include <GLES3/gl3.h>
#include "gazeinfo.h"
#include "gpulogger.h"
#include<math.h>
#define PI 3.1415926535

struct BlockColor
{
    float r;
    float g;
    float b;
};

class GaussianBlurPass {
public:
    GaussianBlurPass(gl_render_utils::Texture *inputTexture);

    void Initialize(uint32_t width, uint32_t height);
    void Render( OriAngle m_Angle ,bool GaussionFlag,bool TDenabled,int GaussionStrategy ,float ndcroirad,
                GazeCenterInfo LGazeCenter ,GazeCenterInfo RGazeCenter);
    gl_render_utils::Texture *GetOutputTexture() { return mOutputTexture.get(); }

private:
    gl_render_utils::Texture *mInputTexture;
    std::unique_ptr<gl_render_utils::Texture> mOutputTexture;
    std::unique_ptr<gl_render_utils::RenderState> mOutputTextureState;
    std::unique_ptr<gl_render_utils::RenderPipeline> mRequantizationPipeline;
   long long  int m_FrameRenderIndex = 0;
    int LeftBlock = 0;
    int RightBlock = 0;

};


