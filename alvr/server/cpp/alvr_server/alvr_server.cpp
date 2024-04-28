#ifdef _WIN32
#include "platform/win32/CEncoder.h"
#include <windows.h>
#elif __APPLE__
#include "platform/macos/CEncoder.h"
#else
#include "platform/linux/CEncoder.h"
#endif
#include "Controller.h"
#include "HMD.h"
#include "Logger.h"
#include "Paths.h"
#include "PoseHistory.h"
#include "Settings.h"
#include "TrackedDevice.h"
#include "bindings.h"
#include "driverlog.h"
#include "openvr_driver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>


#include "Utils.h"
#ifdef __linux__
vr::HmdMatrix34_t GetRawZeroPose();
#endif
static void load_debug_privilege(void) {
#ifdef _WIN32
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID val;

    if (!OpenProcessToken(GetCurrentProcess(), flags, &token)) {
        return;
    }

    if (!!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &val)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = val;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL);
    }

    if (!!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = val;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL)) {
            Warn("[GPU PRIO FIX] Could not set privilege to increase GPU priority\n");
        }
    }

    Debug("[GPU PRIO FIX] Succeeded to set some sort of priority.\n");

    CloseHandle(token);
#endif
}

class DriverProvider : public vr::IServerTrackedDeviceProvider {
  public:
    std::unique_ptr<Hmd> hmd;
    std::unique_ptr<Controller> left_controller, right_controller;
    // std::vector<ViveTrackerProxy> generic_trackers;

    std::map<uint64_t, TrackedDevice *> tracked_devices;

    virtual vr::EVRInitError Init(vr::IVRDriverContext *pContext) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(pContext);
        InitDriverLog(vr::VRDriverLog());

        this->hmd = std::make_unique<Hmd>();
        this->tracked_devices.insert({HEAD_ID, (TrackedDevice *)this->hmd.get()});
        if (vr::VRServerDriverHost()->TrackedDeviceAdded(this->hmd->get_serial_number().c_str(),
                                                         this->hmd->GetDeviceClass(),
                                                         this->hmd.get())) {
        } else {
            Warn("Failed to register HMD device");
        }

        if (Settings::Instance().m_enableControllers) {
            this->left_controller = std::make_unique<Controller>(LEFT_HAND_ID);
            this->right_controller = std::make_unique<Controller>(RIGHT_HAND_ID);

            this->tracked_devices.insert(
                {LEFT_HAND_ID, (TrackedDevice *)this->left_controller.get()});
            this->tracked_devices.insert(
                {RIGHT_HAND_ID, (TrackedDevice *)this->right_controller.get()});

            if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
                    this->left_controller->get_serial_number().c_str(),
                    this->left_controller->getControllerDeviceClass(),
                    this->left_controller.get())) {
                Warn("Failed to register left controller");
            }
            if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
                    this->right_controller->get_serial_number().c_str(),
                    this->right_controller->getControllerDeviceClass(),
                    this->right_controller.get())) {
                Warn("Failed to register right controller");
            }
        }

        return vr::VRInitError_None;
    }
    virtual void Cleanup() override {
        this->left_controller.reset();
        this->right_controller.reset();
        this->hmd.reset();

        CleanupDriverLog();

        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    virtual const char *const *GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    virtual const char *GetTrackedDeviceDriverVersion() {
        return vr::ITrackedDeviceServerDriver_Version;
    }
    virtual void RunFrame() override {
        vr::VREvent_t event;
        while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(vr::VREvent_t))) {
            if (event.eventType == vr::VREvent_Input_HapticVibration) {
                vr::VREvent_HapticVibration_t haptics = event.data.hapticVibration;

                uint64_t id = 0;
                if (this->left_controller &&
                    haptics.containerHandle == this->left_controller->prop_container) {
                    id = LEFT_HAND_ID;
                } else if (this->right_controller &&
                           haptics.containerHandle == this->right_controller->prop_container) {
                    id = RIGHT_HAND_ID;
                }

                HapticsSend(id, haptics.fDurationSeconds, haptics.fFrequency, haptics.fAmplitude);
            }
#ifdef __linux__
            else if (event.eventType == vr::VREvent_ChaperoneUniverseHasChanged) {
                if (hmd && hmd->m_poseHistory) {
                    hmd->m_poseHistory->SetTransformUpdating();
                    hmd->m_poseHistory->SetTransform(GetRawZeroPose());
                }
            }
#endif
        }
    }
    virtual bool ShouldBlockStandbyMode() override { return false; }
    virtual void EnterStandby() override {}
    virtual void LeaveStandby() override {}
} g_driver_provider;

// bindigs for Rust

const unsigned char *FRAME_RENDER_VS_CSO_PTR;
unsigned int FRAME_RENDER_VS_CSO_LEN;
const unsigned char *FRAME_RENDER_PS_CSO_PTR;
unsigned int FRAME_RENDER_PS_CSO_LEN;
const unsigned char *QUAD_SHADER_CSO_PTR;
unsigned int QUAD_SHADER_CSO_LEN;
const unsigned char *COMPRESS_AXIS_ALIGNED_CSO_PTR;
unsigned int COMPRESS_AXIS_ALIGNED_CSO_LEN;
const unsigned char *COLOR_CORRECTION_CSO_PTR;
unsigned int COLOR_CORRECTION_CSO_LEN;

const unsigned char *QUAD_SHADER_COMP_SPV_PTR;
unsigned int QUAD_SHADER_COMP_SPV_LEN;
const unsigned char *COLOR_SHADER_COMP_SPV_PTR;
unsigned int COLOR_SHADER_COMP_SPV_LEN;
const unsigned char *FFR_SHADER_COMP_SPV_PTR;
unsigned int FFR_SHADER_COMP_SPV_LEN;
const unsigned char *RGBTOYUV420_SHADER_COMP_SPV_PTR;
unsigned int RGBTOYUV420_SHADER_COMP_SPV_LEN;

const char *g_sessionPath;
const char *g_driverRootDir;

bool gaussionblurflag = false;
int  strategynum =0 ;
uint8_t frameindex = 0 ;
float roiradius =0.00;
float eyespeedt =1000.0;
void (*LogError)(const char *stringPtr);
void (*LogWarn)(const char *stringPtr);
void (*LogInfo)(const char *stringPtr);
void (*LogDebug)(const char *stringPtr);
void (*LogPeriodically)(const char *tag, const char *stringPtr);
void (*DriverReadyIdle)(bool setDefaultChaprone);
void (*InitializeDecoder)(const unsigned char *configBuffer, int len, int codec);
void (*VideoSend)(unsigned long long targetTimestampNs, unsigned char *buf, int len, bool isIdr);
void (*HapticsSend)(unsigned long long path, float duration_s, float frequency, float amplitude );
void (*ControlInfoSend)( bool enable , int num ,float roisize,bool capflag,float EyeSpeedThre);
void (*ShutdownRuntime)();
unsigned long long (*PathStringToHash)(const char *path);
void (*ReportPresent)(unsigned long long timestamp_ns, unsigned long long offset_ns);
void (*ReportComposed)(unsigned long long timestamp_ns, unsigned long long offset_ns);
void (*ReportEncoded)(unsigned long long timestamp_ns);
FfiDynamicEncoderParams (*GetDynamicEncoderParams)();
unsigned long long (*GetSerialNumber)(unsigned long long deviceID, char *outString);
void (*SetOpenvrProps)(unsigned long long deviceID);
void (*WaitForVSync)();

void *CppEntryPoint(const char *interface_name, int *return_code) {
    // Initialize path constants
    init_paths();

    Settings::Instance().Load();

    load_debug_privilege();

    if (std::string(interface_name) == vr::IServerTrackedDeviceProvider_Version) {
        *return_code = vr::VRInitError_None;
        return &g_driver_provider;
    } else {
        *return_code = vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
}

void InitializeStreaming() {
    Settings::Instance().Load();

    if (g_driver_provider.hmd) {
        g_driver_provider.hmd->StartStreaming();
    }
}

void DeinitializeStreaming() {
    if (g_driver_provider.hmd) {
        g_driver_provider.hmd->StopStreaming();
    }
}

void SendVSync() { vr::VRServerDriverHost()->VsyncEvent(0.0); }

void RequestIDR() {
    if (g_driver_provider.hmd && g_driver_provider.hmd->m_encoder) {
        g_driver_provider.hmd->m_encoder->InsertIDR();
    }
}

void SetTracking(unsigned long long targetTimestampNs,
                 float controllerPoseTimeOffsetS,
                 const FfiDeviceMotion *deviceMotions,
                 int motionsCount,
                 const FfiHandSkeleton *leftHand,
                 const FfiHandSkeleton *rightHand,
                 unsigned int controllersTracked,
                 const FfiEyeGaze *leftEyeGaze,
                 const FfiEyeGaze *rightEyeGaze,
                 const FfiEyeGaze *globalLeftGaze,
                 const FfiEyeGaze *globalRightGaze) {
    for (int i = 0; i < motionsCount; i++) {
        if (deviceMotions[i].deviceID == HEAD_ID && g_driver_provider.hmd) {
            g_driver_provider.hmd->OnPoseUpdated(targetTimestampNs, deviceMotions[i],leftEyeGaze[i],rightEyeGaze[i] ,globalLeftGaze[i] ,globalRightGaze[i]);

        } else {
            if (g_driver_provider.left_controller && deviceMotions[i].deviceID == LEFT_HAND_ID) {
                g_driver_provider.left_controller->onPoseUpdate(
                    controllerPoseTimeOffsetS, deviceMotions[i], leftHand, controllersTracked);
            } else if (g_driver_provider.right_controller &&
                       deviceMotions[i].deviceID == RIGHT_HAND_ID) {
                g_driver_provider.right_controller->onPoseUpdate(
                    controllerPoseTimeOffsetS, deviceMotions[i], rightHand, controllersTracked);
            }
        }
    } 

}


void VideoErrorReportReceive() {
    if (g_driver_provider.hmd) {
        g_driver_provider.hmd->m_encoder->OnPacketLoss();
    }
}

void ShutdownSteamvr() {
    if (g_driver_provider.hmd) {
        vr::VRServerDriverHost()->VendorSpecificEvent(
            g_driver_provider.hmd->object_id, vr::VREvent_DriverRequestedQuit, {}, 0);
    }
}

void SetOpenvrProperty(unsigned long long deviceID, FfiOpenvrProperty prop) {
    auto device_it = g_driver_provider.tracked_devices.find(deviceID);

    if (device_it != g_driver_provider.tracked_devices.end()) {
        device_it->second->set_prop(prop);
    }
}

void SetViewsConfig(FfiViewsConfig config) {
    if (g_driver_provider.hmd) {
        g_driver_provider.hmd->SetViewsConfig(config);
    }
}

void SetBattery(unsigned long long deviceID, float gauge_value, bool is_plugged) {
    auto device_it = g_driver_provider.tracked_devices.find(deviceID);

    if (device_it != g_driver_provider.tracked_devices.end()) {
        vr::VRProperties()->SetFloatProperty(
            device_it->second->prop_container, vr::Prop_DeviceBatteryPercentage_Float, gauge_value);
        vr::VRProperties()->SetBoolProperty(
            device_it->second->prop_container, vr::Prop_DeviceIsCharging_Bool, is_plugged);
    }
}

void SetButton(unsigned long long path, FfiButtonValue value) {
    if (g_driver_provider.left_controller &&
        std::find(LEFT_CONTROLLER_BUTTON_IDS.begin(), LEFT_CONTROLLER_BUTTON_IDS.end(), path) !=
            LEFT_CONTROLLER_BUTTON_IDS.end()) {
        g_driver_provider.left_controller->SetButton(path, value);
    } else if (g_driver_provider.right_controller &&
               std::find(RIGHT_CONTROLLER_BUTTON_IDS.begin(),
                         RIGHT_CONTROLLER_BUTTON_IDS.end(),
                         path) != RIGHT_CONTROLLER_BUTTON_IDS.end()) {
        g_driver_provider.right_controller->SetButton(path, value);
    }
}

void CaptureFrame() {
#ifndef __APPLE__
    if (g_driver_provider.hmd && g_driver_provider.hmd->m_encoder) {
        g_driver_provider.hmd->m_encoder->CaptureFrame();
    }
#endif
}


void AllQpChange( int delatqp) {
    Settings::Instance().m_delatQPmode += delatqp;
        if (Settings::Instance().m_delatQPmode < 0)
        {
        Settings::Instance().m_delatQPmode = 0 ;
        }
        else if (Settings::Instance().m_delatQPmode >30)
        {
            Settings::Instance().m_delatQPmode = 30;
        }
    }

void ROIQpChange( int Strategynum) {

    Settings::Instance().m_RoiQpStraetgy += Strategynum;
    if (Settings::Instance().m_RoiQpStraetgy < 1)
    {
        Settings::Instance().m_RoiQpStraetgy= 1 ;
    }
    else if (Settings::Instance().m_RoiQpStraetgy > 16)
    {
        Settings::Instance().m_RoiQpStraetgy = 16;
    }

    switch (Settings::Instance().m_RoiQpStraetgy)
    {
    case 1:
        Settings::Instance().m_delatRoiQP = 30;
        break;
    case 2:
        Settings::Instance().m_delatRoiQP = 0;
        break;
    case 3:
        Settings::Instance().m_delatRoiQP = 28; //Qp =23
         break;
    case 4:
        Settings::Instance().m_delatRoiQP = 20; // Qp = 31
         break;
    case 5:
        Settings::Instance().m_delatRoiQP = 8;  //Qp =43
         break;
    case 6:
        Settings::Instance().m_delatRoiQP = 22; //Qp = 29
         break;
    case 7:
        Settings::Instance().m_delatRoiQP = 10; //Qp = 41
         break;
    case 8:
        Settings::Instance().m_delatRoiQP = 18; //Qp = 33
         break;
    case 9:
        Settings::Instance().m_delatRoiQP = 6; //Qp = 45
         break;
    case 10:
        Settings::Instance().m_delatRoiQP = 14; //Qp = 37
         break;
    case 11:
        Settings::Instance().m_delatRoiQP = 26; //Qp = 25 
         break;
    case 12:
        Settings::Instance().m_delatRoiQP = 12;  //Qp =39
         break;
    case 13:
        Settings::Instance().m_delatRoiQP = 4 ; //Qp = 47
         break;
    case 14:
        Settings::Instance().m_delatRoiQP = 24;// Qp = 27
         break;
    case 15:
        Settings::Instance().m_delatRoiQP = 16; //Qp = 35
         break;
    case 16:
        Settings::Instance().m_delatRoiQP = 2; //Qp = 49
         break;
    default:
        Settings::Instance().m_delatRoiQP = 30;
        break;
    }
   
    Info("RoiQpStraetgy:%d , ROI QP = %d\n",Settings::Instance().m_RoiQpStraetgy , (51-Settings::Instance().m_delatRoiQP));
    }

struct TestList
{
   int RoiQp ;
   int strategy ;
};


TestList m_Testlist[3][20] = {
        {
            {37, 0}, {23, 5}, {40, 0}, {31, 3}, {46, 0},
            {28, 0}, {31, 5}, {31, 0}, {23, 4}, {31, 2},
            {23, 2}, {23, 1}, {34, 0}, {23, 3}, {23, 0},
            {49, 0}, {43, 0}, {31, 4}, {25, 0}, {31, 1}
        },
        {
            {23, 3}, {28, 0}, {23, 2}, {37, 0}, {31, 0},
            {23, 5}, {31, 5}, {40, 0}, {23, 1}, {49, 0},
            {31, 1}, {46, 0}, {31, 4}, {31, 2}, {25, 0},
            {31, 3}, {23, 4}, {23, 0}, {34, 0}, {43, 0}
        },
        {
            {23, 5}, {31, 0}, {23, 2}, {31, 5}, {37, 0},
            {23, 3}, {31, 4}, {23, 4}, {34, 0}, {46, 0},
            {31, 3}, {40, 0}, {31, 2}, {23, 0}, {28, 0},
            {49, 0}, {31, 1}, {23, 1}, {25, 0}, {43, 0}
        }
    };


void TestSequence( int DelatTestList , int DelatTestNum) {

    Settings::Instance().m_testlist += DelatTestList;
    Settings::Instance().m_testnum += DelatTestNum;
    if (Settings::Instance().m_testlist < 0)
    {
        Settings::Instance().m_testlist = 0 ;
    }
    else if (Settings::Instance().m_testlist > 2)
    {
        Settings::Instance().m_testlist = 2;
    }

    if (Settings::Instance().m_testnum < 0)
    {
        Settings::Instance().m_testnum = 0 ;
    }
    else if (Settings::Instance().m_testnum > 19)
    {
        Settings::Instance().m_testnum = 19;
    }

    strategynum = m_Testlist[Settings::Instance().m_testlist][Settings::Instance().m_testnum].strategy;
    Settings::Instance().m_delatRoiQP = 51- m_Testlist[Settings::Instance().m_testlist][Settings::Instance().m_testnum].RoiQp;
    ControlInfoSend(gaussionblurflag,strategynum,roiradius,false,eyespeedt);
    Info("List%d Num%d ROI Qp = %d Stratey=%d",Settings::Instance().m_testlist
    ,Settings::Instance().m_testnum 
    , (51 -Settings::Instance().m_delatRoiQP )
    ,strategynum );

}
void HQRSizeset( int delathqr) {
    Settings::Instance().m_RoiSize += delathqr ;

        if (Settings::Instance().m_RoiSize < 0)
        {
            Settings::Instance().m_RoiSize = 0 ;
        }
        if (Settings::Instance().m_RoiSize  >39)
        {
            Settings::Instance().m_RoiSize = 39;
        }
}
void CentrSizeset( int delatroi) {
    Settings::Instance().m_centresize += delatroi ;
        if (Settings::Instance().m_centresize < 0)
        {
            Settings::Instance().m_centresize = 0 ;
        }
        if ( Settings::Instance().m_centresize  >39)
        {
            Settings::Instance().m_centresize = 39;
        }
}
void COF0set( float delatcof0) {
   Settings::Instance().m_cof0 +=delatcof0;
   Info("Cof0 = %lf\n", Settings::Instance().m_cof0);
}
void COF1set( float delatcof1) {
    Settings::Instance().m_cof1 +=delatcof1;
   Info("Cof1 = %lf\n", Settings::Instance().m_cof1);
}

void QPDistribution() {
    #ifndef __APPLE__
    if (g_driver_provider.hmd && g_driver_provider.hmd->m_encoder) {
        g_driver_provider.hmd->m_encoder->QPDistribution();
    }
#endif
}
void RecordGaze(){
    #ifndef __APPLE__
    Settings::Instance().m_recordGaze = true;
#endif
}
void StopRecordGaze(){
    #ifndef __APPLE__
    Settings::Instance().m_recordGaze = false;
#endif
}
void MaxQpSet( int delatMaxqp){
    Settings::Instance().m_MaxQp += delatMaxqp;
    if ( Settings::Instance().m_MaxQp < 20)
    {
    Settings::Instance().m_MaxQp = 21;
    }
    else if ( Settings::Instance().m_MaxQp > 51)
    {
    Settings::Instance().m_MaxQp = 51;
    }    
    Info("MaxQp:%d",Settings::Instance().m_MaxQp);
}

void TDmode(){
    #ifndef __APPLE__
    Settings::Instance().m_tdmode=!Settings::Instance().m_tdmode;
    Info("TDmode:%d",Settings::Instance().m_tdmode);
#endif
}

void SpeedThresholdadd(){
    #ifndef __APPLE__
    Settings::Instance().m_speedthreshold=Settings::Instance().m_speedthreshold+100;
#endif
}

void SpeedThresholdsub(){
    #ifndef __APPLE__
    Settings::Instance().m_speedthreshold=Settings::Instance().m_speedthreshold-100;
#endif
}


void GazeVisual(){
   // Window  Nivada GPU 
   Settings::Instance().m_gazevisual = !Settings::Instance().m_gazevisual;
}

void LogLatency(const char *stringPtr, ...) {

    if (false)
    {
        //TxtLatency(stringPtr);
    }
}

void CloseTxtFile(){

    LogFileClose();
}

void UpdateGaussionStrategy(int delatnum){
   
   strategynum = strategynum + delatnum;
   if (strategynum < 0)
   {
      strategynum =0;
   }
   if  (strategynum >11)
   {
      strategynum =11;
   }
   
   ControlInfoSend(gaussionblurflag,strategynum,roiradius,false,eyespeedt);
}

void UpdateGaussionRoiSize(float RoiSizeRad){
    
    roiradius = roiradius +RoiSizeRad;
    if (roiradius <0)
    {
       roiradius = 0;
    }
    if (roiradius > 0.2469)
    {
        roiradius = 0.2469;
    }
    ControlInfoSend(gaussionblurflag,strategynum,roiradius,false,eyespeedt);
}

void GaussionEnable(){
   gaussionblurflag = !gaussionblurflag ;
   ControlInfoSend(gaussionblurflag,strategynum,roiradius,false,eyespeedt);
}
void ClientCapture(){
    ControlInfoSend(gaussionblurflag,strategynum,roiradius,true,eyespeedt);
}

void FPSReduce(){
    Settings::Instance().m_fpsReduce += 1 ;
    Settings::Instance().m_fpsReduce =Settings::Instance().m_fpsReduce % 6;
    if (Settings::Instance().m_fpsReduce < 1)
    {
        Settings::Instance().m_fpsReduce = 1 ;
    }

 

    Info("1/ FPSReduce = %d\n", Settings::Instance().m_fpsReduce);
}


void HmdPoseOffset(const FfiPose *poseoffset ,bool positionlock, bool roationlock){
    Settings::Instance().m_poseoffset = *poseoffset;
    Settings::Instance().m_enable_lockpositon = positionlock;
    Settings::Instance().m_enable_lockrotation = roationlock;
}
void EyeMovementModeSet(float t)
{
    eyespeedt=t;
    ControlInfoSend(gaussionblurflag,strategynum,roiradius,false,eyespeedt);
}