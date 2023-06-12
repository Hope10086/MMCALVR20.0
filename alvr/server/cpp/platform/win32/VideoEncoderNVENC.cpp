#include "VideoEncoderNVENC.h"
#include "NvCodecUtils.h"
#include <math.h>
#include "alvr_server/Logger.h"
#include "alvr_server/Settings.h"
#include "alvr_server/Utils.h"

int Cap_EMPHASIS;
bool Enable_H264 = false;

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

void VideoEncoderNVENC::Transmit(ID3D11Texture2D *pTexture, uint64_t presentationTime, uint64_t targetTimestampNs, bool insertIDR, FfiGazeOPOffset NDCLeftGaze, FfiGazeOPOffset NDCRightGaze)
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
	m_pD3DRender->GetContext()->CopyResource(pInputTexture, pTexture);
    
	D3D11_TEXTURE2D_DESC encDesc;
	pTexture->GetDesc(&encDesc);

	NV_ENC_PIC_PARAMS picParams = {};
	if (insertIDR) {
		Debug("Inserting IDR frame.\n");
		picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
	}
	if (true)
	{    		
		int macrosize = 32;
		if (Enable_H264)
		{
			macrosize = 16;
		}

		int Roi_qpDelta = -24; //51-24=27
		int nRoi_qpDelta = 0;
		int Roi_Size = Settings::Instance().m_delatQPmode;
		// switch (Settings::Instance().m_delatQPmode)
		// {
		// case 0:
		//     Roi_qpDelta = -24;
		// 	nRoi_qpDelta = 0;
		// 	break;
		// case 1:	
		// 	Roi_qpDelta = -24;  //51-24 = 27
		// 	nRoi_qpDelta = -5;  //51-5 = 46
		// 	break;
		// case 2:
		// 	Roi_qpDelta = -24;
		// 	nRoi_qpDelta = -10; //41				
		// 	break;
		// case 3:
		// 	Roi_qpDelta = -24;
		// 	nRoi_qpDelta = -15;	//36			
		// 	break;
		// case 4:
		// 	Roi_qpDelta = -24;
		// 	nRoi_qpDelta = -20;	//31			
		// 	break;		
		// default:
		// 	Roi_qpDelta = -24;
		// 	nRoi_qpDelta = 0;
		// 	break;
		// }
		int countx = Roi_Size*(float(encDesc.Width)/float(2*1792));
		int county = Roi_Size*(float(encDesc.Height)/float(1920));
		//Info("Delta QP Mode: %d  \n", Settings::Instance().m_delatQPmode);
		//Info("Roi MacroSize(single) = %dX%d \n", countx,county);
		Info("Roi QP = %d Roi QP =%d \n", 51+Roi_qpDelta, 51+nRoi_qpDelta);
		float angle = (2*atanf((2*Roi_Size+1)*16/812.4644))*(180/3.1415926);
		Info("Roi Size = %f °\n", angle);


	
		picParams.qpDeltaMapSize = (encDesc.Width/macrosize)*(encDesc.Height/macrosize);
		picParams.qpDeltaMap = (int8_t*)malloc(picParams.qpDeltaMapSize * sizeof(int8_t));     
		// for (int i = 0; i < picParams.qpDeltaMapSize; i++)
		// {
		// 	picParams.qpDeltaMap[i] = NV_ENC_EMPHASIS_MAP_LEVEL_0;
		// }
		// for (int i = 0; i < picParams.qpDeltaMapSize/2; i++)
		// {
		// 	picParams.qpDeltaMap[i] = NV_ENC_EMPHASIS_MAP_LEVEL_4;
		// }
		// calcuate  Marco's location 
		// UINT leftgazeMac_X  = ((NDCLeftGaze.x)*encDesc.Width/2)/macrosize ;
		// UINT leftgazeMac_Y  = ((NDCLeftGaze.y)*encDesc.Height)/macrosize ;

		// UINT rightgazeMac_X = ((1.0+NDCRightGaze.x)*encDesc.Width/2)/macrosize;
		// UINT rightgazeMac_Y = ((NDCRightGaze.y)*encDesc.Height)/macrosize;

		int leftgazeMac_X  = ((NDCLeftGaze.x)*encDesc.Width/2)/macrosize ;
		int leftgazeMac_Y  = ((NDCLeftGaze.y)*encDesc.Height) /macrosize;

		int rightgazeMac_X = ((1.0+NDCRightGaze.x)*encDesc.Width/2)/macrosize;
		int rightgazeMac_Y = ((NDCRightGaze.y)*encDesc.Height)/macrosize;

		for (int x = 0; x < encDesc.Width/macrosize; x++)   
			{
				for (int y = 0; y < encDesc.Height/macrosize; y++)
				{
					if (abs(x - leftgazeMac_X) <= countx && abs(y - leftgazeMac_Y) <= county   && x < (encDesc.Width/macrosize)/2)  
					{
						picParams.qpDeltaMap[y * (encDesc.Width/macrosize) + x] = Roi_qpDelta; 																		  
					}
					else if (abs(x -rightgazeMac_X) <= countx && abs(y - rightgazeMac_Y) <= county && x >= (encDesc.Width/macrosize)/2 )
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
	
	m_NvNecoder->EncodeFrame(vPacket, &picParams);

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
