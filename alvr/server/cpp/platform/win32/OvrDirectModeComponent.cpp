#include "OvrDirectModeComponent.h"

OvrDirectModeComponent::OvrDirectModeComponent(std::shared_ptr<CD3DRender> pD3DRender, std::shared_ptr<PoseHistory> poseHistory)
	: m_pD3DRender(pD3DRender)
	, m_poseHistory(poseHistory)
	, m_submitLayer(0)
{
}

void OvrDirectModeComponent::SetEncoder(std::shared_ptr<CEncoder> pEncoder) {
	m_pEncoder = pEncoder;
}

/** Specific to Oculus compositor support, textures supplied must be created using this method. */
void OvrDirectModeComponent::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t *pSwapTextureSetDesc, SwapTextureSet_t *pOutSwapTextureSet)
{
	Debug("CreateSwapTextureSet pid=%d Format=%d %dx%d SampleCount=%d\n", unPid, pSwapTextureSetDesc->nFormat
		, pSwapTextureSetDesc->nWidth, pSwapTextureSetDesc->nHeight, pSwapTextureSetDesc->nSampleCount);

	//HRESULT hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_HARDWARE, NULL, creationFlags, NULL, 0, D3D11_SDK_VERSION, &pDevice, &eFeatureLevel, &pContext);

	D3D11_TEXTURE2D_DESC SharedTextureDesc = {};
	SharedTextureDesc.ArraySize = 1;
	SharedTextureDesc.MipLevels = 1;
	SharedTextureDesc.SampleDesc.Count = pSwapTextureSetDesc->nSampleCount;
	SharedTextureDesc.SampleDesc.Quality = 0;
	SharedTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	SharedTextureDesc.Format = (DXGI_FORMAT)pSwapTextureSetDesc->nFormat;

	// Some(or all?) applications request larger texture than we specified in GetRecommendedRenderTargetSize.
	// But, we must create textures in requested size to prevent cropped output. And then we must shrink texture to H.264 movie size.
	SharedTextureDesc.Width = pSwapTextureSetDesc->nWidth;
	SharedTextureDesc.Height = pSwapTextureSetDesc->nHeight;

	SharedTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	//SharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	SharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	ProcessResource *processResource = new ProcessResource();
	processResource->pid = unPid;

	for (int i = 0; i < 3; i++) {
		HRESULT hr = m_pD3DRender->GetDevice()->CreateTexture2D(&SharedTextureDesc, NULL, &processResource->textures[i]);
		//LogDriver("texture%d %p res:%d %s", i, texture[i], hr, GetDxErrorStr(hr).c_str());

		IDXGIResource* pResource;
		hr = processResource->textures[i]->QueryInterface(__uuidof(IDXGIResource), (void**)&pResource);
		//LogDriver("QueryInterface %p res:%d %s", pResource, hr, GetDxErrorStr(hr).c_str());

		hr = pResource->GetSharedHandle(&processResource->sharedHandles[i]);
		//LogDriver("GetSharedHandle %p res:%d %s", processResource->sharedHandles[i], hr, GetDxErrorStr(hr).c_str());

		m_handleMap.insert(std::make_pair(processResource->sharedHandles[i], std::make_pair(processResource, i)));

		pOutSwapTextureSet->rSharedTextureHandles[i] = (vr::SharedTextureHandle_t)processResource->sharedHandles[i];

		pResource->Release();

		Debug("Created Texture %d %p\n", i, processResource->sharedHandles[i]);
	}
	//m_processMap.insert(std::pair<uint32_t, ProcessResource *>(unPid, processResource));
}

/** Used to textures created using CreateSwapTextureSet.  Only one of the set's handles needs to be used to destroy the entire set. */
void OvrDirectModeComponent::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
{
	Debug("DestroySwapTextureSet %p\n", sharedTextureHandle);

	auto it = m_handleMap.find((HANDLE)sharedTextureHandle);
	if (it != m_handleMap.end()) {
		// Release all reference (a bit forcible)
		ProcessResource *p = it->second.first;
		m_handleMap.erase(p->sharedHandles[0]);
		m_handleMap.erase(p->sharedHandles[1]);
		m_handleMap.erase(p->sharedHandles[2]);
		delete p;
	}
	else {
		Debug("Requested to destroy not managing texture. handle:%p\n", sharedTextureHandle);
	}
}

/** Used to purge all texture sets for a given process. */
void OvrDirectModeComponent::DestroyAllSwapTextureSets(uint32_t unPid)
{
	Debug("DestroyAllSwapTextureSets pid=%d\n", unPid);

	for (auto it = m_handleMap.begin(); it != m_handleMap.end();) {
		if (it->second.first->pid == unPid) {
			if (it->second.second == 0) {
				delete it->second.first;
			}
			m_handleMap.erase(it++);
		}
		else {
			++it;
		}
	}
}

/** After Present returns, calls this to get the next index to use for rendering. */
void OvrDirectModeComponent::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t(*pIndices)[2])
{
	(*pIndices)[0]++;
	(*pIndices)[0] %= 3;
	(*pIndices)[1]++;
	(*pIndices)[1] %= 3;
}

/** Call once per layer to draw for this frame.  One shared texture handle per eye.  Textures must be created
* using CreateSwapTextureSet and should be alternated per frame.  Call Present once all layers have been submitted. */
void OvrDirectModeComponent::SubmitLayer(const SubmitLayerPerEye_t(&perEye)[2])
{
	auto pPose = &perEye[0].mHmdPose; // TODO: are both poses the same? Name HMD suggests yes.
    auto m_LeftProjectMat = &perEye[0].mProjection;
    auto m_RightProjectMat = &perEye[1].mProjection;
	
	if (m_submitLayer == 0) {
		// Detect FrameIndex of submitted frame by pPose.
		// This is important part to achieve smooth headtracking.
		// We search for history of TrackingInfo and find the TrackingInfo which have nearest matrix value.
            
		auto pose = m_poseHistory->GetBestPoseMatch(*pPose);
		if (pose) {
			// found the frameIndex
			m_prevTargetTimestampNs = m_targetTimestampNs;
			m_targetTimestampNs = pose->targetTimestampNs;

            //head Poses 
			m_prevFramePoseRotation = m_framePoseRotation;
			m_framePoseRotation.x = pose->motion.orientation.x;
			m_framePoseRotation.y = pose->motion.orientation.y;
			m_framePoseRotation.z = pose->motion.orientation.z;
			m_framePoseRotation.w = pose->motion.orientation.w;
			if (Settings::Instance().m_capturePicture)
			{
			TxtPrint("%llu %lf %lf %lf %lf %lf %lf %lf\n"
			,m_targetTimestampNs
			,pose->motion.position[0]
			,pose->motion.position[1]
			,pose->motion.position[2]
			,pose->motion.orientation.x
			,pose->motion.orientation.y
			,pose->motion.orientation.z
			,pose->motion.orientation.w
			);
			}
			

			// m_GazeOffset[0] = pose->NormalGazeOffset[0];
			// m_GazeOffset[1] = pose->NormalGazeOffset[1];
			// Gaze Pose.Orientation
			m_preGazeQuat[0] = m_GazeQuat[0];
			m_preGazeQuat[1] = m_GazeQuat[1];
			m_GazeQuat[0] = pose->GazeQuat[0];
			m_GazeQuat[1] = pose->GazeQuat[1];
            // Global  Gaze Pose.Orientation
			m_preGlobalQuat[0] = m_GlobalQuat[0];
			m_preGlobalQuat[1] = m_GlobalQuat[1];
			m_GlobalQuat[0] = pose->GloabGazeQuat[0];
			m_GlobalQuat[1] = pose->GloabGazeQuat[1];

            Calculate();

			//  Quat to Vector , Vector to angule,center_offset
			if (true)   // Quat to Vector , Vector to angule,center_offset
     		   {   
       			vr::HmdQuaternion_t LeftGazeQuat = HmdQuaternion_Init(
       			 m_GazeQuat[0].w,
       			 m_GazeQuat[0].x,
       			 m_GazeQuat[0].y,
       			 m_GazeQuat[0].z
       			 );
       			 vr::HmdQuaternion_t RightGazeQuat = HmdQuaternion_Init(
       			 m_GazeQuat[1].w, 
       			 m_GazeQuat[1].x, 
       			 m_GazeQuat[1].y, 
       			 m_GazeQuat[1].z 
       			 );
       			 vr::HmdVector3d_t ZAix = {0.0, 0.0, -1.0};//ZAix is (0,0,1)or(0,0,-1)
      			 vr::HmdVector3d_t LeftGazeVector;
       			 vr::HmdVector3d_t RightGazeVector;
      			  if (!m_GazeQuat[0].w || !m_GazeQuat[1].w)  //when eye gaze is null w =0 , so  Gaze is center
       				 {
           			  LeftGazeVector  = ZAix;
           			  RightGazeVector = ZAix;
       				 }
       			 else
      				  {
           	 		 LeftGazeVector  = vrmath::quaternionRotateVector(LeftGazeQuat,ZAix,false);
             		 RightGazeVector = vrmath::quaternionRotateVector(RightGazeQuat,ZAix,false);
      				  }
        			 float LeftGazeRad_X = atanf(-1.0*LeftGazeVector.v[0]/LeftGazeVector.v[2]); 
        			 float LeftGazeRad_Y = atanf(-1.0*LeftGazeVector.v[1]/LeftGazeVector.v[2]);
         			 float RightGazeRad_X = atanf(-1.0*RightGazeVector.v[0]/RightGazeVector.v[2]);
        			 float RightGazeRad_Y = atanf(-1.0*RightGazeVector.v[1]/RightGazeVector.v[2]);
        			 float RadToAnglue = (180.0/3.14159265358979323846f);
					 // need to change if fov is change
                     FfiFov leftcfov ={ -0.942478,0.698132,0.733038,-0.942478};
					 FfiFov rightcfov = { -0.698132,0.942478,0.733038,-0.942478};

        			 m_GazeOffset[0].x = 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left));
        			 m_GazeOffset[0].y = 1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up));
        			 m_GazeOffset[1].x = 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left));
        			 m_GazeOffset[1].y = 1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up));

        			if (m_GazeOffset[0].x<= 0 || m_GazeOffset[0].y<=0 || m_GazeOffset[1].x<=0 || m_GazeOffset[1].y <= 0)
      				  {
         			   Info("Error:calculate GazeOffset in DirectX11 Screen CoorDinate \n"); }       
    }

		}
		else {
			m_targetTimestampNs = 0;
			m_framePoseRotation = HmdQuaternion_Init(0.0, 0.0, 0.0, 0.0);
		}
	}
	if (m_submitLayer < MAX_LAYERS) {
		m_submitLayers[m_submitLayer][1] = perEye[1];
		m_submitLayers[m_submitLayer][0] = perEye[0];
		m_submitLayer++;
	}
	else {
		Warn("Too many layers submitted!\n");
	}

	//CopyTexture();
}

/** Submits queued layers for display. */
void OvrDirectModeComponent::Present(vr::SharedTextureHandle_t syncTexture)
{
	ReportPresent(m_targetTimestampNs, 0);

	bool useMutex = true;

	IDXGIKeyedMutex *pKeyedMutex = NULL;

	uint32_t layerCount = m_submitLayer;
	m_submitLayer = 0;

	if (m_prevTargetTimestampNs == m_targetTimestampNs) {
		Debug("Discard duplicated frame. FrameIndex=%llu (Ignoring)\n", m_targetTimestampNs);
		//return;
	}

	ID3D11Texture2D *pSyncTexture = m_pD3DRender->GetSharedTexture((HANDLE)syncTexture);
	if (!pSyncTexture)
	{
		Warn("[VDispDvr] SyncTexture is NULL!\n");
		return;
	}

	if (useMutex) {
		// Access to shared texture must be wrapped in AcquireSync/ReleaseSync
		// to ensure the compositor has finished rendering to it before it gets used.
		// This enforces scheduling of work on the gpu between processes.
		if (SUCCEEDED(pSyncTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&pKeyedMutex)))
		{
			// TODO: Reasonable timeout and timeout handling
			HRESULT hr = pKeyedMutex->AcquireSync(0, 10);
			if (hr != S_OK)
			{
				Debug("[VDispDvr] ACQUIRESYNC FAILED!!! hr=%d %p %ls\n", hr, hr, GetErrorStr(hr).c_str());
				pKeyedMutex->Release();
				return;
			}
		}
	}

	CopyTexture(layerCount);

	if (useMutex) {
		if (pKeyedMutex)
		{
			pKeyedMutex->ReleaseSync(0);
			pKeyedMutex->Release();
		}
	}

	ReportComposed(m_targetTimestampNs, 0);

	if (m_pEncoder) {
		m_pEncoder->NewFrameReady();
	}
}

void OvrDirectModeComponent::CopyTexture(uint32_t layerCount) {

	uint64_t presentationTime = GetTimestampUs();

	ID3D11Texture2D *pTexture[MAX_LAYERS][2];
	ComPtr<ID3D11Texture2D> Texture[MAX_LAYERS][2];
	vr::VRTextureBounds_t bounds[MAX_LAYERS][2];

	for (uint32_t i = 0; i < layerCount; i++) {
		// Find left eye texture.
		HANDLE leftEyeTexture = (HANDLE)m_submitLayers[i][0].hTexture;
		auto it = m_handleMap.find(leftEyeTexture);
		if (it == m_handleMap.end()) {
			// Ignore this layer.
			Debug("Submitted texture is not found on HandleMap. eye=right layer=%d/%d Texture Handle=%p\n", i, layerCount, leftEyeTexture);
		}
		else {
			Texture[i][0] = it->second.first->textures[it->second.second];
			D3D11_TEXTURE2D_DESC desc;
			Texture[i][0]->GetDesc(&desc);

			// Find right eye texture.
			HANDLE rightEyeTexture = (HANDLE)m_submitLayers[i][1].hTexture;
			it = m_handleMap.find(rightEyeTexture);
			if (it == m_handleMap.end()) {
				// Ignore this layer
				Debug("Submitted texture is not found on HandleMap. eye=left layer=%d/%d Texture Handle=%p\n", i, layerCount, rightEyeTexture);
				Texture[i][0].Reset();
			}
			else {
				Texture[i][1] = it->second.first->textures[it->second.second];
			}
		}

		pTexture[i][0] = Texture[i][0].Get();
		pTexture[i][1] = Texture[i][1].Get();
		bounds[i][0] = m_submitLayers[i][0].bounds;
		bounds[i][1] = m_submitLayers[i][1].bounds;
	}

	// This can go away, but is useful to see it as a separate packet on the gpu in traces.
	m_pD3DRender->GetContext()->Flush();

	if (m_pEncoder) {
		// Wait for the encoder to be ready.  This is important because the encoder thread
		// blocks on transmit which uses our shared d3d context (which is not thread safe).
		m_pEncoder->WaitForEncode();

		std::string debugText;

		uint64_t submitFrameIndex = m_targetTimestampNs;
		//Info("Source File : OvrDirectModeComponent.cpp \n");
		
        if( m_GazeOffset[0].x == 0 || m_GazeOffset[1].x == 0 || abs(m_GazeOffset[0].x)>1 || abs(m_GazeOffset[0].y)>1 || abs(m_GazeOffset[1].x)>1 || abs(m_GazeOffset[1].y)>1 )
		{

		//   m_GazeOffset[0].x = m_GazeOffset[0].y =0.5;// screen center
		//   m_GazeOffset[1] = m_GazeOffset[0];
		  m_GazeOffset[0].x = 0.62125;
		  m_GazeOffset[1].x = 0.37874;
		  m_GazeOffset[0].y = 0.39547;
		  m_GazeOffset[1].y = 0.39547;
		  
		}
		// else{
		//   //Info("GazeOffset Send to Encoder:(%f,%f) (%f,%f)\n",m_GazeOffset[0].x, m_GazeOffset[0].y, m_GazeOffset[1].x, m_GazeOffset[1].y);
		// }
		// Copy entire texture to staging so we can read the pixels to send to remote device.
		m_pEncoder->CopyToStaging(pTexture, bounds, layerCount,false, presentationTime, submitFrameIndex,"", debugText, m_GazeOffset[0],m_GazeOffset[1]);

		m_pD3DRender->GetContext()->Flush();
	}
}

void QuatToEuler(float qx, float qy, float qz, float qw, float& yaw, float& pitch, float& roll) {
    
	// Normalize the quaternion
    double sinr_cosp = +2.0 * (qw * qx + qy * qz);
    double cosr_cosp = +1.0 - 2.0 * (qx * qx + qy * qy);
    roll = atan2(sinr_cosp, cosr_cosp)*(180/PI);

    // pitch (y-axis rotation)
    double sinp = +2.0 * (qw * qy - qz * qx);
    if (fabs(sinp) >= 1)
        pitch = copysign(PI / 2, sinp)* (180 / PI); // use 90 degrees if out of range
    else
        pitch = asin(sinp)*(180 / PI);

    // yaw (z-axis rotation)
    double siny_cosp = +2.0 * (qw * qz + qx * qy) ;
    double cosy_cosp = +1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = atan2(siny_cosp, cosy_cosp)* (180 / PI);
    //    return yaw;
}


void OvrDirectModeComponent::dEulert(){
	 double DelatTime =((m_targetTimestampNs-m_prevTargetTimestampNs)/1000000.0); // ms

	 double DelatHeadYaw = (m_headEuler.Yaw - m_preheadEuler.Yaw)*1000/DelatTime;
	 double DelatHeadPitch = (m_headEuler.Pitch - m_preheadEuler.Pitch)*1000/DelatTime;
	 double DelatHeadRoll = (m_headEuler.Roll - m_preheadEuler.Roll)*1000/DelatTime;

	 double DelatGazeYaw = (m_gazeEuler.Yaw - m_pregazeEuler.Yaw)*1000/DelatTime;
	 double DelatGazePitch = (m_gazeEuler.Pitch - m_pregazeEuler.Pitch)*1000/DelatTime;
	 double DelatGazeRoll = (m_gazeEuler.Roll - m_pregazeEuler.Roll)*1000/DelatTime;

	 double DelatEyeYaw = (m_eyeEuler.Yaw - m_preeyeEuler.Yaw)*1000/DelatTime;
	 double DelatEyePitch = (m_eyeEuler.Pitch - m_preeyeEuler.Pitch)*1000/DelatTime;
	 double DelatEyeRoll = (m_eyeEuler.Roll - m_preeyeEuler.Roll)*1000/DelatTime;

	 double whead = std::sqrt(DelatHeadYaw*DelatHeadYaw + DelatHeadPitch*DelatHeadPitch + DelatHeadRoll*DelatHeadRoll) ;
	 double wgaze = std::sqrt(DelatGazeYaw*DelatGazeYaw + DelatGazePitch*DelatGazePitch + DelatGazeRoll*DelatGazeRoll);
	 double weye = std::sqrt(DelatEyeYaw * DelatEyeYaw + DelatEyePitch*DelatEyePitch + DelatEyeRoll *DelatEyeRoll);
	 m_wspeed.w_head = whead;
	 m_wspeed.w_gaze = wgaze;
	 m_wspeed.w_eye = weye;

	//  double wheadY = cos(roll)

}

void QuatToAngle(float qx, float qy, float qz, float qw, float& yaw, float& pitch, float& roll)
{
  	 OVR::Quatf quaternion =OVR::Quatf(qx, qy, qz, qw);
     OVR::Vector3f direction = quaternion.Rotate(OVR::Vector3f(0.0f, 0.0f, -1.0f));
	 yaw = std::atan2(-direction.x, direction.z)*(180 / PI); //y Aisx
	 pitch = std::atan2(-direction.y,direction.z)*(180 / PI);
	 roll = std::atan2(-direction.y, direction.x)*(180 / PI);
}

void QuatToEuler2(float qx, float qy, float qz, float qw, float& yaw, float& pitch, float& roll)
{
    OVR::Quatf quaternion =OVR::Quatf(qx, qy, qz, qw);
	quaternion.GetYawPitchRoll(&yaw,&pitch,&roll);
	yaw = yaw *(180 / PI);
	pitch = pitch *(180 / PI);
	roll = roll  *(180 / PI);
}

void OvrDirectModeComponent::Calculate(){
				OVR::Quatf headrot = OVR::Quatf(m_framePoseRotation.x,m_framePoseRotation.y,
			m_framePoseRotation.z,m_framePoseRotation.w);

			OVR::Quatf local_leftgaze = OVR::Quatf(m_GazeQuat[0].x,m_GazeQuat[0].y,
			m_GazeQuat[0].z,m_GazeQuat[0].w );
			OVR::Quatf local_rightgaze = OVR::Quatf(m_GazeQuat[1].x,m_GazeQuat[1].y,
			m_GazeQuat[1].z,m_GazeQuat[1].w );
            OVR::Vector3f local_leftgazeVector = local_leftgaze.ToRotationVector();
            OVR::Vector3f local_rightgazeVector = local_rightgaze.ToRotationVector();
			OVR::Vector3f local_gazeVector = OVR::Vector3f(
				(local_leftgazeVector.x + local_rightgazeVector.x)/2,
				(local_leftgazeVector.y + local_rightgazeVector.y)/2,
				(local_leftgazeVector.z + local_rightgazeVector.z)/2
				 );
			OVR::Quatf local_gaze =OVR::Quatf::FromRotationVector (local_gazeVector);

			//OVR::Quatf eyerot =  (gazerot * headrot) ;
			//eyerot.Normalize(); //Normalize

			OVR::Quatf global_leftgaze =  OVR::Quatf(m_GlobalQuat[0].x,m_GlobalQuat[0].y, 
			m_GlobalQuat[0].z, m_GlobalQuat[0].w);
			OVR::Quatf global_rightgaze =  OVR::Quatf(m_GlobalQuat[1].x,m_GlobalQuat[1].y, 
			m_GlobalQuat[1].z, m_GlobalQuat[1].w);

			OVR::Vector3f global_leftgazeVector = global_leftgaze.ToRotationVector();
			OVR::Vector3f global_rightgazeVector = global_rightgaze.ToRotationVector();
			OVR::Vector3f global_gazeVector = OVR::Vector3f(
				(global_leftgazeVector.x + global_rightgazeVector.x)/2,
				(global_leftgazeVector.y + global_rightgazeVector.y)/2,
				(global_leftgazeVector.z + global_rightgazeVector.z)/2
			);
			OVR::Quatf global_gaze = OVR::Quatf::FromRotationVector(global_gazeVector);

			
			m_preheadEuler = m_headEuler; //old euler
			m_pregazeEuler = m_gazeEuler;
			m_preeyeEuler = m_eyeEuler;
            QuatToEuler2(headrot.x,headrot.y,headrot.z,headrot.w,m_headEuler.Yaw,m_headEuler.Pitch,m_headEuler.Roll);
			QuatToEuler2(local_gaze.x,local_gaze.y,local_gaze.z,local_gaze.w,m_gazeEuler.Yaw,m_gazeEuler.Pitch,m_gazeEuler.Roll);
			QuatToEuler2(global_gaze.x,global_gaze.y,global_gaze.z,global_gaze.w,m_eyeEuler.Yaw,m_eyeEuler.Pitch,m_eyeEuler.Roll);
			//TxtGaze("time: %llu hyaw %lf hpitch %lf hroll %lf\n",m_targetTimestampNs ,m_headEuler.Yaw,m_headEuler.Pitch ,m_headEuler.Roll);
			//Info("time: %llu yaw %lf pitch %lf roll %lf\n",m_targetTimestampNs ,m_headEuler.Yaw,m_headEuler.Pitch ,m_headEuler.Roll);
            dEulert();
			//Info("time: %llu wh %lf wg %lf we %lf \n",m_targetTimestampNs ,m_wspeed.w_head ,m_wspeed.w_gaze ,m_wspeed.w_eye);	
			if (Settings::Instance().m_gazevisual && Settings::Instance().m_recordGaze)
			{
			 Txtwspeed("time: %llu wh %lf wg %lf we %lf \n",m_targetTimestampNs ,m_wspeed.w_head ,m_wspeed.w_gaze ,m_wspeed.w_eye);
			}	
}