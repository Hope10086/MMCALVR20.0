
#include "GaussianBlurPass.h"
#include "utils.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <sys/types.h>


using namespace std;
using namespace gl_render_utils;

namespace {

const string BLUR_COMMON_SHADER_FORMAT = R"glsl(#version 300 es
    #extension GL_OES_EGL_image_external_essl3 : enable
    precision highp float;

    const vec2 TEXTURE_SIZE = vec2(%i,%i);
    uniform float a ;
    uniform float b ;
    uniform float center ;


    )glsl";

const string HORIZONTAL_BLUR_SHADER = R"glsl(
    
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D inputTexture;

    void main() {
        float kernelWeight = 2.0*a +2.0*b + 2.0*center;    
        float kernel[5] = float[5](a, b, center, b ,a); 
        vec4 color = texture(inputTexture, uv);
        vec3 result = color.rgb * kernel[2];
       for (int i = -2; i <= 2; i++) {
        ivec2 offset = ivec2(i, 0);
 
        vec4 nearsample = texture(inputTexture, uv + vec2(offset.x, 0.0) / TEXTURE_SIZE);

        result += nearsample.rgb * kernel[i + 2];
        }
        fragColor = vec4(result / kernelWeight, color.a);
    }
)glsl";


const string  VERTICAL_BLUR_SHADER = R"glsl(

    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D inputTexture;

    void main() {
        float kernelWeight = 2.0*a +2.0*b + 2.0*center;    
        float kernel[5] = float[5](a, b, center, b ,a);
        vec4 color = texture(inputTexture, uv);
        vec3 result = color.rgb * kernel[2];
       for (int i = -2; i <= 2; i++) {
        ivec2 offset = ivec2(0, i);
 
        vec4 nearsample = texture(inputTexture, uv + vec2(0.0 ,offset.y) /TEXTURE_SIZE);

        result += nearsample.rgb * kernel[i + 2];
        }
        fragColor = vec4(result / kernelWeight, color.a);
    }
)glsl";
}

using namespace std;
using namespace gl_render_utils;
GaussianBlurPass::GaussianBlurPass(gl_render_utils::Texture *inputTexture) : mInputTexture(inputTexture) {}

void GaussianBlurPass::Initialize(uint32_t width, uint32_t height) {
    //Texture may be 3684 x 1920 because it will  scaled before rendering 
  //  GaussianKernel5 Kernel = {0.0 , 0.0, 2.0, 2.0};
    int iwidth = 2*width;
    int iheight =  height;
    auto  blurCommonShaderStr = string_format(BLUR_COMMON_SHADER_FORMAT,
                                             iwidth,
                                             iheight
                                             );
    // Create output texture
    //mOutputTexture = make_unique<Texture>(false, 0, false, width, height);
    mOutputTexture.reset (new Texture(false, 0, false, width *2, height));
    mOutputTextureState = make_unique<RenderState>(mOutputTexture.get());

    // Create  first blur shader output texture

    mstagOutputTex1.reset((new Texture(false, 0, false, width *2, height)));
    mstagOutputTex1State = make_unique<RenderState>(mstagOutputTex1.get());
    gl_render_utils::Texture* stagTexture = mstagOutputTex1.get();

    // Create horizontal blur shader
    auto horizontalBlurShader = blurCommonShaderStr + HORIZONTAL_BLUR_SHADER;
    mHorizontalBlurPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
       {mInputTexture},
        QUAD_2D_VERTEX_SHADER,
        horizontalBlurShader));
  
    // Create vertical blur shader
    auto verticalBlurShader = blurCommonShaderStr + VERTICAL_BLUR_SHADER;
    mVerticalBlurPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
        {stagTexture},
        QUAD_2D_VERTEX_SHADER,
        verticalBlurShader ));

}

void GaussianBlurPass::Render(int strategynum) {

    mstagOutputTex1State->ClearDepth();
    mOutputTextureState->ClearDepth();

    GaussianKernel5  TotalStrategys[6] = { { 0.0 ,0.0 ,2.0 }, {0.0 ,1.0 ,2.0} ,{ 1.0, 2.0, 4.0 },
                                           { 1.0, 2.0, 2.0 }, {1.0 ,1.0 ,1.0 },{ 2.0 ,1.0 ,2.0 }
                                         };
    GaussianKernel5 Strategy;

    if (strategynum>=0 && strategynum<=5)
    {
        Strategy = TotalStrategys[strategynum];
    }
    else
    {   Strategy = TotalStrategys[0];
    }
    
    // Render horizontal blur
    mHorizontalBlurPipeline->MyRender(Strategy.a,Strategy.b,Strategy.center,*mstagOutputTex1State);

    // Render vertical blur
    mVerticalBlurPipeline->MyRender(Strategy.a,Strategy.b,Strategy.center,*mOutputTextureState);
}