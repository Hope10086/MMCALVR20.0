
#include "GaussianBlurPass.h"
#include "utils.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <sys/types.h>


using namespace std;
using namespace gl_render_utils;

namespace {
const string HORIZONTAL_BLUR_SHADER = R"glsl(#version 300 es
    #extension GL_OES_EGL_image_external_essl3 : enable
    precision mediump float;
    in vec2 uv;
    out vec4 fragColor;
    
    uniform sampler2D inputTexture;
    const float kernel[7] = float[7](1.0, 1.0, 1.0, 0.5 ,1.0 ,1.0 ,1.0);
    const float kernelWeight = 7.0;
    const int uTextureWidth = 5184 ; 
    const int uTextureHeight = 2592; 

    void main() {
        vec4 color = texture(inputTexture, uv);
        vec3 result = color.rgb * kernel[3];
       for (int i = -3; i <= 3; i++) {
        ivec2 offset = ivec2(i, 0);
 
        vec4 nearsample = texture(inputTexture, uv + vec2(offset.x, 0.0) / vec2(uTextureWidth, uTextureHeight));

        result += nearsample.rgb * kernel[i + 3];
        }
        fragColor = vec4(result / kernelWeight, color.a);
    }
)glsl";



const string  VERTICAL_BLUR_SHADER = R"glsl(#version 300 es
    #extension GL_OES_EGL_image_external_essl3 : enable
    precision mediump float;
    in vec2 uv;
    out vec4 fragColor;
    
    uniform sampler2D inputTexture;
    const float kernel[7] = float[7](1.0, 1.0, 1.0, 0.5 ,1.0 ,1.0 ,1.0);
    const float kernelWeight = 7.0;
    const int uTextureWidth = 5184 ; 
    const int uTextureHeight = 2592; 

    void main() {
        vec4 color = texture(inputTexture, uv);
        vec3 result = color.rgb * kernel[3];
       for (int i = -3; i <= 3; i++) {
        ivec2 offset = ivec2(0, i);
 
        vec4 nearsample = texture(inputTexture, uv + vec2(0.0 ,offset.y) / vec2(uTextureWidth, uTextureHeight));

        result += nearsample.rgb * kernel[i + 3];
        }
        fragColor = vec4(result / kernelWeight, color.a);
    }
)glsl";
}

using namespace std;
using namespace gl_render_utils;
GaussianBlurPass::GaussianBlurPass(gl_render_utils::Texture *inputTexture) : mInputTexture(inputTexture) {}

void GaussianBlurPass::Initialize(uint32_t width, uint32_t height) {
    //
    
    // Create output texture
    //mOutputTexture = make_unique<Texture>(false, 0, false, width, height);
    mOutputTexture.reset (new Texture(false, 0, false, width *2, height));
    mOutputTextureState = make_unique<RenderState>(mOutputTexture.get());

    // Create  first blur shader output texture

    mstagOutputTex1.reset((new Texture(false, 0, false, width *2, height)));
    mstagOutputTex1State = make_unique<RenderState>(mstagOutputTex1.get());
    gl_render_utils::Texture* stagTexture = mstagOutputTex1.get();

    // Create horizontal blur shader
    auto horizontalBlurShader = HORIZONTAL_BLUR_SHADER;
    mHorizontalBlurPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
       {mInputTexture},
        QUAD_2D_VERTEX_SHADER,
        horizontalBlurShader));
  
    // Create vertical blur shader
    auto verticalBlurShader = VERTICAL_BLUR_SHADER;
    mVerticalBlurPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
        {stagTexture},
        QUAD_2D_VERTEX_SHADER,
        verticalBlurShader ));

}

void GaussianBlurPass::Render() const {

    mstagOutputTex1State->ClearDepth();
    mOutputTextureState->ClearDepth();
    // Render horizontal blur
    

    mHorizontalBlurPipeline->Render(*mstagOutputTex1State);

    // Render vertical blur
    mVerticalBlurPipeline->Render(*mOutputTextureState);

}