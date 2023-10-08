#include "CEncoder.h"


		CEncoder::CEncoder()
			: m_bExiting(false)
			, m_targetTimestampNs(0)
		{
			m_encodeFinished.Set();
		}

		
			CEncoder::~CEncoder()
		{
			if (m_videoEncoder)
			{
				m_videoEncoder->Shutdown();
				m_videoEncoder.reset();
			}
		}

		void CEncoder::Initialize(std::shared_ptr<CD3DRender> d3dRender) {
			m_FrameRender = std::make_shared<FrameRender>(d3dRender);
			m_FrameRender->Startup();
			uint32_t encoderWidth, encoderHeight;
			m_FrameRender->GetEncodingResolution(&encoderWidth, &encoderHeight);

			Exception vceException;
			Exception nvencException;
#ifdef ALVR_GPL
			Exception swException;
			if (Settings::Instance().m_force_sw_encoding) {
				try {
					Info("Try to use VideoEncoderSW.\n");
					m_videoEncoder = std::make_shared<VideoEncoderSW>(d3dRender, encoderWidth, encoderHeight);
					m_videoEncoder->Initialize();
					
					return;
				}
				catch (Exception e) {
					swException = e;
				}
			}
#endif
			
			try {
				Debug("Try to use VideoEncoderVCE.\n");
				m_videoEncoder = std::make_shared<VideoEncoderVCE>(d3dRender, encoderWidth, encoderHeight);
				m_videoEncoder->Initialize();
				return;
			}
			catch (Exception e) {
				vceException = e;
			}
			try { //shn
				Debug("Try to use VideoEncoderNVENC.\n");
				m_videoEncoder = std::make_shared<VideoEncoderNVENC>(d3dRender, encoderWidth, encoderHeight);
				m_videoEncoder->Initialize();
				return;
			}
			catch (Exception e) {
				nvencException = e;
			}
#ifdef ALVR_GPL
			try {
				Debug("Try to use VideoEncoderSW.\n");
				m_videoEncoder = std::make_shared<VideoEncoderSW>(d3dRender, encoderWidth, encoderHeight);
				m_videoEncoder->Initialize();
				return;
			}
			catch (Exception e) {
				swException = e;
			}
			throw MakeException("All VideoEncoder are not available. VCE: %s, NVENC: %s, SW: %s", vceException.what(), nvencException.what(), swException.what());
#else
			throw MakeException("All VideoEncoder are not available. VCE: %s, NVENC: %s", vceException.what(), nvencException.what());
#endif
		}

		bool CEncoder::CopyToStaging(ID3D11Texture2D *pTexture[][2], vr::VRTextureBounds_t bounds[][2], int layerCount, bool recentering
			, uint64_t presentationTime, uint64_t targetTimestampNs, const std::string& message, const std::string& debugText,  FfiGazeOPOffset leftGazeOffset, FfiGazeOPOffset rightGazeOffset, FfiAnglespeed wspeed)
		{
			m_presentationTime = presentationTime;
			m_targetTimestampNs = targetTimestampNs;
			//Info("Source file:CEncoder.cpp\n");
			//Info("Recive GazeOffset = (%lf,%lf) (%lf,%lf)\n",leftGazeOffset.x, leftGazeOffset.y, rightGazeOffset.x, rightGazeOffset.y);
			m_GazeOffset[0] = leftGazeOffset;
			m_GazeOffset[1] = rightGazeOffset;

			m_wspeed=wspeed;
			//TxtPrint("Frame Render Time %llu ",m_targetTimestampNs);
			m_FrameRender->Startup();
			m_FrameRender->RenderFrame(pTexture, bounds, layerCount, recentering, message, debugText, m_GazeOffset[0], m_GazeOffset[1]); 
			return true;
		}

		void CEncoder::Run()
		{
			Debug("CEncoder: Start thread. Id=%d\n", GetCurrentThreadId());
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_MOST_URGENT);

			while (!m_bExiting)
			{
				m_newFrameReady.Wait();
				if (m_bExiting)
					break;
				if (m_captureFrame)
					{
						// capture pictures sequence 
				        Settings::Instance().m_capturePicture ^= m_captureFrame;
				        // capture signal picture
				        //Settings::Instance().m_capturePicture ^= m_captureFrame;
						m_captureFrame = false; 
						//Info("m_captureFrame has been set");						
					}
				if (m_qpmodeset)
				{
				        m_qpmodeset = false;
						Settings::Instance().m_delatQPmode =(Settings::Instance().m_delatQPmode+1)%29 ;
				}
				if (m_roisizeset)
				{
					    m_roisizeset =false;
						Settings::Instance().m_RoiSize = (Settings::Instance().m_RoiSize+1)%40 ;
						
				}
				if (m_centresizeset)
				{
					    m_centresizeset =false;
						Settings::Instance().m_centresize = (Settings::Instance().m_centresize+1)%40 ;
						Info("Centre Size: %d×%d",2*Settings::Instance().m_centresize+1, 2*Settings::Instance().m_centresize+1);  
				}
				if (m_centresizereset)
				{
					    m_centresizereset =false;
						Settings::Instance().m_centresize = 0 ;
						Info("Centre Size: %d×%d",2*Settings::Instance().m_centresize+1, 2*Settings::Instance().m_centresize+1); 
				}

				if (m_qpmodezero)
				{
					    m_qpmodezero  = false;
					    Settings::Instance().m_delatQPmode = 0;

				}
				if (m_roisizezero)
				{
					    m_roisizezero = false; 
						Settings::Instance().m_RoiSize = 0;

				}		
				
				
				if(m_QPDistribution)
				{
					m_QPDistribution=false;
					Settings::Instance().m_QPDistribution=(Settings::Instance().m_QPDistribution+1)%3;  //三种模式
					Info("distribution mode: %d",Settings::Instance().m_QPDistribution);  
				}


				if (m_FrameRender->GetTexture())
				{
					m_videoEncoder->Transmit(m_FrameRender->GetTexture().Get(), m_presentationTime, m_targetTimestampNs, m_scheduler.CheckIDRInsertion(), m_GazeOffset[0], m_GazeOffset[1], m_wspeed);
				}

				m_encodeFinished.Set();
			}
		}

		void CEncoder::Stop()
		{
			m_bExiting = true;
			m_newFrameReady.Set();
			Join();
			m_FrameRender.reset();
		}

		void CEncoder::NewFrameReady()
		{
			m_encodeFinished.Reset();
			m_newFrameReady.Set();
		}

		void CEncoder::WaitForEncode()
		{
			m_encodeFinished.Wait();
		}

		void CEncoder::OnStreamStart() {
			m_scheduler.OnStreamStart();
		}

		void CEncoder::OnPacketLoss() {
			m_scheduler.OnPacketLoss();
		}

		void CEncoder::InsertIDR() {
			m_scheduler.InsertIDR();
		}

		void CEncoder::CaptureFrame() {
			m_captureFrame = true;
		}

		void CEncoder::QpModeset() { m_qpmodeset = true;}
		void CEncoder::RoiSizeset() { m_roisizeset =true;}
		void CEncoder::CentreSizeset() { m_centresizeset =true;}
		void CEncoder::CentreSizereset() { m_centresizereset =true;}
		void CEncoder::QpModezero() { m_qpmodezero = true;}
		void CEncoder::RoiSizezero() { m_roisizezero = true;}

		void CEncoder::QPDistribution() {  m_QPDistribution = true;}
