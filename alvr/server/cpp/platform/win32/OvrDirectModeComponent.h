#pragma once
#include "openvr_driver.h"
#include "alvr_server/Utils.h"
#include "CEncoder.h"
#include "alvr_server/PoseHistory.h"

#include "alvr_server/Settings.h"

#include "alvr_server/include/openvr_math.h"   
#include "alvr_server/include/OVR_Math.h"   
#define PI       3.14159265358979323846 
class OvrDirectModeComponent : public vr::IVRDriverDirectModeComponent
{
public:
	OvrDirectModeComponent(std::shared_ptr<CD3DRender> pD3DRender, std::shared_ptr<PoseHistory> poseHistory);

	void SetEncoder(std::shared_ptr<CEncoder> pEncoder);

	/** Specific to Oculus compositor support, textures supplied must be created using this method. */
	virtual void CreateSwapTextureSet( uint32_t unPid, const SwapTextureSetDesc_t *pSwapTextureSetDesc, SwapTextureSet_t *pOutSwapTextureSet );

	/** Used to textures created using CreateSwapTextureSet.  Only one of the set's handles needs to be used to destroy the entire set. */
	virtual void DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle);

	/** Used to purge all texture sets for a given process. */
	virtual void DestroyAllSwapTextureSets(uint32_t unPid);

	/** After Present returns, calls this to get the next index to use for rendering. */
	virtual void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t(*pIndices)[2]);

	/** Call once per layer to draw for this frame.  One shared texture handle per eye.  Textures must be created
	* using CreateSwapTextureSet and should be alternated per frame.  Call Present once all layers have been submitted. */
	virtual void SubmitLayer(const SubmitLayerPerEye_t(&perEye)[2]);

	/** Submits queued layers for display. */
	virtual void Present(vr::SharedTextureHandle_t syncTexture);
		/** Called after Present to allow driver to take more time until vsync after they've successfully acquired the sync texture in Present.*/
	virtual void PostPresent();

	void CopyTexture(uint32_t layerCount);

	double calspeed(double offset);

private:
	std::shared_ptr<CD3DRender> m_pD3DRender;
	std::shared_ptr<CEncoder> m_pEncoder;
	std::shared_ptr<PoseHistory> m_poseHistory;

	// Resource for each process
	struct ProcessResource {
		ComPtr<ID3D11Texture2D> textures[3];
		HANDLE sharedHandles[3];
		uint32_t pid;
	};
	std::map<HANDLE, std::pair<ProcessResource *, int> > m_handleMap;
   

   
	static const int MAX_LAYERS = 10;
	int m_submitLayer;
	SubmitLayerPerEye_t m_submitLayers[MAX_LAYERS][2];
	vr::HmdQuaternion_t m_prevFramePoseRotation;
	vr::HmdQuaternion_t m_framePoseRotation;
	uint64_t m_targetTimestampNs;
	uint64_t m_prevTargetTimestampNs;
	uint64_t m_targetTimestampNs_txt;
	uint64_t m_prevTargetTimestampNs_txt;
	FfiGazeOPOffset m_GazeOffset[2];
	FfiGazeOPOffset HisGloGazeLoactDel[2];
	FfiQuat m_GazeQuat[2];
	FfiQuat m_preGazeQuat[2];
	FfiQuat m_GlobalQuat[2];
	FfiQuat m_preGlobalQuat[2];

	GazeHistory m_gazeinfo;


	FfiAnglespeed m_wspeed;

};

void GazeQuatToNDCLocation(FfiQuat LGazeQuat ,FfiQuat RGazeQuat	,FfiGazeOPOffset *LNDCLocat , FfiGazeOPOffset *RNDCLocat);
void GazeQuatToNDCLocation(FfiQuat LGazeQuat ,FfiQuat RGazeQuat	,FfiGazeOPOffset *LNDCLocat , FfiGazeOPOffset *RNDCLocat , double *RGazeVector ,double *LGazeVector);
FfiGazeOPOffset DeltaLocationCal(FfiGazeOPOffset nowNDCLocat , FfiGazeOPOffset preNDCLocat);
FfiQuat QuatFmt( vr::HmdQuaternion_t  rawQuat);
FfiQuat DelatQuatCal( FfiQuat preQuat , FfiQuat nowQuat);