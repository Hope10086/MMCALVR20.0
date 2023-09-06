#include "VideoEncoderNVENC.h"
#include "NvCodecUtils.h"
#include <math.h>
#include "alvr_server/Logger.h"
#include "alvr_server/Settings.h"
#include "alvr_server/Utils.h"

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

int Cap_EMPHASIS;
bool Enable_H264 = false;
int m_QpModechange = 0;
int m_RoiSizechange = 0;
int m_QPMaxchange = 51;
float m_cof0change=0;
float m_cof1change=0;
int m_QPDistribution_change=4;
int m_centresize_change=4;
float m_speedthreshold_change=0;

//SK
ComPtr<ID3D11Texture2D> GazepointTexture;  //

//float cof0=0.3836,cof1=26.3290;   //watch: QP=cof0*FOV+cof1
//float cof0=0.435,cof1=29.9997;   //play: QP=0.435*FOV+29.9997
float cof0=27.54,cof1=0.01004;

#define QP_Radial 1   // QP=0.3836*FOV+26.3290
#define QP_Radial_square 1  //

VideoEncoderNVENC::VideoEncoderNVENC(std::shared_ptr<CD3DRender> pD3DRender
	, int width, int height)
	: m_pD3DRender(pD3DRender)
	, m_codec(Settings::Instance().m_codec)
	, m_refreshRate(Settings::Instance().m_refreshRate)
	, m_renderWidth(width)
	, m_renderHeight(height)
	, m_bitrateInMBits(30)
{
	
}

VideoEncoderNVENC::~VideoEncoderNVENC()
{}

void VideoEncoderNVENC::Initialize()
{
	//
	// Initialize Encoder
	//

	NV_ENC_BUFFER_FORMAT format = NV_ENC_BUFFER_FORMAT_ABGR;
	
	if (Settings::Instance().m_use10bitEncoder) {
		format = NV_ENC_BUFFER_FORMAT_ABGR10;
	}

	Debug("Initializing CNvEncoder. Width=%d Height=%d Format=%d\n", m_renderWidth, m_renderHeight, format);

	try {
		m_NvNecoder = std::make_shared<NvEncoderD3D11>(m_pD3DRender->GetDevice(), m_renderWidth, m_renderHeight, format, 0);
	}
	catch (NVENCException e) {
		throw MakeException("NvEnc NvEncoderD3D11 failed. Code=%d %hs\n", e.getErrorCode(), e.what());
	}

	NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
	NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
	initializeParams.encodeConfig = &encodeConfig;

	FillEncodeConfig(initializeParams, m_refreshRate, m_renderWidth, m_renderHeight, m_bitrateInMBits * 1'000'000L);
	   
	try {
		m_NvNecoder->CreateEncoder(&initializeParams);
	} 
	catch (NVENCException e) {
		if (e.getErrorCode() == NV_ENC_ERR_INVALID_PARAM) {
			throw MakeException("This GPU does not support H.265 encoding. (NvEncoderCuda NV_ENC_ERR_INVALID_PARAM)");
		}
		throw MakeException("NvEnc CreateEncoder failed. Code=%d %hs", e.getErrorCode(), e.what());
	}

	Debug("CNvEncoder is successfully initialized.\n");
}

void VideoEncoderNVENC::Shutdown()
{
	std::vector<std::vector<uint8_t>> vPacket;
	if(m_NvNecoder)
		m_NvNecoder->EndEncode(vPacket);

	for (std::vector<uint8_t> &packet : vPacket)
	{
		if (fpOut) {
			fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
		}
	}
	if (m_NvNecoder) {
		m_NvNecoder->DestroyEncoder();
		m_NvNecoder.reset();
	}

	Debug("CNvEncoder::Shutdown\n");

	if (fpOut) {
		fpOut.close();
	}
}

void VideoEncoderNVENC::Transmit(ID3D11Texture2D *pTexture, uint64_t presentationTime, uint64_t targetTimestampNs, bool insertIDR, FfiGazeOPOffset NDCLeftGaze, FfiGazeOPOffset NDCRightGaze, GazeHistory m_gazeinfo)
{
	
	

	auto params = GetDynamicEncoderParams();
	if (params.updated) {
		m_bitrateInMBits = params.bitrate_bps / 1'000'000;
		NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
		NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
		initializeParams.encodeConfig = &encodeConfig;
		FillEncodeConfig(initializeParams, params.framerate, m_renderWidth, m_renderHeight, m_bitrateInMBits * 1'000'000L);
		NV_ENC_RECONFIGURE_PARAMS reconfigureParams = { NV_ENC_RECONFIGURE_PARAMS_VER };
		reconfigureParams.reInitEncodeParams = initializeParams;
		m_NvNecoder->Reconfigure(&reconfigureParams);
	}

	std::vector<std::vector<uint8_t>> vPacket;

	const NvEncInputFrame* encoderInputFrame = m_NvNecoder->GetNextInputFrame();

	ID3D11Texture2D *pInputTexture = reinterpret_cast<ID3D11Texture2D*>(encoderInputFrame->inputPtr);
	//m_pD3DRender->GetContext()->CopyResource(pInputTexture, pTexture);
    
	D3D11_TEXTURE2D_DESC encDesc;
	pTexture->GetDesc(&encDesc);

	//SK
	if (Settings::Instance().m_gazevisual )
	{
            UINT W = encDesc.Width/64;
		    UINT H = encDesc.Height/64*2; 
			struct GazePoint
	       {  UINT x;
	         UINT y;
	       } GazePoint[2];
			GazePoint[0].x = (NDCLeftGaze.x)*encDesc.Width/2;
		    GazePoint[0].y = NDCLeftGaze.y * encDesc.Height;
		    GazePoint[1].x = (1.0+NDCRightGaze.x)*encDesc.Width/2;
		    GazePoint[1].y = NDCRightGaze.y * encDesc.Height;
            D3D11_BOX sourceRegion;
	        sourceRegion.left  = 0;
	        sourceRegion.right = W;
	        sourceRegion.top   = 0;
	        sourceRegion.bottom = H;
	        sourceRegion.front = 0;
	        sourceRegion.back  = 1;
			if(GazepointTexture==NULL)  //降低延迟
			{
			CreateGazepointTexture(encDesc);
			}
			m_pD3DRender->GetContext()->CopySubresourceRegion(pTexture,0,GazePoint[0].x-W/2,GazePoint[0].y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
		    m_pD3DRender->GetContext()->CopySubresourceRegion(pTexture,0,GazePoint[1].x-W/2,GazePoint[1].y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
	}

   //SK

	// capture pictures sequence
	if (false /*Settings::Instance().m_capturePicture */)
	{
		D3D11_BOX srcRegion;
	   	srcRegion.left   = 0;
	    srcRegion.right  = encDesc.Width/2;
	    srcRegion.top    = 0;
	    srcRegion.bottom = encDesc.Height;
	    srcRegion.front  = 0;
	    srcRegion.back   = 1;
		m_pD3DRender->GetContext()->CopySubresourceRegion(pInputTexture,0,0,0,0,pTexture,0,&srcRegion);
	}
	else
	{
		m_pD3DRender->GetContext()->CopyResource(pInputTexture, pTexture);

	}
//SK  output DDS
	if (Settings::Instance().m_capturePicture)
	{
	wchar_t buf[1024];	
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring wpath = converter.from_bytes(g_driverRootDir)+L"\\dds\\" ;
	_snwprintf_s(buf, sizeof(buf), (wpath+L"%llu.dds").c_str(),targetTimestampNs);
		
	    HRESULT hr = DirectX::SaveDDSTextureToFile(m_pD3DRender->GetContext(), pInputTexture, buf);
        if(FAILED (hr))
        Info("Failed to save DDS texture  %llu to file",targetTimestampNs);
	}
	NV_ENC_PIC_PARAMS picParams = {};
	if (insertIDR) {
		Debug("Inserting IDR frame.\n");
		picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
	}
	// gaze history update
	if (m_gazeBuffer.size() == 0)
	{
		m_gazeBuffer.push_back(m_gazeinfo);
	}
	else
	{
		if (m_gazeBuffer.back().FrameTimestampNs != m_gazeinfo.FrameTimestampNs)
		{
			m_gazeBuffer.push_back(m_gazeinfo);
		}
		
	}
	if (m_gazeBuffer.size() > 90)
	{
		 m_gazeBuffer.pop_front();
	}
	
	
	

	
	
	// qp changed 
	if (true)
	{    		
		int macrosize = 32;
		if (Enable_H264)
		{
			macrosize = 16;
		}
		int Roi_qpDelta = -30; //51-30=21  // may be changed in switch
		int nRoi_qpDelta = -Settings::Instance().m_delatQPmode;
		int Roi_Size = Settings::Instance().m_RoiSize;

		int TDRoiDeltaQp = 0;
		int TDnonRoiDeltaQp = 0;
		int TDsubRoiDeltaQp = 0;

		int countx = Roi_Size*(float(encDesc.Width)/float(2*2592));
		int county = countx;		
		float ZDepth = 2592/(tanf(0.942478)+tanf(0.698132));
		float angle = (2*atanf((2*Roi_Size+1)*16/ZDepth))*(180/(4*atanf(1)));
		if (m_QpModechange != Settings::Instance().m_delatQPmode)
		{
			m_QpModechange = Settings::Instance().m_delatQPmode;
			Info("Roi QP = %d Roi QP =%d \n", 51+Roi_qpDelta, 51+nRoi_qpDelta);
		}
		if (m_RoiSizechange != Settings::Instance().m_RoiSize)
		{
			m_RoiSizechange = Settings::Instance().m_RoiSize;
			Info("Roi Size = %f °  %dx%d (ctu) \n", angle, 2*countx+1, 2*county+1);
		}
		picParams.qpDeltaMapSize = (encDesc.Width/macrosize)*(encDesc.Height/macrosize);
		picParams.qpDeltaMap = (int8_t*)malloc(picParams.qpDeltaMapSize * sizeof(int8_t));   

		int leftgazeMac_X  = ((NDCLeftGaze.x)*encDesc.Width/2)/macrosize ;
		int leftgazeMac_Y  = ((NDCLeftGaze.y)*encDesc.Height) /macrosize;

		int rightgazeMac_X = ((1.0+NDCRightGaze.x)*encDesc.Width/2)/macrosize;
		int rightgazeMac_Y = ((NDCRightGaze.y)*encDesc.Height)/macrosize;

		float distance=0;
		float FOV=0;   //Dgree °
		int expect_qp=51;
		int max_qp = Settings::Instance().m_MaxQp;
		if (m_QPMaxchange != max_qp)
		{
			m_QPMaxchange = max_qp;
			Info("Max Qp = %d\n",max_qp);
		}
		float cof0_final=cof0+Settings::Instance().m_cof0delta;   // changed cof0
		float cof1_final=cof1+Settings::Instance().m_cof1delta;   // changed  cof1
		if (m_cof0change != cof0_final)
		{
			m_cof0change = cof0_final;
			Info("cof0: %f",cof0_final);
		}
		if (m_cof1change != cof1_final)
		{
			m_cof1change = cof1_final;
			Info("cof1: %f",cof1_final);
		}
		if (m_QPDistribution_change != Settings::Instance().m_QPDistribution)
		{
			m_QPDistribution_change=Settings::Instance().m_QPDistribution;
			Info("distribution mode: %d",m_QPDistribution_change);  
		}
		
		int centresize=Settings::Instance().m_centresize;   //centre*2
		if(m_centresize_change != Settings::Instance().m_centresize)
		{
			m_centresize_change=Settings::Instance().m_centresize;
			Info("Centre Size: %d×%d",2*m_centresize_change+1, 2*m_centresize_change+1);  
		}

		if(m_speedthreshold_change!=Settings::Instance().m_speedthreshold)
		{
			m_speedthreshold_change=Settings::Instance().m_speedthreshold;
			Info("Speed Threshold: %f",m_speedthreshold_change);  
		}

        //  Based GazeHistory to  determine whether the current frame is in the saccade process

		if (m_gazeinfo.GazeDirectionAS >Settings::Instance().m_speedthreshold)
		{
			int  precount =0 ;
			for (auto it = m_gazeBuffer.rbegin(); it != m_gazeBuffer.rend() && precount <12; ++it)
			{
				if (it->GazeDirectionAS > Settings::Instance().m_speedthreshold)
				{
					precount++;
				}
				else
				{
					break;
				}
				
			}
			Info("Frames %lld is in saccade ,there are %d frames before it \n",targetTimestampNs ,precount);
			


		}
		else Info("Frames %lld isn't in saccade \n",targetTimestampNs);
		






        // Based GazeHistory  to assign strategy for Time Domain QP Distribution

		if ( Settings::Instance().m_tdmode ) // head speed
		{
			if (m_gazeinfo.HeadDirectionAS > Settings::Instance().m_speedthreshold)
			{
			    TDRoiDeltaQp = Settings::Instance().m_tdroideltaqp;
			    TDsubRoiDeltaQp = Settings::Instance().m_tdsubroideltaqp;
			    TDnonRoiDeltaQp = Settings::Instance().m_tdnonroideltaqp;
			}
			else if (m_gazeinfo.GazeDirectionAS >Settings::Instance().m_speedthreshold)
			{
				TDRoiDeltaQp = Settings::Instance().m_tdroideltaqp;
			    TDsubRoiDeltaQp = Settings::Instance().m_tdsubroideltaqp;
			    TDnonRoiDeltaQp = Settings::Instance().m_tdnonroideltaqp;
			}
			
			

		}



		for (int x = 0; x < encDesc.Width/macrosize; x++)   
			{
				for (int y = 0; y < encDesc.Height/macrosize; y++)
				{
                   if(Settings::Instance().m_QPDistribution==1)  // Squence
				   {
					   // ROI
						if (abs(x - leftgazeMac_X) <= centresize && abs(y - leftgazeMac_Y) <= centresize && x < (encDesc.Width/macrosize)/2)  //Left view roi
						{
							picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = 21+TDRoiDeltaQp-51; 		
							continue;																  
						}
						else if(abs(x -rightgazeMac_X) <= centresize && abs(y - rightgazeMac_Y) <= centresize && x >= (encDesc.Width/macrosize)/2 )// Right view roi
						{
							picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = 21+TDRoiDeltaQp-51; 		
							continue;
						}
					  // non-ROI & sub -ROI

						if (x < (encDesc.Width/macrosize)/2)    //Left eye view 
						{						
							distance=(((abs(x*macrosize-leftgazeMac_X*macrosize)) > (abs(y*macrosize-leftgazeMac_Y*macrosize))) ? (abs(x*macrosize-leftgazeMac_X*macrosize)) : (abs(y*macrosize-leftgazeMac_Y*macrosize)));
						}
					  	else if(x >= (encDesc.Width/macrosize)/2)   //Right eye view
						{
							distance=(((abs(x*macrosize-rightgazeMac_X*macrosize)) > (abs(y*macrosize-rightgazeMac_Y*macrosize))) ? (abs(x*macrosize-rightgazeMac_X*macrosize)) : (abs(y*macrosize-rightgazeMac_Y*macrosize)));
						}
						FOV=2*atanf(distance/ZDepth)*180/(4*atanf(1));
						//expect_qp=floor(cof0_final*FOV+cof1_final);    //linear
						expect_qp = floor(cof0_final*exp(cof1_final*FOV));        //exp
						if(expect_qp < 21)  //limit QP 
						{
							expect_qp = 21 + TDRoiDeltaQp;
						}
					    else {
						   if(expect_qp >= max_qp){
							expect_qp = max_qp + TDnonRoiDeltaQp; //non ROI
						    }
					    	else{
							expect_qp = expect_qp +TDsubRoiDeltaQp; // sub ROI
							}
						}
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = expect_qp-51; 		

				   }
                   else if(Settings::Instance().m_QPDistribution==2)    //circle 
				   {     // ROI
						if (abs(x - leftgazeMac_X) <= centresize && abs(y - leftgazeMac_Y) <= centresize && x < (encDesc.Width/macrosize)/2)  //左眼中心21qp区域
						{
							picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = 21-51; 		
							continue;																  
						}
						else if(abs(x -rightgazeMac_X) <= centresize && abs(y - rightgazeMac_Y) <= centresize && x >= (encDesc.Width/macrosize)/2 )//右眼中心21qp区域
						{
							picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = 21-51; 		
							continue;
						}
                       // non-ROI & sub ROI
						if (x < (encDesc.Width/macrosize)/2)    //
						{						
							distance=sqrt(pow(x*macrosize-leftgazeMac_X*macrosize,2)+pow(y*macrosize-leftgazeMac_Y*macrosize,2));
						}
						else if(x >= (encDesc.Width/macrosize)/2)   //
						{
							distance=sqrt(pow(x*macrosize-rightgazeMac_X*macrosize,2)+pow(y*macrosize-rightgazeMac_Y*macrosize,2));
						}
						FOV=2*atanf(distance/ZDepth)*180/(4*atanf(1));
						//expect_qp=floor(cof0_final*FOV+cof1_final);   //linear
						expect_qp=floor(cof0_final*exp(cof1_final*FOV));        //exp
						if(expect_qp<21)  //
						{
							expect_qp=21;
						}
						if(expect_qp>= max_qp)
						{
							expect_qp= max_qp;
						}
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = expect_qp-51; 
				   }
				   else if(Settings::Instance().m_QPDistribution==0)  // 
				   {
					  if (abs(x - leftgazeMac_X) <= countx && abs(y - leftgazeMac_Y) <= county   && x < (encDesc.Width/macrosize)/2)  //左眼
					{
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = Roi_qpDelta; 																		  
					}
					  else if (abs(x -rightgazeMac_X) <= countx && abs(y - rightgazeMac_Y) <= county && x >= (encDesc.Width/macrosize)/2 )  //右眼
					{						
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = Roi_qpDelta; 	
					}				
					  else
					 {
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = nRoi_qpDelta;
					 }
				   }
				}
			}		
	    }
	m_NvNecoder->EncodeFrame(vPacket, &picParams);

	free(picParams.qpDeltaMap);

	for (std::vector<uint8_t> &packet : vPacket)
	{
		if (fpOut) {
			fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
		}
		
		 ParseFrameNals(m_codec, packet.data(), (int)packet.size(), targetTimestampNs, insertIDR);
	}
}

void VideoEncoderNVENC::FillEncodeConfig(NV_ENC_INITIALIZE_PARAMS &initializeParams, int refreshRate, int renderWidth, int renderHeight, uint64_t bitrate_bps)
{
	auto &encodeConfig = *initializeParams.encodeConfig;
	GUID encoderGUID = m_codec == ALVR_CODEC_H264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;

	GUID qualityPreset;
	// See recommended NVENC settings for low-latency encoding.
	// https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-video-encoder-api-prog-guide/#recommended-nvenc-settings
	switch (Settings::Instance().m_nvencQualityPreset) {
		case 7:
			qualityPreset = NV_ENC_PRESET_P7_GUID;
			break;
		case 6:
			qualityPreset = NV_ENC_PRESET_P6_GUID;
			break;
		case 5:
			qualityPreset = NV_ENC_PRESET_P5_GUID;
			break;
		case 4:
			qualityPreset = NV_ENC_PRESET_P4_GUID;
			break;
		case 3:
			qualityPreset = NV_ENC_PRESET_P3_GUID;
			break;
		case 2:
			qualityPreset = NV_ENC_PRESET_P2_GUID;
			break;
		case 1:
		default:
			qualityPreset = NV_ENC_PRESET_P1_GUID;
			break;
  }

	NV_ENC_TUNING_INFO tuningPreset = static_cast<NV_ENC_TUNING_INFO>(Settings::Instance().m_nvencTuningPreset);
//shn
	m_NvNecoder->CreateDefaultEncoderParams(&initializeParams, encoderGUID, qualityPreset, tuningPreset);

	initializeParams.encodeWidth = initializeParams.darWidth = renderWidth;
	initializeParams.encodeHeight = initializeParams.darHeight = renderHeight;
	initializeParams.frameRateNum = refreshRate;
	initializeParams.frameRateDen = 1;

	if (Settings::Instance().m_nvencRefreshRate != -1) {
		initializeParams.frameRateNum = Settings::Instance().m_nvencRefreshRate;
	}

	initializeParams.enableWeightedPrediction = Settings::Instance().m_nvencEnableWeightedPrediction;

	// 16 is recommended when using reference frame invalidation. But it has caused bad visual quality.
	// Now, use 0 (use default).
	uint32_t maxNumRefFrames = 0;
	uint32_t gopLength = NVENC_INFINITE_GOPLENGTH;

	if (Settings::Instance().m_nvencMaxNumRefFrames != -1) {
		maxNumRefFrames = Settings::Instance().m_nvencMaxNumRefFrames;
	}
	if (Settings::Instance().m_nvencGopLength != -1) {
		gopLength = Settings::Instance().m_nvencGopLength;
	}

	if (m_codec == ALVR_CODEC_H264) {
		auto &config = encodeConfig.encodeCodecConfig.h264Config;
		config.repeatSPSPPS = 1;
		config.enableIntraRefresh = Settings::Instance().m_nvencEnableIntraRefresh;
		
		if (Settings::Instance().m_nvencIntraRefreshPeriod != -1) {
			config.intraRefreshPeriod = Settings::Instance().m_nvencIntraRefreshPeriod;
		}
		if (Settings::Instance().m_nvencIntraRefreshCount != -1) {
			config.intraRefreshCnt = Settings::Instance().m_nvencIntraRefreshCount;
		}

		switch (Settings::Instance().m_entropyCoding) {
			case ALVR_CABAC:
				config.entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CABAC;
				break;
			case ALVR_CAVLC:
				config.entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
				break;
		}

		config.maxNumRefFrames = maxNumRefFrames;
		config.idrPeriod = gopLength;

		if (Settings::Instance().m_fillerData) {
			config.enableFillerDataInsertion = Settings::Instance().m_rateControlMode == ALVR_CBR;
		}
	} 
	else {
		auto &config = encodeConfig.encodeCodecConfig.hevcConfig;
		config.repeatSPSPPS = 1;
		config.enableIntraRefresh = Settings::Instance().m_nvencEnableIntraRefresh;

		if (Settings::Instance().m_nvencIntraRefreshPeriod != -1) {
			config.intraRefreshPeriod = Settings::Instance().m_nvencIntraRefreshPeriod;
		}
		if (Settings::Instance().m_nvencIntraRefreshCount != -1) {
			config.intraRefreshCnt = Settings::Instance().m_nvencIntraRefreshCount;
		}

		config.maxNumRefFramesInDPB = maxNumRefFrames;
		config.idrPeriod = gopLength;

		if (Settings::Instance().m_use10bitEncoder) {
			encodeConfig.encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = 2;
		}

		if (Settings::Instance().m_fillerData) {
			config.enableFillerDataInsertion = Settings::Instance().m_rateControlMode == ALVR_CBR;
		}
	}

	// Disable automatic IDR insertion by NVENC. We need to manually insert IDR when packet is dropped
	// if don't use reference frame invalidation.
	encodeConfig.gopLength = gopLength;
	encodeConfig.frameIntervalP = 1;

	if (Settings::Instance().m_nvencPFrameStrategy != -1) {
		encodeConfig.frameIntervalP = Settings::Instance().m_nvencPFrameStrategy;
	}

	switch (Settings::Instance().m_rateControlMode) {
		case ALVR_CBR:
			//encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
			encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
			encodeConfig.rcParams.constQP = {51,51,51};
			//Info("RC: NV_ENC_PARAMS_RC_CONSTQP \n");
			// shn
			break;
		case ALVR_VBR:
			encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
			break;
	}
	encodeConfig.rcParams.multiPass = static_cast<NV_ENC_MULTI_PASS>(Settings::Instance().m_nvencMultiPass);
	encodeConfig.rcParams.lowDelayKeyFrameScale = 1;
	
	if (Settings::Instance().m_nvencLowDelayKeyFrameScale != -1) {
		encodeConfig.rcParams.lowDelayKeyFrameScale = Settings::Instance().m_nvencLowDelayKeyFrameScale;
	}
	
	uint32_t maxFrameSize = static_cast<uint32_t>(bitrate_bps / refreshRate);
	Debug("VideoEncoderNVENC: maxFrameSize=%d bits\n", maxFrameSize);
	encodeConfig.rcParams.vbvBufferSize = maxFrameSize * 1.1;
	encodeConfig.rcParams.vbvInitialDelay = maxFrameSize * 1.1;
	encodeConfig.rcParams.maxBitRate = static_cast<uint32_t>(bitrate_bps);
	encodeConfig.rcParams.averageBitRate = static_cast<uint32_t>(bitrate_bps);
	if (true)
	{
        Cap_EMPHASIS = m_NvNecoder->GetCapabilityValue(encoderGUID,NV_ENC_CAPS_SUPPORT_EMPHASIS_LEVEL_MAP);
		if (!Cap_EMPHASIS)
		{
			//Info("Emphasis Level Map based delta QP not supported.\n");
		}
		// else
		// {
        //     //Info("Emphasis Level Map based delta QP is supported");
		// 	//encodeConfig.rcParams.qpMapMode = NV_ENC_QP_MAP_EMPHASIS;
		// 	//encodeConfig.rcParams.qpMapMode = NV_ENC_QP_MAP_DELTA;
		// }
		encodeConfig.rcParams.qpMapMode = NV_ENC_QP_MAP_DELTA;
		if (m_codec == ALVR_CODEC_H264)
		{
			Enable_H264 = true;
		}


		
	}
	
	if (Settings::Instance().m_nvencAdaptiveQuantizationMode == SpatialAQ) {
		encodeConfig.rcParams.enableAQ = 1;
	} else if (Settings::Instance().m_nvencAdaptiveQuantizationMode == TemporalAQ) {
		encodeConfig.rcParams.enableTemporalAQ = 1;
	}

	if (Settings::Instance().m_nvencRateControlMode != -1) {
		encodeConfig.rcParams.rateControlMode = (NV_ENC_PARAMS_RC_MODE)Settings::Instance().m_nvencRateControlMode;
	}
	if (Settings::Instance().m_nvencRcBufferSize != -1) {
		encodeConfig.rcParams.vbvBufferSize = Settings::Instance().m_nvencRcBufferSize;
	}
	if (Settings::Instance().m_nvencRcInitialDelay != -1) {
		encodeConfig.rcParams.vbvInitialDelay = Settings::Instance().m_nvencRcInitialDelay;
	}
	if (Settings::Instance().m_nvencRcMaxBitrate != -1) {
		encodeConfig.rcParams.maxBitRate = Settings::Instance().m_nvencRcMaxBitrate;
	}
	if (Settings::Instance().m_nvencRcAverageBitrate != -1) {
		encodeConfig.rcParams.averageBitRate = Settings::Instance().m_nvencRcAverageBitrate;
	}  // log 
	//Info("bitrate: %d\n",encodeConfig.rcParams.averageBitRate);
}

void VideoEncoderNVENC::CreateGazepointTexture(D3D11_TEXTURE2D_DESC m_srcDesc)
{
	    D3D11_TEXTURE2D_DESC gazeDesc;
	    gazeDesc.Width = m_srcDesc.Width;
	    gazeDesc.Height = m_srcDesc.Height;	
	    gazeDesc.Format = m_srcDesc.Format;
	    gazeDesc.Usage = D3D11_USAGE_DEFAULT;
	    gazeDesc.MipLevels = 1;
	    gazeDesc.ArraySize = 1;
	    gazeDesc.SampleDesc.Count = 1;
	    gazeDesc.CPUAccessFlags = 0;
	    gazeDesc.MiscFlags = 0;
	    gazeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;//绑定为常量缓冲区，可以与任何其他绑定标志组合
        //
	    const UINT pixelSize = 4; // 
        const UINT rowPitch = gazeDesc.Width* pixelSize;
        const UINT textureSize = rowPitch * gazeDesc.Height;
        std::vector<float> pixels(textureSize / sizeof(float));
        for (UINT y = 0; y < gazeDesc.Height; ++y)
        {
          for (UINT x = 0; x < gazeDesc.Width; ++x)
          {
            UINT pixelIndex = y * rowPitch / sizeof(float) + x * pixelSize / sizeof(float);
            pixels[pixelIndex + 0] = 1.0f; // 红色通道
            pixels[pixelIndex + 1] = 0.0f; // 绿色通道
            pixels[pixelIndex + 2] = 0.0f; // 蓝色通道
            pixels[pixelIndex + 3] = 0.0f; // 透明度通道
          }
        }
		D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pixels.data();
        initData.SysMemPitch = rowPitch;
        initData.SysMemSlicePitch = textureSize;
		//创建2D纹理对象
	    HRESULT hr = m_pD3DRender->GetDevice()->CreateTexture2D(&gazeDesc,&initData,&GazepointTexture);
		if (FAILED(hr))
		{
		   Info("CreateTexture2D failed :GazepointTexture hr = %x\n", hr);
		}		
}