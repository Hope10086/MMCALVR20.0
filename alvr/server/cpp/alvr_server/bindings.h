#pragma once

struct FfiFov {
    float left;
    float right;
    float up;
    float down;
};

struct FfiQuat {
    float x;
    float y;
    float z;
    float w;
};
struct FfiPose {
    float x;
    float y;
    float z;
    FfiQuat orientation;
};

struct FfiHandSkeleton {
    float jointPositions[26][3];
    FfiQuat jointRotations[26];
};

struct FfiDeviceMotion {
    unsigned long long deviceID;
    FfiQuat orientation;
    float position[3];
    float linearVelocity[3];
    float angularVelocity[3];
};

struct FfiEyeGaze
{
    float position[3];
    FfiQuat orientation;
};
struct FfiGazeOPOffset
{
    double x;
    double y;
};
struct FfiEuler
{
    float Yaw;
    float Pitch;
    float  Roll;
};
struct FfiAnglespeed
   {
	float w_head;
	float w_gaze;
	float w_eye;
   };
enum FfiOpenvrPropertyType {
    Bool,
    Float,
    Int32,
    Uint64,
    Vector3,
    Double,
    String,
};

union FfiOpenvrPropertyValue {
    unsigned int bool_;
    float float_;
    int int32;
    unsigned long long uint64;
    float vector3[3];
    double double_;
    char string[256];
};

struct FfiOpenvrProperty {
    unsigned int key;
    FfiOpenvrPropertyType type;
    FfiOpenvrPropertyValue value;
};

struct FfiViewsConfig {
    FfiFov fov[2];
    float ipd_m;
};

enum FfiButtonType {
    BUTTON_TYPE_BINARY,
    BUTTON_TYPE_SCALAR,
};

struct FfiButtonValue {
    FfiButtonType type;
    union {
        unsigned int binary;
        float scalar;
    };
};

struct FfiDynamicEncoderParams {
    unsigned int updated;
    unsigned long long bitrate_bps;
    float framerate;
};

extern "C" const unsigned char *FRAME_RENDER_VS_CSO_PTR;
extern "C" unsigned int FRAME_RENDER_VS_CSO_LEN;
extern "C" const unsigned char *FRAME_RENDER_PS_CSO_PTR;
extern "C" unsigned int FRAME_RENDER_PS_CSO_LEN;
extern "C" const unsigned char *QUAD_SHADER_CSO_PTR;
extern "C" unsigned int QUAD_SHADER_CSO_LEN;
extern "C" const unsigned char *COMPRESS_AXIS_ALIGNED_CSO_PTR;
extern "C" unsigned int COMPRESS_AXIS_ALIGNED_CSO_LEN;
extern "C" const unsigned char *COLOR_CORRECTION_CSO_PTR;
extern "C" unsigned int COLOR_CORRECTION_CSO_LEN;

extern "C" const unsigned char *QUAD_SHADER_COMP_SPV_PTR;
extern "C" unsigned int QUAD_SHADER_COMP_SPV_LEN;
extern "C" const unsigned char *COLOR_SHADER_COMP_SPV_PTR;
extern "C" unsigned int COLOR_SHADER_COMP_SPV_LEN;
extern "C" const unsigned char *FFR_SHADER_COMP_SPV_PTR;
extern "C" unsigned int FFR_SHADER_COMP_SPV_LEN;
extern "C" const unsigned char *RGBTOYUV420_SHADER_COMP_SPV_PTR;
extern "C" unsigned int RGBTOYUV420_SHADER_COMP_SPV_LEN;

extern "C" const char *g_sessionPath;
extern "C" const char *g_driverRootDir;

extern "C" void (*LogError)(const char *stringPtr);
extern "C" void (*LogWarn)(const char *stringPtr);
extern "C" void (*LogInfo)(const char *stringPtr);
extern "C" void (*LogDebug)(const char *stringPtr);
extern "C" void (*LogPeriodically)(const char *tag, const char *stringPtr);
extern "C" void (*DriverReadyIdle)(bool setDefaultChaprone);
extern "C" void (*InitializeDecoder)(const unsigned char *configBuffer, int len, int codec);
extern "C" void (*VideoSend)(unsigned long long targetTimestampNs,
                             unsigned char *buf,
                             int len,
                             bool isIdr);
extern "C" void (*HapticsSend)(unsigned long long path,
                               float duration_s,
                               float frequency,
                               float amplitude);
extern "C" void (*ControlInfoSend)( bool enable , int num,float roisize ,bool capflag ,float EyeSpeedThre);
extern "C" void (*ShutdownRuntime)();
extern "C" unsigned long long (*PathStringToHash)(const char *path);
extern "C" void (*ReportPresent)(unsigned long long timestamp_ns, unsigned long long offset_ns);
extern "C" void (*ReportComposed)(unsigned long long timestamp_ns, unsigned long long offset_ns);
extern "C" void (*ReportEncoded)(unsigned long long timestamp_ns);
extern "C" FfiDynamicEncoderParams (*GetDynamicEncoderParams)();
extern "C" unsigned long long (*GetSerialNumber)(unsigned long long deviceID, char *outString);
extern "C" void (*SetOpenvrProps)(unsigned long long deviceID);

extern "C" void (*WaitForVSync)();

extern "C" void *CppEntryPoint(const char *pInterfaceName, int *pReturnCode);
extern "C" void InitializeStreaming();
extern "C" void DeinitializeStreaming();
extern "C" void SendVSync();
extern "C" void RequestIDR();
extern "C" void SetTracking(unsigned long long targetTimestampNs,
                            float controllerPoseTimeOffsetS,
                            const FfiDeviceMotion *deviceMotions,
                            int motionsCount,
                            const FfiHandSkeleton *leftHand,
                            const FfiHandSkeleton *rightHand,
                            unsigned int controllersTracked,
                            const FfiEyeGaze *leftEyeGaze,
                            const FfiEyeGaze *rightEyeGaze,
                            const FfiEyeGaze *globalLeftGaze,
                            const FfiEyeGaze *globalRightGaze);
extern "C" void VideoErrorReportReceive();
extern "C" void ShutdownSteamvr();

extern "C" void SetOpenvrProperty(unsigned long long deviceID, FfiOpenvrProperty prop);
extern "C" void SetChaperone(float areaWidth, float areaHeight);
extern "C" void SetViewsConfig(FfiViewsConfig config);
extern "C" void SetBattery(unsigned long long deviceID, float gauge_value, bool is_plugged);
extern "C" void SetButton(unsigned long long path, FfiButtonValue value);

extern "C" void CaptureFrame();
extern "C" void AllQpChange(int delatqp);
extern "C" void ROIQpChange(int Strategy);
extern "C" void HQRSizeset( int delathqr);
extern "C" void CentrSizeset( int delatroi);
extern "C" void COF0set( float delatcof0);
extern "C" void COF1set( float delatcof1);
extern "C" void TestSequence( int DelatTestList , int DelatTestNum);
extern "C" void FPSReduce();
extern "C" void QPDistribution();
extern "C" void RecordGaze();
extern "C" void StopRecordGaze();
extern "C" void MaxQpSet( int delatMaxqp);
extern "C" void TDmode();
extern "C" void SpeedThresholdadd();
extern "C" void SpeedThresholdsub();
extern "C" void GazeVisual();
extern "C" void LogLatency(const char *stringPtr, ...) ;
extern "C" void CloseTxtFile();
extern "C" void UpdateGaussionStrategy(int delatnum);
extern "C" void GaussionEnable();
extern "C" void UpdateGaussionRoiSize(float RoiSizeRad);
extern "C" void ClientCapture();
extern "C" void HmdPoseOffset(const FfiPose *poseoffset ,bool positionlock, bool roationlock);
extern "C" void EyeMovementModeSet(float t);


// NalParsing.cpp
void ParseFrameNals(
    int codec, unsigned char *buf, int len, unsigned long long targetTimestampNs, bool isIdr);
