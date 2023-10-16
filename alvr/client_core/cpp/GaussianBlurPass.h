#pragma once

#include "gl_render_utils/render_pipeline.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <GLES3/gl3.h>


struct GaussianKernel3
{
   float a ;

   float center ;
   float weight ;
};

struct GaussianKernel5
{
   float a ;
   float b ;

   float center ;
   float weight ;
};

struct GaussianKernel7
{
   float a ;
   float b ;
   float c ;

   float center ;
   float weight ;
};




class GaussianBlurPass {
public:
    GaussianBlurPass(gl_render_utils::Texture *inputTexture);

    void Initialize(uint32_t width, uint32_t height);
    void Render() const;

    gl_render_utils::Texture *GetOutputTexture() { return mOutputTexture.get(); }

private:
    gl_render_utils::Texture *mInputTexture;
    std::unique_ptr<gl_render_utils::Texture> mOutputTexture;
    std::unique_ptr<gl_render_utils::RenderState> mOutputTextureState;

    std::unique_ptr<gl_render_utils::Texture> mstagOutputTex1;
    std::unique_ptr<gl_render_utils::RenderState> mstagOutputTex1State;

    std::unique_ptr<gl_render_utils::RenderPipeline> mHorizontalBlurPipeline;
    std::unique_ptr<gl_render_utils::RenderPipeline> mVerticalBlurPipeline;
};


