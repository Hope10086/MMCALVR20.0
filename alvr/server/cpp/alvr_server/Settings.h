#pragma once

#include "ALVR-common/packet_types.h"
#include <string>

class Settings {
    static Settings m_Instance;
    bool m_loaded;

    Settings();
    virtual ~Settings();

  public:
    void Load();
    static Settings &Instance() { return m_Instance; }

    bool IsLoaded() { return m_loaded; }

    int m_refreshRate;
    uint32_t m_renderWidth;
    uint32_t m_renderHeight;
    int32_t m_recommendedTargetWidth;
    int32_t m_recommendedTargetHeight;
    int32_t m_nAdapterIndex;
    std::string m_captureFrameDir;

    bool m_enableFoveatedRendering;
    float m_foveationCenterSizeX;
    float m_foveationCenterSizeY;
    float m_foveationCenterShiftX;
    float m_foveationCenterShiftY;
    float m_foveationEdgeRatioX;
    float m_foveationEdgeRatioY;

    bool m_enableColorCorrection;
    float m_brightness;
    float m_contrast;
    float m_saturation;
    float m_gamma;
    float m_sharpening;

    int m_codec;
    bool m_use10bitEncoder;
    bool m_enableVbaq;
    bool m_usePreproc;
    uint32_t m_preProcSigma;
    uint32_t m_preProcTor;
    uint32_t m_amdEncoderQualityPreset;
    uint32_t m_nvencQualityPreset;
    uint32_t m_rateControlMode;
    bool m_fillerData;
    uint32_t m_entropyCoding;
    bool m_force_sw_encoding;
    uint32_t m_swThreadCount;

    uint32_t m_nvencTuningPreset;
    uint32_t m_nvencMultiPass;
    uint32_t m_nvencAdaptiveQuantizationMode;
    int64_t m_nvencLowDelayKeyFrameScale;
    int64_t m_nvencRefreshRate;
    bool m_nvencEnableIntraRefresh;
    int64_t m_nvencIntraRefreshPeriod;
    int64_t m_nvencIntraRefreshCount;
    int64_t m_nvencMaxNumRefFrames;
    int64_t m_nvencGopLength;
    int64_t m_nvencPFrameStrategy;
    int64_t m_nvencRateControlMode;
    int64_t m_nvencRcBufferSize;
    int64_t m_nvencRcInitialDelay;
    int64_t m_nvencRcMaxBitrate;
    int64_t m_nvencRcAverageBitrate;
    bool m_nvencEnableWeightedPrediction;

    bool m_aggressiveKeyframeResend;

    bool m_enableViveTrackerProxy = false;
    bool m_TrackingRefOnly = false;
    bool m_enableLinuxVulkanAsync;

    bool m_enableControllers;
    int m_controllerMode = 0;
    bool m_overrideTriggerThreshold;
    float m_triggerThreshold;
    bool m_overrideGripThreshold;
    float m_gripThreshold;
    // Roi Size 
    int m_RoiSize = 6;
    // CentreSize
    int m_centresize=0;
    // QP mode 
    int m_delatQPmode = 13;
    int m_delatRoiQP = 20;
    int m_RoiQpStraetgy = 1;
    int m_MaxQp = 51;
    int m_FrameRenderIndex = 0;
    int m_FrameEncodeIndex = 0;

    int m_testnum = 0;
    int m_testlist = 0;
    int m_fpsReduce = 1;
    //拟合系数delta
    float m_cof0=27.54;
    float m_cof1=0.01004;
    //拟合模式切换（三种，0：阶跃；1：方形辐射；2：圆形辐射）
    int m_QPDistribution=0;
    //capture 
    bool m_capturePicture = false;
    bool m_recordGaze = false;
    //gaze visual 
    bool m_gazevisual = false ;
    bool newlogpath =true;

    bool m_tdmode = false;
    float m_speedthreshold=100;
    FfiPose m_poseoffset;
    bool m_enable_lockpositon;
    bool m_enable_lockrotation;
    float m_eyespeedthre=1000.0;
};
