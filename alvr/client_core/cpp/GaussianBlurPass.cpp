
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

    float roia = 0.0 ;
    float roib = 0.0 ;
    float roicenter = 1.0 ;
    
    uniform float ndcrad ;
    uniform vec2 lgazepoint;
    uniform vec2 rgazepoint;


    )glsl";

const string HORIZONTAL_BLUR_SHADER = R"glsl(
    
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D inputTexture;

    void main() {
        vec2  ndcradius = vec2( ndcrad, ndcrad *2.0);

        float kernel[5] = float[5](roia,roib ,roicenter ,roib ,roia);
        float kernelWeight = (2.0*roia +2.0*roib + roicenter)*(2.0*roia +2.0*roib + roicenter);



        if(  (length(uv.x-lgazepoint.x) < ndcradius.x && length(uv.y-lgazepoint.y) < ndcradius.y)
            || (length(uv.x-rgazepoint.x) < ndcradius.x && length(uv.y-rgazepoint.y) < ndcradius.y) )
        { 
           kernelWeight = (2.0*roia +2.0*roib + roicenter)*(2.0*roia +2.0*roib + roicenter);
            kernel[0] = roia;
            kernel[1] = roib;
            kernel[2] = roicenter;
            kernel[3] = roib;
            kernel[4] = roia;
        }
        else
        {
            kernelWeight = (2.0*a +2.0*b + center)*(2.0*a +2.0*b + center);    
            kernel[0] = a;
            kernel[1] = b;
            kernel[2] = center;
            kernel[3] = b;
            kernel[4] = a;
        }
        
        vec4 color = texture(inputTexture, uv);
        vec3 result = vec3(0.0,0.0,0.0);

        for (int i = -2; i <= 2; i++) {
        ivec2 xoffset = ivec2(i, 0);  
            
            for( int j = -2 ;j <= 2; j++){

                ivec2 yoffset = ivec2(0,j);

                vec4 nearsample = texture(inputTexture , uv + vec2(xoffset.x, yoffset.y) /TEXTURE_SIZE);

                result += nearsample.rgb * kernel[j+2] * kernel[i+2] / kernelWeight;
            }
        }

        fragColor = vec4(result , color.a);
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

    // mstagOutputTex1.reset((new Texture(false, 0, false, width *2, height)));
    // mstagOutputTex1State = make_unique<RenderState>(mstagOutputTex1.get());
    // gl_render_utils::Texture* stagTexture = mstagOutputTex1.get();

    // Create horizontal blur shader
    auto horizontalBlurShader = blurCommonShaderStr + HORIZONTAL_BLUR_SHADER;
    mHorizontalBlurPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
       {mInputTexture},
        QUAD_2D_VERTEX_SHADER,
        horizontalBlurShader));
  
    // Create vertical blur shader
    // auto verticalBlurShader = blurCommonShaderStr + VERTICAL_BLUR_SHADER;
    // mVerticalBlurPipeline = unique_ptr<RenderPipeline>(
    //     new RenderPipeline(
    //     {stagTexture},
    //     QUAD_2D_VERTEX_SHADER,
    //     verticalBlurShader ));

}

void GaussianBlurPass::Render(int strategynum) {

   // mstagOutputTex1State->ClearDepth();
    mOutputTextureState->ClearDepth();

    GaussianKernel5  TotalStrategys[6] = { { 0.0 ,0.0 ,1.0 }, {0.0 ,1.0 ,2.0} ,{ 1.0, 2.0, 4.0 },
                                           { 1.0, 2.0, 2.0 }, {1.0 ,1.0 ,1.0 },{ 2.0 ,2.0 ,1.0 }
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
    mHorizontalBlurPipeline->MyRender(Strategy.a,Strategy.b,Strategy.center,*mOutputTextureState);

    // Render vertical blur
   // mVerticalBlurPipeline->MyRender(Strategy.a,Strategy.b,Strategy.center,*mOutputTextureState);
}