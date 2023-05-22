#include "HMD.h"

#include "Controller.h"
#include "Logger.h"
#include "Paths.h"
#include "PoseHistory.h"
#include "Settings.h"
#include "Utils.h"
#include "ViveTrackerProxy.h"
#include "bindings.h"
#include <cfloat>
#include "include/openvr_math.h"    

#ifdef _WIN32
#include "platform/win32/CEncoder.h"
#elif __APPLE__
#include "platform/macos/CEncoder.h"
#else
#include "platform/linux/CEncoder.h"
#endif

const vr::HmdMatrix34_t MATRIX_IDENTITY = {
    {{1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}}};


vr::HmdRect2_t fov_to_projection(FfiFov fov) {
    auto proj_bounds = vr::HmdRect2_t{};
    proj_bounds.vTopLeft.v[0] = tanf(fov.left);
    proj_bounds.vBottomRight.v[0] = tanf(fov.right);
    proj_bounds.vTopLeft.v[1] = tanf(fov.down);
    proj_bounds.vBottomRight.v[1] = tanf(fov.up);

    return proj_bounds;
}

Hmd::Hmd()
    : TrackedDevice(HEAD_ID), m_baseComponentsInitialized(false),
      m_streamComponentsInitialized(false) {
    auto dummy_fov = FfiFov{-1.0, 1.0, 1.0, -1.0};

    this->views_config = FfiViewsConfig{};
    this->views_config.ipd_m = 0.063;
    this->views_config.fov[0] = dummy_fov;
    this->views_config.fov[1] = dummy_fov;

    leftcfov  = { -0.942478,0.698132,0.733038,-0.942478};
    rightcfov = { -0.698132,0.942478,0.733038,-0.942478};

    m_pose = vr::DriverPose_t{};
    m_pose.poseIsValid = true;
    m_pose.result = vr::TrackingResult_Running_OK;
    m_pose.deviceIsConnected = true;
    m_pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
    m_pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);
    m_pose.qRotation = HmdQuaternion_Init(1, 0, 0, 0);

    m_poseHistory = std::make_shared<PoseHistory>();

    m_deviceClass = Settings::Instance().m_TrackingRefOnly
                        ? vr::TrackedDeviceClass_TrackingReference
                        : vr::TrackedDeviceClass_HMD;

    if (Settings::Instance().m_enableViveTrackerProxy) {
        m_viveTrackerProxy = std::make_unique<ViveTrackerProxy>(*this);
        if (!vr::VRServerDriverHost()->TrackedDeviceAdded(m_viveTrackerProxy->GetSerialNumber(),
                                                          vr::TrackedDeviceClass_GenericTracker,
                                                          m_viveTrackerProxy.get())) {
            Warn("Failed to register Vive tracker");
        }
    }

    Debug("CRemoteHmd successfully initialized.\n");
}

Hmd::~Hmd() {
    ShutdownRuntime();

    if (m_encoder) {
        Debug("Hmd::~Hmd(): Stopping encoder...\n");
        m_encoder->Stop();
        m_encoder.reset();
    }

#ifdef _WIN32
    if (m_D3DRender) {
        m_D3DRender->Shutdown();
        m_D3DRender.reset();
    }
#endif
}

vr::EVRInitError Hmd::Activate(vr::TrackedDeviceIndex_t unObjectId) {
    Debug("CRemoteHmd Activate %d\n", unObjectId);

    auto vr_properties = vr::VRProperties();

    this->object_id = unObjectId;
    this->prop_container = vr_properties->TrackedDeviceToPropertyContainer(this->object_id);

    SetOpenvrProps(this->device_id);

    vr_properties->SetFloatProperty(this->prop_container,
                                    vr::Prop_DisplayFrequency_Float,
                                    static_cast<float>(Settings::Instance().m_refreshRate));

    vr::VRDriverInput()->CreateBooleanComponent(this->prop_container, "/proximity", &m_proximity);

#ifdef _WIN32
    float originalIPD =
        vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float);
    vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float, 0.063);
#endif

    HmdMatrix_SetIdentity(&m_eyeToHeadLeft);
    HmdMatrix_SetIdentity(&m_eyeToHeadRight);

// Disable async reprojection on Linux. Windows interface uses IVRDriverDirectModeComponent
// which never applies reprojection
// Also Disable async reprojection on vulkan
#ifndef _WIN32
    vr::VRSettings()->SetBool(
        vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_DisableAsyncReprojection_Bool, true);
    vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section,
                              vr::k_pch_SteamVR_EnableLinuxVulkanAsync_Bool,
                              Settings::Instance().m_enableLinuxVulkanAsync);
#endif

    if (!m_baseComponentsInitialized) {
        m_baseComponentsInitialized = true;

        if (IsHMD()) {
#ifdef _WIN32
            m_D3DRender = std::make_shared<CD3DRender>();

            // Use the same adapter as vrcompositor uses. If another adapter is used, vrcompositor
            // says "failed to open shared texture" and then crashes. It seems vrcompositor selects
            // always(?) first adapter. vrcompositor may use Intel iGPU when user sets it as primary
            // adapter. I don't know what happens on laptop which support optimus.
            // Prop_GraphicsAdapterLuid_Uint64 is only for redirect display and is ignored on direct
            // mode driver. So we can't specify an adapter for vrcompositor. m_nAdapterIndex is set
            // 0 on the dashboard.
            if (!m_D3DRender->Initialize(Settings::Instance().m_nAdapterIndex)) {
                Error("Could not create graphics device for adapter %d.  Requires a minimum of two "
                      "graphics cards.\n",
                      Settings::Instance().m_nAdapterIndex);
                return vr::VRInitError_Driver_Failed;
            }

            int32_t nDisplayAdapterIndex;
            if (!m_D3DRender->GetAdapterInfo(&nDisplayAdapterIndex, m_adapterName)) {
                Error("Failed to get primary adapter info!\n");
                return vr::VRInitError_Driver_Failed;
            }

            Info("Using %ls as primary graphics adapter.\n", m_adapterName.c_str());
            Info("OSVer: %ls\n", GetWindowsOSVersion().c_str());

            m_directModeComponent =
                std::make_shared<OvrDirectModeComponent>(m_D3DRender, m_poseHistory);
#endif
        }

        DriverReadyIdle(IsHMD());
    }

    if (IsHMD()) {
        vr::VREvent_Data_t eventData;
        eventData.ipd = {0.063};
        vr::VRServerDriverHost()->VendorSpecificEvent(
            this->object_id, vr::VREvent_IpdChanged, eventData, 0);
    }

    return vr::VRInitError_None;
}

void Hmd::Deactivate() {
    this->object_id = vr::k_unTrackedDeviceIndexInvalid;
    this->prop_container = vr::k_ulInvalidPropertyContainer;
}

void *Hmd::GetComponent(const char *component_name_and_version) {
    // NB: "this" pointer needs to be statically cast to point to the correct vtable

    auto name_and_vers = std::string(component_name_and_version);
    if (name_and_vers == vr::IVRDisplayComponent_Version) {
        return (vr::IVRDisplayComponent *)this;
    }

#ifdef _WIN32
    if (name_and_vers == vr::IVRDriverDirectModeComponent_Version) {
        return m_directModeComponent.get();
    }
#endif

    return nullptr;
}

vr::DriverPose_t Hmd::GetPose() { return m_pose; }

void Hmd::OnPoseUpdated(uint64_t targetTimestampNs, FfiDeviceMotion motion, FfiEyeGaze LeftGaze,FfiEyeGaze RightGaze) {
    if (this->object_id == vr::k_unTrackedDeviceIndexInvalid) {
        return;
    }
    auto pose = vr::DriverPose_t{};
    pose.poseIsValid = true;
    pose.result = vr::TrackingResult_Running_OK;
    pose.deviceIsConnected = true;

    pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
    pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);

    pose.qRotation = HmdQuaternion_Init(
        motion.orientation.w, motion.orientation.x, motion.orientation.y, motion.orientation.z);

    pose.vecPosition[0] = motion.position[0];
    pose.vecPosition[1] = motion.position[1];
    pose.vecPosition[2] = motion.position[2];

    m_pose = pose;// don't change it
    
    if (false)  // check out eyegaze if correct
    {  Info("Source: HMD.cpp OnPoseUpdated\n");
       Info("targetTimestampNs %llu  \n",targetTimestampNs);
       Info("LeftEyeGaze: (%f %f %f),(%f %f %f %f)\n"
       ,LeftGaze.position[0]
       ,LeftGaze.position[1]
       ,LeftGaze.position[2]
       ,LeftGaze.orientation.w
       ,LeftGaze.orientation.x
       ,LeftGaze.orientation.y
       ,LeftGaze.orientation.z
       );
    }
   
    
    if (true)   // Quat to Vector , Vector to angule,center_offset
    {   
        FfiGazeOPOffset GazeOffset[2]; 
        vr::HmdQuaternion_t LeftGazeQuat = HmdQuaternion_Init(
        LeftGaze.orientation.w,
        LeftGaze.orientation.x,
        LeftGaze.orientation.y,
        LeftGaze.orientation.z
        );
        vr::HmdQuaternion_t RightGazeQuat = HmdQuaternion_Init(
        RightGaze.orientation.w,    
        RightGaze.orientation.x,    
        RightGaze.orientation.y,    
        RightGaze.orientation.z  
        );
        vr::HmdVector3d_t ZAix = {0.0, 0.0, -1.0};//ZAix is (0,0,1)or(0,0,-1)
        vr::HmdVector3d_t LeftGazeVector;
        vr::HmdVector3d_t RightGazeVector;
        if (!LeftGaze.orientation.w || !RightGaze.orientation.w)  //when eye gaze is null w =0 , so  Gaze is center
        {
             LeftGazeVector  = ZAix;
             RightGazeVector = ZAix;
        }
        else
        {
             LeftGazeVector  = vrmath::quaternionRotateVector(LeftGazeQuat,ZAix,false);
             RightGazeVector = vrmath::quaternionRotateVector(RightGazeQuat,ZAix,false);
        }
        // check out  LeftGazeVector  
        // Info("LeftGazeVector : (%f,%f,%f)\n"
        // ,LeftGazeVector.v[0]
        // ,LeftGazeVector.v[1]
        // ,LeftGazeVector.v[2]);
         //  to Rad and log Anglue
         float LeftGazeRad_X = atanf(-1.0*LeftGazeVector.v[0]/LeftGazeVector.v[2]); 
         float LeftGazeRad_Y = atanf(-1.0*LeftGazeVector.v[1]/LeftGazeVector.v[2]);
         float RightGazeRad_X = atanf(-1.0*RightGazeVector.v[0]/RightGazeVector.v[2]);
         float RightGazeRad_Y = atanf(-1.0*RightGazeVector.v[1]/RightGazeVector.v[2]);
         float RadToAnglue = (180.0/3.14159265358979323846f);
        //  Info(" Gaze Anglue in camera space\n (%lf,%lf)  (%lf,%lf)\n"
        // ,LeftGazeRad_X*RadToAnglue
        // ,LeftGazeRad_Y*RadToAnglue
        // ,RightGazeRad_X*RadToAnglue
        // ,RightGazeRad_Y*RadToAnglue
        //  );// the rad is useless

         //  just for Direct coordinate system x:right y down(*-1.0) z useless depthz =1.0
        // Info("leftfov up = ",leftcfov.up);//
         GazeOffset[0].x = 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left));
         GazeOffset[0].y = 1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up));

         GazeOffset[1].x = 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left));
         GazeOffset[1].y = 1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up));

        if (GazeOffset[0].x<= 0 || GazeOffset[0].y<=0 || GazeOffset[1].x<=0 || GazeOffset[1].y <= 0)
        {
            Info("Error:calculate GazeOffset in DirectX11 Screen CoorDinate \n");
            m_poseHistory->OnPoseUpdated(targetTimestampNs, motion,{0.5, 0.5},{0.5, 0.5});
        }
        else
        {
            //Info("GazeOffset= (%lf,%lf) (%lf,%lf)\n",GazeOffset[0].x, GazeOffset[0].y, GazeOffset[1].x, GazeOffset[1].y);
            m_poseHistory->OnPoseUpdated(targetTimestampNs, motion,GazeOffset[0],GazeOffset[1]);
        }
        
    }
    else
    {   // unenable offset =0
        m_poseHistory->OnPoseUpdated(targetTimestampNs, motion,{0.0, 0.0},{0.0, 0.0});
    }
    

  //  m_poseHistory->OnPoseUpdated(targetTimestampNs, motion);

    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
        this->object_id, pose, sizeof(vr::DriverPose_t));

    if (m_viveTrackerProxy)
        m_viveTrackerProxy->update();

#if !defined(_WIN32) && !defined(__APPLE__)
    // This has to be set after initialization is done, because something in vrcompositor is
    // setting it to 90Hz in the meantime
    if (!m_refreshRateSet && m_encoder && m_encoder->IsConnected()) {
        m_refreshRateSet = true;
        vr::VRProperties()->SetFloatProperty(
            this->prop_container,
            vr::Prop_DisplayFrequency_Float,
            static_cast<float>(Settings::Instance().m_refreshRate));
    }
#endif
}

void Hmd::StartStreaming() {
    vr::VRDriverInput()->UpdateBooleanComponent(m_proximity, true, 0.0);

    if (m_streamComponentsInitialized) {
        return;
    }

    // Spin up a separate thread to handle the overlapped encoding/transmit step.
    if (IsHMD()) {
#ifdef _WIN32
        m_encoder = std::make_shared<CEncoder>();
        try {
            m_encoder->Initialize(m_D3DRender);
        } catch (Exception e) {
            Error("Your GPU does not meet the requirements for video encoding. %s %s\n%s %s\n",
                  "If you get this error after changing some settings, you can revert them by",
                  "deleting the file \"session.json\" in the installation folder.",
                  "Failed to initialize CEncoder:",
                  e.what());
        }
        m_encoder->Start();

        m_directModeComponent->SetEncoder(m_encoder);

        m_encoder->OnStreamStart();
#elif __APPLE__
        m_encoder = std::make_shared<CEncoder>();
#else
        m_encoder = std::make_shared<CEncoder>(m_poseHistory);
        m_encoder->Start();
#endif
    }

    m_streamComponentsInitialized = true;
}

void Hmd::StopStreaming() { vr::VRDriverInput()->UpdateBooleanComponent(m_proximity, false, 0.0); }

void Hmd::SetViewsConfig(FfiViewsConfig config) {
    this->views_config = config;

    auto left_transform = MATRIX_IDENTITY;
    left_transform.m[0][3] = -config.ipd_m / 2.0;
    auto right_transform = MATRIX_IDENTITY;
    right_transform.m[0][3] = config.ipd_m / 2.0;
    vr::VRServerDriverHost()->SetDisplayEyeToHead(object_id, left_transform, right_transform);

    auto left_proj = fov_to_projection(config.fov[0]);
    auto right_proj = fov_to_projection(config.fov[1]);
    
    leftcfov  = config.fov[0];
    rightcfov = config.fov[1];
    Info("Decive LFov %f %f %f %f \n",leftcfov.left,leftcfov.right, leftcfov.up, leftcfov.down);
    Info("Decive RFov %f %f %f %f \n",rightcfov.left,rightcfov.right, rightcfov.up, rightcfov.down);

    vr::VRServerDriverHost()->SetDisplayProjectionRaw(object_id, left_proj, right_proj);

    // todo: check if this is still needed
    vr::VRServerDriverHost()->VendorSpecificEvent(
        object_id, vr::VREvent_LensDistortionChanged, {}, 0);
}

void Hmd::GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight) {
    Debug("GetWindowBounds %dx%d - %dx%d\n",
          0,
          0,
          Settings::Instance().m_renderWidth,
          Settings::Instance().m_renderHeight);
    *pnX = 0;
    *pnY = 0;
    *pnWidth = Settings::Instance().m_renderWidth;
    *pnHeight = Settings::Instance().m_renderHeight;
}

bool Hmd::IsDisplayRealDisplay() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

void Hmd::GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight) {
    *pnWidth = Settings::Instance().m_recommendedTargetWidth / 2;
    *pnHeight = Settings::Instance().m_recommendedTargetHeight;
    Debug("GetRecommendedRenderTargetSize %dx%d\n", *pnWidth, *pnHeight);
}

void Hmd::GetEyeOutputViewport(
    vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight) {
    *pnY = 0;
    *pnWidth = Settings::Instance().m_renderWidth / 2;
    *pnHeight = Settings::Instance().m_renderHeight;

    if (eEye == vr::Eye_Left) {
        *pnX = 0;
    } else {
        *pnX = Settings::Instance().m_renderWidth / 2;
    }
    Debug("GetEyeOutputViewport Eye=%d %dx%d %dx%d\n", eEye, *pnX, *pnY, *pnWidth, *pnHeight);
}

void Hmd::GetProjectionRaw(vr::EVREye eye, float *left, float *right, float *top, float *bottom) {
    auto proj = fov_to_projection(this->views_config.fov[eye]);
    *left = proj.vTopLeft.v[0];
    *right = proj.vBottomRight.v[0];
    *top = proj.vTopLeft.v[1];
    *bottom = proj.vBottomRight.v[1];
}

vr::DistortionCoordinates_t Hmd::ComputeDistortion(vr::EVREye, float u, float v) {
    return {{u, v}, {u, v}, {u, v}};
}
