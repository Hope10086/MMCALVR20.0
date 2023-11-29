
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
    precision mediump float;
    const vec2 TEXTURE_SIZE = vec2(%i,%i);
    )glsl";
const string HORIZONTAL_BLUR_SHADER = R"glsl(
    in vec2 uv;
    out vec4 fragColor;
    uniform sampler2D Texture0;
    uniform float ndcrad ;
    uniform vec2 lgazepoint;
    uniform vec2 rgazepoint;
    uniform float Qa; 
    uniform float Qb; 
    void main() {
        vec2  ndcradius = vec2( ndcrad, ndcrad *2.0);
        vec3 IsRightROI= ( length(uv.x-lgazepoint.x)<ndcradius.x && length(uv.y-lgazepoint.y)<ndcradius.y)? vec3(1.0):vec3(0.0);
        vec3 IsLeftROI=  ( length(uv.x-rgazepoint.x)<ndcradius.x && length(uv.y-rgazepoint.y)<ndcradius.y)? vec3(1.0):vec3(0.0);
        vec3 IsROI= IsRightROI+IsLeftROI;
        vec4 RoiValue = texture(Texture0, uv);

        float Y = 0.299 * RoiValue.r + 0.587 * RoiValue.g + 0.114 * RoiValue.b;
        float U = 0.492 * (RoiValue.b - Y);
        float V = 0.877 * (RoiValue.r - Y);

        float Qs = Qa *(1.0 + 1.0/(4.0*Y)) ;
        float  DelatY = round(Y * Qs) / Qs - Y;
        Y = Y + Qb* DelatY;

        
        vec3 NonRoiValue =vec3(Y + 1.13983*V , Y-0.39465*U-0.58060*V , Y+2.03211*U);
        fragColor = vec4(RoiValue.rgb* IsROI + NonRoiValue * (1.0-IsROI), RoiValue.a);
    }
)glsl";
 }

using namespace std;
using namespace gl_render_utils;
GaussianBlurPass::GaussianBlurPass(gl_render_utils::Texture *inputTexture) : mInputTexture(inputTexture) {}

void GaussianBlurPass::Initialize(uint32_t width, uint32_t height) {
    //Texture may be 3684 x 1920 because it will  scaled before rendering 
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


    // Create Requantization shader
    auto horizontalBlurShader = blurCommonShaderStr + HORIZONTAL_BLUR_SHADER;
    mRequantizationPipeline = unique_ptr<RenderPipeline>(
        new RenderPipeline(
       {mInputTexture},
        QUAD_2D_VERTEX_SHADER,
        horizontalBlurShader));
}

void GaussianBlurPass::Render(bool GaussionFlag,bool TDenabled,int GaussionStrategy ,float ndcroirad ,GazeCenterInfo LGazeCenter ,GazeCenterInfo RGazeCenter) {

    mOutputTextureState->ClearDepth();
    GaussianKernel5  TotalStrategys[12] = { { 0.0 ,1.0 ,256.0 }, // Lossless  0
                                            { 1.0 ,1.0, 128.0 },  //0.304     1
                                            { 3.0 ,1.0 ,64.0 },  //1.8        2
                                            { 2.0 ,1.0 ,64.0 },  //1.2        3
                                            { 1.0 ,1.0, 64.0 },   //0.599     4
                                            { 2.0 ,1.0 ,48.0 },  // 2.4       5

                                            { 3.0, 1.0, 48.0 },  //1.6        6
                                            { 0.0 ,1.0 ,256.0 },  //Qp = 23
                                            { 1.0 ,1.0, 64.0 },   //0.599
                                            { 3.0, 1.0, 32.0 },  //3.6
                                            { 2.0, 1.0, 32.0 },  //2.4    
                                            { 2.0, 1.0, 16.0 }  //worst quatity
                                                                                
                                            };
    GazeCenterInfo   DefaultGazeCenter[2] ={ {0.25 , 0.5},{0.75 ,0.5} }; 
    GaussianKernel5 Strategy;
    if (GaussionFlag)
    {
        Strategy = TotalStrategys[GaussionStrategy];
    }
    else if ( TDenabled )
            {
                Strategy = TotalStrategys[GaussionStrategy];
            }
            else
            {   Strategy = TotalStrategys[0];
            }
    // Rendering

    mRequantizationPipeline->MyRender(Strategy, LGazeCenter, RGazeCenter, ndcroirad,*mOutputTextureState);

}