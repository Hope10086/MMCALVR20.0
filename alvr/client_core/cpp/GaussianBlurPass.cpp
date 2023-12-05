
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
    uniform bool leftflag;
    uniform bool rightflag;
    uniform float righty;
    uniform vec3  RightBlockValue;
    uniform vec4  Blocklocation;
    void main() {
        vec2  ndcradius = vec2( ndcrad, ndcrad *2.0);
      //  to check current piexl is the Display Block or not
      //  vec3 IsCenterBlock = (( length(uv.x-0.25) < 0.01 && length(uv.y-0.5) < 0.02)||( length(uv.x-0.75) < 0.01 && length(uv.y-0.5) < 0.02))? vec3(1.0):vec3(0.0);
        vec3 IsLeftBlock = ((leftflag  == true) && (( length(uv.x-Blocklocation.r) < 0.01 && length(uv.y-0.45) < 0.02 *1.0)||( length(uv.x-Blocklocation.g) < 0.01 && length(uv.y-0.45) < 0.02 *1.0)))? vec3(1.0):vec3(0.0); // left(0.10,0.30)
        vec3 IsRightBlock =((rightflag == true) && (( length(uv.x-Blocklocation.b) < 0.01 && length(uv.y-0.45) < 0.02 *righty)||( length(uv.x-Blocklocation.a) < 0.01 && length(uv.y-0.45) < 0.02 *righty)))? vec3(1.0):vec3(0.0); // Right(0.40,0.20)
       
        vec3 IsRightROI= ( length(uv.x-lgazepoint.x)<ndcradius.x && length(uv.y-lgazepoint.y)<ndcradius.y)? vec3(1.0):vec3(0.0);
        vec3 IsLeftROI=  ( length(uv.x-rgazepoint.x)<ndcradius.x && length(uv.y-rgazepoint.y)<ndcradius.y)? vec3(1.0):vec3(0.0);
        //  to check current piexl is the Roi or not  but its priority  is lower than the Display Block  
     // vec3 IsROI= (IsCenterBlock  ==vec3(0.0)&& IsLeftBlock==vec3(0.0) && IsRightBlock ==vec3(0.0))?(IsRightROI+IsLeftROI):vec3(0.0);
        vec3 IsROI= (IsLeftBlock==vec3(0.0) && IsRightBlock ==vec3(0.0))?(IsRightROI+IsLeftROI):vec3(0.0);
        //  sample the input texture and compute the output value
        vec4 RoiValue = texture(Texture0, uv);

        float Y = 0.299 * RoiValue.r + 0.587 * RoiValue.g + 0.114 * RoiValue.b;
        float U = 0.492 * (RoiValue.b - Y);
        float V = 0.877 * (RoiValue.r - Y);

        float Qs = Qa *(1.0 + 1.0/(4.0*Y)) ;
        float  DelatY = round(Y * Qs) / Qs - Y;
        Y = Y + Qb* DelatY;
        // the Block Color
      //  vec3 CenterBlockValue = vec3(0.0, 1.0, 0.0);
        vec3 LeftBlockValue = vec3(1.0, 0.0, 0.0);
        //vec3 RightBlockValue = vec3(0.0, 0.0, 1.0);

        vec3 NonRoiValue =vec3(Y + 1.13983*V , Y-0.39465*U-0.58060*V , Y+2.03211*U);
        // fragColor = vec4( RoiValue.rgb* IsROI 
        //                 + NonRoiValue * (1.0-IsROI) 
        //                 + CenterBlockValue * IsCenterBlock 
        //                 + LeftBlockValue * IsLeftBlock 
        //                 + RightBlockValue * IsRightBlock
        //                 ,RoiValue.a);

        fragColor = vec4( RoiValue.rgb* IsROI + NonRoiValue * (1.0-IsROI -IsRightBlock -IsLeftBlock) ,RoiValue.a)
                   +vec4( LeftBlockValue * IsLeftBlock + RightBlockValue * IsRightBlock , 1.0);
    }
)glsl";
 } 

using namespace std;
using namespace gl_render_utils;

int ColorNum =0;
int LocationNum = 0;

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

void GaussianBlurPass::Render( OriAngle m_Angle ,bool GaussionFlag,bool TDenabled,int GaussionStrategy ,float ndcroirad ,GazeCenterInfo LGazeCenter ,GazeCenterInfo RGazeCenter) {

    mOutputTextureState->ClearDepth();
    GaussianKernel5  TotalStrategys[12] = { { 0.0 ,0.0 ,256.0 }, // Lossless  0
                                            { 1.0 ,1.0, 128.0 },  //0.304     1
                                            { 1.0 ,1.0 ,64.0 },  //1.8        2
                                            { 2.0 ,1.0 ,64.0 },  //1.2        3
                                            { 3.0 ,1.0, 64.0 },   //0.599     4


                                            { 2.0 ,1.0 ,48.0 },  // 2.4       5
                                            { 3.0, 1.0, 48.0 },  //1.6        6
                                           
                                            
                                            { 3.0, 1.0, 32.0 },  //3.6
                                            { 2.0, 1.0, 32.0 },  //2.4    
                                            { 2.0, 1.0, 16.0 },  //worst quatity
                                            { 3.0 ,1.0 ,16.0 },   //worst quatity
                                            { 3.0 ,1.0 ,12.0 }   //worst quatity
                                                                                
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
    // RightBlock's color & Location
    BlockColor Colors[9] ={
        {0.0, 1.0, 0.0},//Green
        {0.9, 0.0, 0.0},//Red
        {0.0, 0.0, 1.0},//Blue
        {1.0, 1.0, 0.0},//Yellow
        {0.0, 1.0, 1.0},//Cyan
        {1.0, 0.0, 1.0},//Magenta
        {1.0, 0.27, 0.0},//橘红色
        {0.0, 0.0, 0.0}, //Black
        {1.0, 1.0, 1.0,}  //White
     };
     float RightBlockLocationy[5] = {1.0,0.75,1.75,1.25,1.5};
   
    
     float head_zeroangle = 0.0;  // need to be changed to the real head zero angle
     float RightBlock_angle = head_zeroangle +60;
     float LeftBlock_angle  = head_zeroangle -15; // -90~ +90
    
    float leftblock_leftview = 0.15;
    float leftblock_rightview = 0.65;
    float rightblock_leftview = 0.35;
    float rightblock_rightview = 0.85;
    
    // 投影矩阵 并判断是否需要进行投影
    if ( (RightBlock_angle -m_Angle.head_x) > -42.0 && (RightBlock_angle -m_Angle.head_x) < 42.0)
    {   
        //Info("%lf - %lf - %lf", m_Angle.head_x, RightBlock_angle, (RightBlock_angle -m_Angle.head_x));
        // RightBlock shoud be shown
        RightBlock = 1;
        // 计算两个块在左右眼视图中的归一化坐标 并检查是否是处于合理范围中
        rightblock_leftview  =0.5*( tanf((RightBlock_angle -m_Angle.head_x) *PI/180.0)+tanf(0.942478))/2.21548;
        rightblock_rightview =0.5 + 0.5*( tanf((RightBlock_angle -m_Angle.head_x)*PI/180.0)+tanf(0.698132))/2.21548;
    }
    else 
    { RightBlock = 0;}

    if ( (LeftBlock_angle -m_Angle.head_x) > -42.0 && (LeftBlock_angle -m_Angle.head_x) < 42.0)
    {   
       // Info("%lf - %lf - %lf", m_Angle.head_x, LeftBlock_angle, (LeftBlock_angle -m_Angle.head_x));
        // LeftBlock shoud be shown
        LeftBlock = 1;
        leftblock_leftview  =0.5*( tanf((LeftBlock_angle -m_Angle.head_x) *PI/180.0)+tanf(0.942478))/2.21548;
        leftblock_rightview =0.5 + 0.5*( tanf((LeftBlock_angle -m_Angle.head_x)*PI/180.0)+tanf(0.698132))/2.21548;
    }
    else { LeftBlock = 0;}
    // float block_y = 0.45;
    // if ( m_Angle.head_y > -40.0 && m_Angle.head_y < 40.0)
    // {
    //     block_y = (tanf(m_Angle.head_y *PI/180.0)+tanf(0.733038))/1.739504;
    // }
    


    // 根据帧索引选择进行显示 两个Block
    // 按照 FPS=90 计算每10秒内  前2秒显示左方块  左侧方块消失的同时显示1.5秒的右块
    // 900帧中0~ 180 帧显示左块 271 ~ 406 帧显示右块

    if ( (m_FrameRenderIndex % 900) >= 0 &&(m_FrameRenderIndex % 900) < 180)
    {   LeftBlock = LeftBlock*1;
    }else  {LeftBlock = 0; }

    if ((m_FrameRenderIndex % 900) >= 181 &&(m_FrameRenderIndex % 900) < 316)
    {
        if (m_FrameRenderIndex % 900 == 181 )
        {   
           // Info("Change rightblock 's color & location!");
            ColorNum = std::rand() % 9 ;
            LocationNum = std::rand() % 5; 
        }
        if (m_FrameRenderIndex % 900 == 242 )
        {   
            //Info("Change rightblock 's color & location!");
            int ColorNum2 = std::rand() % 9 ;
            while (ColorNum2 == ColorNum)
            {
                ColorNum2 = std::rand() % 9 ;
            }
            ColorNum = ColorNum2;
        }

       // Info("NDC Left Gaze Location in the left view:%d ",int((leftblock_leftview-LGazeCenter.x)*5184));
        RightBlock =RightBlock*1;
    }else  {RightBlock =0; } 
    
    
    // Rendering
    //Info("FrameIndex %d LeftBlock %d RightBlock %d",m_FrameRenderIndex,LeftBlock, RightBlock);
     m_FrameRenderIndex++;
    mRequantizationPipeline->MyRender(RightBlockLocationy[LocationNum],
    Colors[ColorNum].r, Colors[ColorNum].g, Colors[ColorNum].b,
    LeftBlock, RightBlock, 
    Strategy, 
    leftblock_leftview,leftblock_rightview,rightblock_leftview,rightblock_rightview,
    LGazeCenter, RGazeCenter, ndcroirad,*mOutputTextureState);
   
}