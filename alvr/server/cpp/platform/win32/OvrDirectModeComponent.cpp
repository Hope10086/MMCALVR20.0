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

//Compare the quaternions of the old and new globals
			bool bprint=false;

			if((m_GlobalQuat[0].w != pose->GloabGazeQuat[0].w)||
			(m_GlobalQuat[0].x != pose->GloabGazeQuat[0].x)||
			(m_GlobalQuat[0].y != pose->GloabGazeQuat[0].y)||
			(m_GlobalQuat[0].z != pose->GloabGazeQuat[0].z))   //The timestamp is updated only when the global quaternion is different
			{
				m_prevTargetTimestampNs_txt = m_targetTimestampNs_txt;
				m_targetTimestampNs_txt = pose->targetTimestampNs;
				//head Poses , Lower frequency
				m_prevFramePoseRotation = m_framePoseRotation;
				m_framePoseRotation.x = pose->motion.orientation.x;
			    m_framePoseRotation.y = pose->motion.orientation.y;
			    m_framePoseRotation.z = pose->motion.orientation.z;
			    m_framePoseRotation.w = pose->motion.orientation.w;
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
				bprint=true;
			}
			m_prevTargetTimestampNs = m_targetTimestampNs;
			m_targetTimestampNs = pose->targetTimestampNs;

//Txt pose			
			// if (Settings::Instance().m_capturePicture || Settings::Instance().m_recordGaze)
			// {
			// TxtPrint("%llu position %lf %lf %lf orientation %lf %lf %lf %lf\n"
			// ,m_targetTimestampNs
			// ,pose->motion.position[0]
			// ,pose->motion.position[1]
			// ,pose->motion.position[2]
			// ,pose->motion.orientation.x
			// ,pose->motion.orientation.y
			// ,pose->motion.orientation.z
			// ,pose->motion.orientation.w
			// );
			// }
			
			FfiGazeOPOffset LeftGazeDirection ,RightGazeDirection;
			//  Quat to Vector , Vector to angule,center_offset
     		GazeQuatToNDCLocation(m_GazeQuat[0],m_GazeQuat[1], &m_GazeOffset[0], &m_GazeOffset[1]);
        	if (m_GazeOffset[0].x<= 0 || m_GazeOffset[0].y<=0 || m_GazeOffset[1].x<=0 || m_GazeOffset[1].y <= 0)
      		{Info("Error:calculate GazeOffset in DirectX11 Screen CoorDinate \n"); 
			}  
			// Txt Delta Loaction   
			if (Settings::Instance().m_recordGaze)
			{
			int width  = Settings::Instance().m_renderWidth /2;
	        int height = Settings::Instance().m_renderHeight;
//local
				//local gaze  loaction 's delta 
				FfiGazeOPOffset preLocNDCLocat[2] , nowLocNDCLocat[2];
				nowLocNDCLocat[0] =   m_GazeOffset[0];
				nowLocNDCLocat[1] =   m_GazeOffset[1];
				double LeftLocalDirection ,RightLocalDirection;
				GazeQuatToNDCLocation(m_preGazeQuat[0] , m_preGazeQuat[1] ,&preLocNDCLocat[0],&preLocNDCLocat[1],&LeftLocalDirection,&RightLocalDirection);
				//Delta Quat
				FfiQuat LocalDelatQuat[2];
				FfiGazeOPOffset LLocalGazeLoactDel ,RLocalGazeLoactDel;
				LocalDelatQuat[0]=DelatQuatCal(m_preGazeQuat[0],m_GazeQuat[0]);
				LocalDelatQuat[1]=DelatQuatCal(m_preGazeQuat[1],m_GazeQuat[1]);
				GazeQuatToNDCLocation(LocalDelatQuat[0],LocalDelatQuat[1], &LLocalGazeLoactDel, &RLocalGazeLoactDel,&LeftLocalDirection,&RightLocalDirection);
				//Delta Quat
				FfiGazeOPOffset LLocGazeLoactDel = DeltaLocationCal(nowLocNDCLocat[0] ,preLocNDCLocat[0]);
				FfiGazeOPOffset RLocGazeLoactDel = DeltaLocationCal(nowLocNDCLocat[1] ,preLocNDCLocat[1]);
				FfiQuat GlobDelatQuat[2] , HmdDelatQuat;
//head
				FfiGazeOPOffset preHeadNDCLocat[2] , nowHeadNDCLocat[2];
				FfiQuat nowHeadQuat = QuatFmt(m_framePoseRotation);
				FfiQuat preHeadQuat = QuatFmt(m_prevFramePoseRotation);
                FfiGazeOPOffset HeadGazeLoactDel[2];
				double LeftheadDirection ,RightheadDirection;   //head angle
				HmdDelatQuat = DelatQuatCal(preHeadQuat, nowHeadQuat);
				GazeQuatToNDCLocation(HmdDelatQuat, HmdDelatQuat,&HeadGazeLoactDel[0],&HeadGazeLoactDel[1], &LeftheadDirection, &RightheadDirection);
                FfiGazeOPOffset LHeadGazeLoactDel = {(HeadGazeLoactDel[0].x - 0.62125) *width  ,(HeadGazeLoactDel[0].y - 0.39547)*height};
                FfiGazeOPOffset RHeadGazeLoactDel = {(HeadGazeLoactDel[1].x - 0.37874) *width  ,(HeadGazeLoactDel[1].y - 0.39547)*height};


//global
				GlobDelatQuat[0] = DelatQuatCal(m_preGlobalQuat[0],m_GlobalQuat[0]);
				GlobDelatQuat[1] = DelatQuatCal(m_preGlobalQuat[1],m_GlobalQuat[1]);
				FfiGazeOPOffset LGloGazeLoactDel ,RGloGazeLoactDel;
				double LeftGlobDirection ,RightGlobDirection;  //global angle
				GazeQuatToNDCLocation(GlobDelatQuat[0] , GlobDelatQuat[1], &LGloGazeLoactDel, &RGloGazeLoactDel ,&LeftGlobDirection, &RightGlobDirection);
				//LGloGazeLoactDel ={(LGloGazeLoactDel.x - 0.62125) *width  ,(LGloGazeLoactDel.y - 0.39547)*height};
				//RGloGazeLoactDel ={(RGloGazeLoactDel.x - 0.37874) *width  ,(RGloGazeLoactDel.y - 0.39547)*height};
				LGloGazeLoactDel={LHeadGazeLoactDel.x+LLocGazeLoactDel.x,LHeadGazeLoactDel.y+LLocGazeLoactDel.y};  //global = head + local
				RGloGazeLoactDel={RHeadGazeLoactDel.x+RLocGazeLoactDel.x,RHeadGazeLoactDel.y+RLocGazeLoactDel.y};

//pixel offset speed
                //Info("%llu",(m_targetTimestampNs-m_prevTargetTimestampNs));
				FfiGazeOPOffset Headspeed_XY={calspeed(LHeadGazeLoactDel.x),calspeed(LHeadGazeLoactDel.y)};
				double Headspeed=calspeed(sqrt(pow(LHeadGazeLoactDel.x,2) + pow(LHeadGazeLoactDel.y,2)));
				FfiGazeOPOffset Leftlocalspeed_XY={calspeed(LLocGazeLoactDel.x),calspeed(LLocGazeLoactDel.y)};
				double Leftlocalspeed = calspeed(sqrt(pow(LLocGazeLoactDel.x,2) + pow(LLocGazeLoactDel.y,2)));
				FfiGazeOPOffset Leftglobalspeed_XY={calspeed(LGloGazeLoactDel.x),calspeed(LGloGazeLoactDel.y)};
				double Leftglobalspeed = calspeed(sqrt(pow(LGloGazeLoactDel.x,2) + pow(LGloGazeLoactDel.y,2)));
				
//Angle speed
				double HeadAngSpeed_angle = calspeed(LeftheadDirection);
				double LeftLocalSpeed_angle = calspeed(LeftLocalDirection);
				double LeftGlobalSpeed_angle = calspeed(LeftGlobDirection);
// List 




//  Printf Txt  speed
			// if(bprint)
			// {
			// 	//SK
			// 	Info("%llu %llu",m_prevTargetTimestampNs_txt,m_targetTimestampNs_txt);
			// 	//SK
			// 	TxtDeltaLocat("%llu speed head %d %d %d Left: local %d %d %d global %d %d %d\n"
			// 	, m_targetTimestampNs
			// 	, int (Headspeed_XY.x)
			// 	, int (Headspeed_XY.y)
			// 	, int (Headspeed)
			// 	, int (Leftlocalspeed_XY.x)
			// 	, int (Leftlocalspeed_XY.y)
			// 	, int (Leftlocalspeed)
			// 	, int (Leftglobalspeed_XY.x)
			// 	, int (Leftglobalspeed_XY.y)
			// 	, int (Leftglobalspeed)
			// 	);
			// 	Txtwspeed("%llu Anglespeed: head %lf Left: local %lf global %lf \n"
			// 	,m_targetTimestampNs
			// 	,HeadAngSpeed_angle
			// 	,LeftLocalSpeed_angle
			// 	,LeftGlobalSpeed_angle	
			// 	);
			// }

// Printf  Txt  offset
            if(bprint)
			{
				TxtDeltaLocat("%llu variation head %d %d %d Left: local %d %d %d global %d %d %d\n"
				, m_targetTimestampNs
				, int (LHeadGazeLoactDel.x)
				, int (LHeadGazeLoactDel.y)
				, int (sqrt(pow(LHeadGazeLoactDel.x,2) + pow(LHeadGazeLoactDel.y,2)))
				, int (LLocGazeLoactDel.x)
				, int (LLocGazeLoactDel.y)
				, int (sqrt(pow(LLocGazeLoactDel.x,2) + pow(LLocGazeLoactDel.y,2)))
				, int (LGloGazeLoactDel.x)
				, int (LGloGazeLoactDel.y)
				, int (sqrt(pow(LGloGazeLoactDel.x,2) + pow(LGloGazeLoactDel.y,2)))
				);
				Txtwspeed("%llu Angle: head %lf Left: local %lf global %lf \n"
				,m_targetTimestampNs
				,LeftheadDirection
				,LeftLocalDirection
				,LeftGlobDirection	
				);

			TxtNDCGaze("%llu %lf %lf %lf %lf %lf %lf %lf %lf \n"
			,m_targetTimestampNs
			,m_GazeOffset[0].x
			,m_GazeOffset[0].y
			,m_GazeOffset[1].x+1
			,m_GazeOffset[1].y
			,m_GazeOffset[0].x*width
			,m_GazeOffset[0].y*height
			,(m_GazeOffset[1].x+1)*width
			,(m_GazeOffset[1].y)*height
			);
			
			TxtPrint("%llu position %lf %lf %lf orientation %lf %lf %lf %lf\n"
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

void OvrDirectModeComponent::PostPresent() {
	WaitForVSync();
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
		
		
		if (Settings::Instance().m_FrameRenderIndex %3 ==0)
		{
		m_pEncoder->CopyToStaging(pTexture, bounds, layerCount,false, presentationTime, submitFrameIndex,"", debugText, m_GazeOffset[0],m_GazeOffset[1], m_wspeed);	
		}
		

        Settings::Instance().m_FrameRenderIndex ++;
		m_pD3DRender->GetContext()->Flush();
	}
}

double OvrDirectModeComponent::calspeed(double offset)
{
	double speed=offset/((m_targetTimestampNs_txt-m_prevTargetTimestampNs_txt)/1000000000.0);  //The unit is /s 
	return speed;
}

void GazeQuatToNDCLocation( FfiQuat LGazeQuat , FfiQuat RGazeQuat ,FfiGazeOPOffset* LNDCLocat , FfiGazeOPOffset* RNDCLocat)
{
	       		vr::HmdQuaternion_t LeftGazeQuat = HmdQuaternion_Init(
                LGazeQuat.w,
				LGazeQuat.x,
				LGazeQuat.y,
				LGazeQuat.z
       			 );
       			 vr::HmdQuaternion_t RightGazeQuat = HmdQuaternion_Init(
       			 RGazeQuat.w, 
       			 RGazeQuat.x, 
       			 RGazeQuat.y, 
       			 RGazeQuat.z 
       			 );
       			 vr::HmdVector3d_t ZAix = {0.0, 0.0, -1.0};//ZAix is (0,0,1)or(0,0,-1)
      			 vr::HmdVector3d_t LeftGazeVector;
       			 vr::HmdVector3d_t RightGazeVector;
      			  if (!LGazeQuat.w || !RGazeQuat.w)  //when eye gaze is null w =0 , so  Gaze is center
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
                     FfiFov leftcfov  = { -0.942478,0.698132,0.733038,-0.942478};
					 FfiFov rightcfov = { -0.698132,0.942478,0.733038,-0.942478};

        			//  *LNDCLocat.x = 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left));
        			//  *LNDCLocat.y = 1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up));
        			 
					//  *RNDCLocat.x = 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left));
        			//  *RNDCLocat.y = 1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up));
				    if (LNDCLocat)
					{
						*LNDCLocat = 
						{
						 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left))
						,1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up))
						};
					}
					else
					Info("LNDCLocat in null\n");
					if (RNDCLocat)
					{
						*RNDCLocat = {
						 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left))        
					    ,1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up))
						};
					}
					else
					Info("RNDCLocat in null\n");
					
					

}


void GazeQuatToNDCLocation( FfiQuat LGazeQuat , FfiQuat RGazeQuat ,FfiGazeOPOffset* LNDCLocat , FfiGazeOPOffset* RNDCLocat , double *LGazeVector ,double *RGazeVector)
{
	       		vr::HmdQuaternion_t LeftGazeQuat = HmdQuaternion_Init(
                LGazeQuat.w,
				LGazeQuat.x,
				LGazeQuat.y,
				LGazeQuat.z
       			 );
       			 vr::HmdQuaternion_t RightGazeQuat = HmdQuaternion_Init(
       			 RGazeQuat.w, 
       			 RGazeQuat.x, 
       			 RGazeQuat.y, 
       			 RGazeQuat.z 
       			 );
       			 vr::HmdVector3d_t ZAix = {0.0, 0.0, -1};//ZAix is (0,0,1)or(0,0,-1)
      			 vr::HmdVector3d_t LeftGazeVector;
       			 vr::HmdVector3d_t RightGazeVector;
      			  if (!LGazeQuat.w || !RGazeQuat.w)  //when eye gaze is null w =0 , so  Gaze is center
       				 {
           			  LeftGazeVector  = ZAix;
           			  RightGazeVector = ZAix;
       				 }
       			 else
      				  {
           	 		 LeftGazeVector  = vrmath::quaternionRotateVector(LeftGazeQuat,ZAix,false);
             		 RightGazeVector = vrmath::quaternionRotateVector(RightGazeQuat,ZAix,false);
      				  }

				//Value limit
					if(RightGazeVector.v[2]<-1)
					{
						RightGazeVector.v[2]=-1;
					}
					if(LeftGazeVector.v[2]<-1)
					{
						LeftGazeVector.v[2]=-1;
					}


        			 double LeftGazeRad_X = atanf(-1.0*LeftGazeVector.v[0]/LeftGazeVector.v[2]); 
        			 double LeftGazeRad_Y = atanf(-1.0*LeftGazeVector.v[1]/LeftGazeVector.v[2]);
         			 double RightGazeRad_X = atanf(-1.0*RightGazeVector.v[0]/RightGazeVector.v[2]);
        			 double RightGazeRad_Y = atanf(-1.0*RightGazeVector.v[1]/RightGazeVector.v[2]);

					 double LeftGazeRad = acos(-1.0* LeftGazeVector.v[2]);
					 double RightGazeRad = acos( -1.0* RightGazeVector.v[2] );

        			 double RadToAnglue = (180.0/3.14159265358979323846f);
					 *LGazeVector = LeftGazeRad  *RadToAnglue ;    
					 *RGazeVector = RightGazeRad *RadToAnglue ;
					 // need to change if fov is change
                     FfiFov leftcfov  = { -0.942478,0.698132,0.733038,-0.942478};
					 FfiFov rightcfov = { -0.698132,0.942478,0.733038,-0.942478};

        			//  *LNDCLocat.x = 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left));
        			//  *LNDCLocat.y = 1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up));
        			 
					//  *RNDCLocat.x = 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left));
        			//  *RNDCLocat.y = 1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up));
				    if (LNDCLocat)
					{
						*LNDCLocat = 
						{
						 1.0*(tanf(LeftGazeRad_X)+tanf(-leftcfov.left))/(tanf(leftcfov.right)+tanf(-leftcfov.left))
						,1.0*(tanf(-LeftGazeRad_Y)+tanf(leftcfov.up))/(tanf(-leftcfov.down)+tanf(leftcfov.up))
						};
					}
					else
					Info("LNDCLocat in null\n");
					if (RNDCLocat)
					{
						*RNDCLocat = {
						 1.0*(tanf(RightGazeRad_X)+tanf(-rightcfov.left))/(tanf(rightcfov.right)+tanf(-rightcfov.left))        
					    ,1.0*(tanf(-RightGazeRad_Y)+tanf(rightcfov.up))/(tanf(-rightcfov.down)+tanf(rightcfov.up))
						};
					}
					else
					Info("RNDCLocat in null\n");
					
					

}

FfiGazeOPOffset DeltaLocationCal (FfiGazeOPOffset nowNDCLocat , FfiGazeOPOffset preNDCLocat)
{
	FfiGazeOPOffset DeltaLocat;
	int width = Settings::Instance().m_renderWidth /2;
	int height = Settings::Instance().m_renderHeight;
	DeltaLocat.x = width * nowNDCLocat.x - width * preNDCLocat.x;
	DeltaLocat.y = height * nowNDCLocat.y - height * preNDCLocat.y;

    return DeltaLocat;
}

FfiQuat QuatFmt( vr::HmdQuaternion_t  rawQuat)
{
   FfiQuat newQuat; 
   newQuat.w = rawQuat.w;
   newQuat.x = rawQuat.x;
   newQuat.y = rawQuat.y;
   newQuat.z = rawQuat.z;
   return newQuat;
}
FfiQuat DelatQuatCal( FfiQuat preQuat , FfiQuat nowQuat)
{
   OVR::Quatf ovrprequat = OVR::Quatf(preQuat.x ,preQuat.y ,preQuat.z ,preQuat.w);
   OVR::Quatf ovrnowquat = OVR::Quatf(nowQuat.x ,nowQuat.y ,nowQuat.z ,nowQuat.w);
   OVR::Quatf delatquat =  ovrprequat.Inverse() *ovrnowquat;

   FfiQuat DelatQuat = {delatquat.x ,delatquat.y ,delatquat.z ,delatquat.w };

   return DelatQuat;

}
