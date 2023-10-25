
#include "GaussianBlurPass.h"
#include "utils.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <sys/types.h>

#include "gpulogger.h"

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

        float kernel[5] = float[5](a,b,center,b,a);
        float kernelWeight = (2.0*a +2.0*b + center);

        vec3 IsLeftROI = (length(uv.x - lgazepoint.x) < ndcradius.x && length(uv.y - lgazepoint.y) < ndcradius.y) ? vec3(1.0) : vec3(0.0);
        vec3 IsRightROI = (length(uv.x - rgazepoint.x) < ndcradius.x && length(uv.y - rgazepoint.y) < ndcradius.y) ? vec3(1.0) : vec3(0.0);
        vec4 RoiValue = texture(inputTexture, uv);

        vec3 leftonepiexl = texture(inputTexture , uv + vec2(-2.0, 0.0) /TEXTURE_SIZE).rgb;
        vec3 lefttwopiexl = texture(inputTexture , uv + vec2(-1.0, 0.0) /TEXTURE_SIZE).rgb;
        vec3 righttwoepiexl = texture(inputTexture , uv + vec2(1.0, 0.0) /TEXTURE_SIZE).rgb;
        vec3 rightonepiexl = texture(inputTexture , uv + vec2(2.0, 0.0) /TEXTURE_SIZE).rgb;

        vec3 NonRoiValue = (RoiValue.rgb * kernel[2]  
                           +leftonepiexl * kernel[0]
                           +lefttwopiexl * kernel[1]
                           +righttwoepiexl * kernel[3]
                           +rightonepiexl * kernel[4]
                           )/kernelWeight;
            
        fragColor = vec4(RoiValue.rgb * (IsLeftROI +IsRightROI) + NonRoiValue * (1.0-IsRightROI -IsLeftROI), RoiValue.a);
    }
)glsl";

const string VERTICAL_BLUR_SHADER = R"glsl(
    
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D inputTexture;

    void main() {
        vec2  ndcradius = vec2( ndcrad, ndcrad *2.0);

        float kernel[5] = float[5](a,b ,center ,b ,a);
        float kernelWeight = (2.0*a +2.0*b + center);

        vec4 color = texture(inputTexture, uv);
        vec3 result = vec3(0.0,0.0,0.0);

        if(  (length(uv.x-lgazepoint.x) < ndcradius.x && length(uv.y-lgazepoint.y) < ndcradius.y)
          || (length(uv.x-rgazepoint.x) < ndcradius.x && length(uv.y-rgazepoint.y) < ndcradius.y) )
        { 
            vec4 nearsample =  texture(inputTexture , uv);
            result = nearsample.rgb;
        }
        else
        {
            for (int i = -2; i <= 2; i++) {

             ivec2 yoffset = ivec2(0, i);  
             vec4 nearsample = texture(inputTexture , uv + vec2(0.0, yoffset.y) /TEXTURE_SIZE);
             result += nearsample.rgb * kernel[i+2] / kernelWeight;
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

void GaussianBlurPass::Render(int strategynum ,GazeCenterInfo LGazeCenter ,GazeCenterInfo RGazeCenter) {

    mstagOutputTex1State->ClearDepth();
    mOutputTextureState->ClearDepth();


GaussianKernel5  TotalStrategys[6] = { { 0.0 ,0.0 ,1.0 }, {0.0 ,1.0 ,2.0} ,{ 1.0, 2.0, 4.0 },
                                           { 1.0, 2.0, 2.0 }, {1.0 ,1.0 ,1.0 },{ 2.0 ,2.0 ,1.0 }
                                         };
GazeCenterInfo   DefaultGazeCenter[2] ={ {0.25 , 0.5},{0.75 ,0.5} }; 
    GaussianKernel5 Strategy;

    if (strategynum>=0 && strategynum<=5)
    {
        Strategy = TotalStrategys[strategynum];
    }
    else
    {   Strategy = TotalStrategys[0];
    }
    
    // Render horizontal blur
    mHorizontalBlurPipeline->MyRender(Strategy, LGazeCenter, RGazeCenter, 0.01,*mstagOutputTex1State);
    Info("HorizontalBlur Rendercost is %d us",mHorizontalBlurPipeline->timecost/1000);

    // Render vertical blur
    mVerticalBlurPipeline->MyRender(Strategy,  LGazeCenter, RGazeCenter, 0.01, *mOutputTextureState);
    Info("VerticalBlur RenderCost is %d us" , mVerticalBlurPipeline->timecost /1000);
}