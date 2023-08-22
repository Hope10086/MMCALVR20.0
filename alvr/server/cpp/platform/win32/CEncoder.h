#pragma once
#include "shared/d3drender.h"

#include "shared/threadtools.h"

#include <d3d11.h>
#include <wrl.h>
#include <map>
#include <d3d11_1.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include "alvr_server/Utils.h"
#include "FrameRender.h"
#include "VideoEncoder.h"
#include "VideoEncoderNVENC.h"
#include "VideoEncoderVCE.h"
#ifdef ALVR_GPL
	#include "VideoEncoderSW.h"
#endif
#include "alvr_server/IDRScheduler.h"


	using Microsoft::WRL::ComPtr;

	//----------------------------------------------------------------------------
	// Blocks on reading backbuffer from gpu, so WaitForPresent can return
	// as soon as we know rendering made it this frame.  This step of the pipeline
	// should run about 3ms per frame.
	//----------------------------------------------------------------------------
	class CEncoder : public CThread
	{
	public:
		CEncoder();
		~CEncoder();

		void Initialize(std::shared_ptr<CD3DRender> d3dRender);

		bool CopyToStaging(ID3D11Texture2D *pTexture[][2], vr::VRTextureBounds_t bounds[][2], int layerCount, bool recentering
			, uint64_t presentationTime, uint64_t targetTimestampNs, const std::string& message, const std::string& debugText, FfiGazeOPOffset leftGazeOffset, FfiGazeOPOffset rightGazeOffset, FfiAnglespeed wspeed);

		virtual void Run();

		virtual void Stop();

		void NewFrameReady();

		void WaitForEncode();

		void OnStreamStart();

		void OnPacketLoss();

		void InsertIDR();

		void CaptureFrame();

		void QpModeset();

		void RoiSizeset();

		void CentreSizeset();

		void CentreSizereset();

		void QpModezero();

		void RoiSizezero();

		void COF0set();

		void COF1set();

		void COF0reset();

		void COF1reset();

		void QPDistribution();
		
	private:
		CThreadEvent m_newFrameReady, m_encodeFinished;
		std::shared_ptr<VideoEncoder> m_videoEncoder;
		bool m_bExiting;
		uint64_t m_presentationTime;
		uint64_t m_targetTimestampNs;
		FfiGazeOPOffset m_GazeOffset[2] = {{0.621,0.395},{0.338,0.395}};
		FfiAnglespeed m_wspeed={0,0,0};

		std::shared_ptr<FrameRender> m_FrameRender;

		IDRScheduler m_scheduler;
		// capture button
		std::atomic_bool m_captureFrame = false;
		std::atomic_bool m_qpmodeset = false;
		std::atomic_bool m_roisizeset = false;
		std::atomic_bool m_centresizeset = false;
		std::atomic_bool m_centresizereset = false;
		std::atomic_bool m_qpmodezero = false;
		std::atomic_bool m_roisizezero = false;
		std::atomic_bool m_cof0set = false;
		std::atomic_bool m_cof1set = false;
		std::atomic_bool m_cof0reset = false;
		std::atomic_bool m_cof1reset = false;
		std::atomic_bool m_QPDistribution = false;
	};

