#pragma once

#include <string>
#include <memory>
#include <stdint.h>
#include <chrono>	
#include <iostream>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>

#include <windows.h>
#include <dxgi.h>
#include <unknwn.h>
#include <cinttypes>

#include "shared/d3drender.h"
#include "openvr_driver.h"
#include "FFR.h"

//#include "ScreenGrab11.h"	

#define GPU_PRIORITY_VAL 7

using Microsoft::WRL::ComPtr;

template<class T> class ComQIPtr : public ComPtr<T> {

public:
	inline ComQIPtr(IUnknown* unk)
	{
		this->ptr_ = nullptr;
		unk->QueryInterface(__uuidof(T), (void**)this->GetAddressOf());
	}

	inline ComPtr<T>& operator=(IUnknown* unk)
	{
		ComPtr<T>::Clear();
		unk->QueryInterface(__uuidof(T), (void**)this->GetAddressOf());
		return *this;
	}
};

class FrameRender
{
public:
	FrameRender(std::shared_ptr<CD3DRender> pD3DRender);
	virtual ~FrameRender();

	bool Startup();
	bool RenderFrame(ID3D11Texture2D *pTexture[][2], vr::VRTextureBounds_t bounds[][2], int layerCount, bool recentering, const std::string& message, const std::string& debugText, FfiGazeOPOffset NDCLeftGaze, FfiGazeOPOffset NDCRightGaze);
	void GetEncodingResolution(uint32_t *width, uint32_t *height);

	ComPtr<ID3D11Texture2D> GetTexture();
	HRESULT CpuCopyTexture(ID3D11Texture2D *pTexture);
	HRESULT GetSurfaceInfo(
		_In_ size_t width,
        _In_ size_t height,
        _In_ DXGI_FORMAT fmt,
        _Out_opt_ size_t* outNumBytes,
        _Out_opt_ size_t* outRowBytes,
        _Out_opt_ size_t* outNumRows);
	size_t BitsPerPixel(_In_ DXGI_FORMAT fmt);
	void Memcpythread( uint8_t *dptr_tmp , const uint8_t *sptr_tmp ,size_t rowCount ,size_t rowBytes, size_t msize ,bool endflag);
private:
	std::shared_ptr<CD3DRender> m_pD3DRender;
	ComPtr<ID3D11Texture2D> m_pStagingTexture;

	ComPtr<ID3D11VertexShader> m_pVertexShader;
	ComPtr<ID3D11PixelShader> m_pPixelShader;

	ComPtr<ID3D11InputLayout> m_pVertexLayout;
	ComPtr<ID3D11Buffer> m_pVertexBuffer;
	ComPtr<ID3D11Buffer> m_pIndexBuffer;

	ComPtr<ID3D11SamplerState> m_pSamplerLinear;

	ComPtr<ID3D11Texture2D> m_pDepthStencil;
	ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
	ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;

	ComPtr<ID3D11BlendState> m_pBlendStateFirst;
	ComPtr<ID3D11BlendState> m_pBlendState;

	ComPtr<ID3D11Resource> m_recenterTexture;
	ComPtr<ID3D11ShaderResourceView> m_recenterResourceView;
	ComPtr<ID3D11Resource> m_messageBGTexture;
	ComPtr<ID3D11ShaderResourceView> m_messageBGResourceView;

	void CreateGazepointTexture(D3D11_TEXTURE2D_DESC m_srcDesc);
	ComPtr<ID3D11Texture2D> GazepointTexture;
	//UINT m_GazepointWidth = 1024;  //
	//UINT m_GazepointHeight = 1024;
	long int begaincount = 0;
	long int cpureadcount = 0;
	struct SimpleVertex
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT2 Tex;
		uint32_t View;
	};
	// Parameter for Draw method. 2-triangles for both eyes.
	static const int VERTEX_INDEX_COUNT = 12;

	// shn Copy tfrom GPU to CPU
	ComPtr<ID3D11Texture2D> m_stagingTexture;
	D3D11_TEXTURE2D_DESC m_stagingTextureDesc;
	D3D11_MAPPED_SUBRESOURCE m_stagingTextureMap;

	std::unique_ptr<d3d_render_utils::RenderPipeline> m_colorCorrectionPipeline;
	bool enableColorCorrection;

	std::unique_ptr<FFR> m_ffr;
	bool enableFFR;

	static bool SetGpuPriority(ID3D11Device* device)
	{
		typedef enum _D3DKMT_SCHEDULINGPRIORITYCLASS {
			D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE,
			D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL,
			D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL,
			D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL,
			D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH,
			D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME
		} D3DKMT_SCHEDULINGPRIORITYCLASS;

		ComQIPtr<IDXGIDevice> dxgiDevice(device);
		if (!dxgiDevice) {
			Info("[GPU PRIO FIX] Failed to get IDXGIDevice\n");
			return false;
		}

		HMODULE gdi32 = GetModuleHandleW(L"GDI32");
		if (!gdi32) {
			Info("[GPU PRIO FIX] Failed to get GDI32\n");
			return false;
		}

		NTSTATUS(WINAPI* d3dkmt_spspc)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS);
		d3dkmt_spspc = (decltype(d3dkmt_spspc))GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass");
		if (!d3dkmt_spspc) {
			Info("[GPU PRIO FIX] Failed to get d3dkmt_spspc\n");
			return false;
		}

		NTSTATUS status = d3dkmt_spspc(GetCurrentProcess(), D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME);
		if (status == 0xc0000022) { // STATUS_ACCESS_DENIED, see http://deusexmachina.uk/ntstatus.html
			Info("[GPU PRIO FIX] Failed to set process (%d) priority class, please run ALVR as Administrator.\n", GetCurrentProcess());
			return false;
		} else if (status != 0) {
			Info("[GPU PRIO FIX] Failed to set process (%d) priority class: %u\n", GetCurrentProcess(), status);
			return false;
		}

		HRESULT hr = dxgiDevice->SetGPUThreadPriority(GPU_PRIORITY_VAL);
		if (FAILED(hr)) {
			Info("[GPU PRIO FIX] SetGPUThreadPriority failed\n");
			return false;
		}

		Debug("[GPU PRIO FIX] D3D11 GPU priority setup success\n");
		return true;
	}
};
