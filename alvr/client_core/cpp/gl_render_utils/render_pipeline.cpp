#include "render_pipeline.h"
#include "../utils.h"

#define GL_GLEXT_PROTOTYPES
#define GL_EXT_disjoint_timer_query 1
using namespace std;

GLuint createShader(GLenum type, const string &shaderStr) {
    auto shader = glCreateShader(type);
    auto *shaderCStr = shaderStr.c_str();
    GL(glShaderSource(shader, 1, &shaderCStr, nullptr));

    GLint compiled;
    GL(glCompileShader(shader));
    GL(glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled));
    if (!compiled) {
        char errorLog[1000];
        GL(glGetShaderInfoLog(shader, sizeof(errorLog), nullptr, errorLog));
        LOGE("SHADER COMPILATION ERROR: %s\nSHADER:\n%s", errorLog, shaderCStr);
    }
    return shader;
}

namespace gl_render_utils {

RenderState::RenderState(const Texture *renderTarget) {
    mRenderTarget = renderTarget;
    mDepthTarget = make_unique<Texture>(false,
                                        0,
                                        false,
                                        renderTarget->GetWidth(),
                                        renderTarget->GetHeight(),
                                        GL_DEPTH_COMPONENT32F,
                                        GL_DEPTH_COMPONENT32F);

    GL(glGenFramebuffers(1, &mFrameBuffer));
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFrameBuffer));
    GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                              GL_COLOR_ATTACHMENT0,
                              mRenderTarget->GetTarget(),
                              renderTarget->GetGLTexture(),
                              0));
    GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                              GL_DEPTH_ATTACHMENT,
                              mDepthTarget->GetTarget(),
                              mDepthTarget->GetGLTexture(),
                              0));
}

RenderState::~RenderState() { GL(glDeleteFramebuffers(1, &mFrameBuffer)); }

void RenderState::ClearDepth() const {
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFrameBuffer));
    GL(glDisable(GL_SCISSOR_TEST));
    GL(glClear(GL_DEPTH_BUFFER_BIT));
}

GLuint RenderPipeline::mBindingPointCounter = 0;

RenderPipeline::RenderPipeline(const vector<const Texture *> &inputTextures,
                               const string &vertexShader,
                               const string &fragmentShader,
                               size_t uniformBlockSize) {
    mVertexShader = createShader(GL_VERTEX_SHADER, vertexShader);
    mFragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentShader);

    mProgram = glCreateProgram();
    GL(glAttachShader(mProgram, mVertexShader));
    GL(glAttachShader(mProgram, mFragmentShader));

    GLint linked;
    GL(glLinkProgram(mProgram));
    GL(glGetProgramiv(mProgram, GL_LINK_STATUS, &linked));
    if (!linked) {
        char errorLog[1000];
        GL(glGetProgramInfoLog(mProgram, sizeof(errorLog), nullptr, errorLog));
        LOGE("SHADER LINKING ERROR: %s", errorLog);
    }

    for (size_t i = 0; i < inputTextures.size(); i++) {
        mInputTexturesInfo.push_back(
            {inputTextures[i], glGetUniformLocation(mProgram, ("tex" + to_string(i)).c_str())});
    }

    mUniformBlockSize = uniformBlockSize;
    if (mUniformBlockSize > 0) {
        GL(glUniformBlockBinding(mProgram, 0, mBindingPointCounter));
        GL(glGenBuffers(1, &mBlockBuffer));
        GL(glBindBuffer(GL_UNIFORM_BUFFER, mBlockBuffer));
        GL(glBufferData(GL_UNIFORM_BUFFER, uniformBlockSize, nullptr, GL_DYNAMIC_DRAW));
        GL(glBindBufferBase(GL_UNIFORM_BUFFER, mBindingPointCounter, mBlockBuffer));
        mBindingPointCounter++;
    }

}

void RenderPipeline::Render(const RenderState &renderState, const void *uniformBlockData) const {
    GL(glUseProgram(mProgram));
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderState.GetFrameBuffer()));

    GL(glDisable(GL_SCISSOR_TEST));
    GL(glDepthMask(GL_TRUE));
    GL(glDisable(GL_CULL_FACE));
    GL(glEnable(GL_DEPTH_TEST));
    GL(glEnable(GL_BLEND));
    GL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL(glViewport(0,
                  0,
                  renderState.GetRenderTarget()->GetWidth(),
                  renderState.GetRenderTarget()->GetHeight()));

    for (size_t i = 0; i < mInputTexturesInfo.size(); i++) {
        GL(glActiveTexture(GL_TEXTURE0 + i));
        GL(glBindTexture(mInputTexturesInfo[i].texture->GetTarget(),
                         mInputTexturesInfo[i].texture->GetGLTexture()));
        GL(glUniform1i(mInputTexturesInfo[i].uniformLocation, i));
    }

    if (uniformBlockData != nullptr) {
        GL(glBindBuffer(GL_UNIFORM_BUFFER, mBlockBuffer));
        GL(glBufferSubData(GL_UNIFORM_BUFFER, 0, mUniformBlockSize, uniformBlockData));
    }

    GL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
}

void RenderPipeline::MyRender(  float rightblockY,float color_r, float color_g, float color_b,int leftblock, int rightblock, GaussianKernel5 NonRoiStrategy,
                                float leftblock_leftview, float leftblock_rightview, float rightblock_leftview, float rightblock_rightview, 
                                GazeCenterInfo LeftCenter, GazeCenterInfo RightCenter, float roisize, const RenderState &renderState, const void *uniformBlockData)  {
    

    GLuint  Qa = GL(glGetUniformLocation(mProgram,"Qa"));
    GLuint  Qb = GL(glGetUniformLocation(mProgram,"Qb"));

    GLuint ndcrad =GL (glGetUniformLocation(mProgram,"ndcrad"));
    GLuint lgazepoint =GL (glGetUniformLocation(mProgram,"lgazepoint"));
    GLuint rgazepoint =GL (glGetUniformLocation(mProgram,"rgazepoint"));

    GLuint rightflag = GL(glGetUniformLocation(mProgram,"rightflag"));
    GLuint leftflag  = GL(glGetUniformLocation(mProgram,"leftflag"));
    GLuint rightY =    GL(glGetUniformLocation(mProgram,"righty"));
    GLuint rightcolor =GL(glGetUniformLocation(mProgram,"RightBlockValue"));
    GLuint blocklocation = GL(glGetUniformLocation(mProgram,"Blocklocation"));

    GL(glUseProgram(mProgram));

    GL(glUniform1i(rightflag, rightblock));
    GL(glUniform1i(leftflag,  leftblock));
    GL(glUniform1f(rightY , rightblockY));
    GL(glUniform3f(rightcolor, color_r, color_g, color_b));
    GL(glUniform4f(blocklocation, leftblock_leftview,leftblock_rightview ,rightblock_leftview ,rightblock_rightview));

    GL(glUniform1f(Qa , NonRoiStrategy.center));
    GL(glUniform1f(Qb , NonRoiStrategy.a));
    if (roisize >0.2)
        {
            GL(glUniform1f(ndcrad,0.2));
        }
        else if(roisize >0){
            GL(glUniform1f(ndcrad,roisize));
        }
        else{ GL(glUniform1f(ndcrad,0.00));}

 

    GL(glUniform2f(lgazepoint, LeftCenter.x,LeftCenter.y));
    GL(glUniform2f(rgazepoint, RightCenter.x, RightCenter.y));

    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderState.GetFrameBuffer()));

    GL(glDisable(GL_SCISSOR_TEST));
    GL(glDepthMask(GL_TRUE));
    GL(glDisable(GL_CULL_FACE));
    GL(glEnable(GL_DEPTH_TEST));
    GL(glEnable(GL_BLEND));
    GL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL(glViewport(0,
                  0,
                  renderState.GetRenderTarget()->GetWidth(),
                  renderState.GetRenderTarget()->GetHeight()));

    for (size_t i = 0; i < mInputTexturesInfo.size(); i++) {
        GL(glActiveTexture(GL_TEXTURE0 + i));
        GL(glBindTexture(mInputTexturesInfo[i].texture->GetTarget(),
                         mInputTexturesInfo[i].texture->GetGLTexture()));
        GL(glUniform1i(mInputTexturesInfo[i].uniformLocation, i));
    }

    if (uniformBlockData != nullptr) {
        GL(glBindBuffer(GL_UNIFORM_BUFFER, mBlockBuffer));
        GL(glBufferSubData(GL_UNIFORM_BUFFER, 0, mUniformBlockSize, uniformBlockData));
    }

    GL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
}

RenderPipeline::~RenderPipeline() {
    if (GL_TRUE == glIsBuffer(mBlockBuffer)) {
        GL(glDeleteBuffers(1, &mBlockBuffer));
    }
    if (GL_TRUE == glIsShader(mVertexShader)) {
        GL(glDetachShader(mProgram, mVertexShader));
        GL(glDeleteShader(mVertexShader));
    }
    if (GL_TRUE == glIsShader(mFragmentShader)) {
        GL(glDetachShader(mProgram, mFragmentShader));
        GL(glDeleteShader(mFragmentShader));
    }
    if (GL_TRUE == glIsProgram(mProgram)) {
        GL(glDeleteProgram(mProgram));
    }
}
} // namespace gl_render_utils
