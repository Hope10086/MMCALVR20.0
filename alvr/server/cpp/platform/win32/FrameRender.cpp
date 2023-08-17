#include "FrameRender.h"
#include "alvr_server/Utils.h"
#include "alvr_server/Logger.h"
#include "alvr_server/Settings.h"
#include "alvr_server/bindings.h"
	


extern uint64_t g_DriverTestMode;

using namespace d3d_render_utils;


FrameRender::FrameRender(std::shared_ptr<CD3DRender> pD3DRender)
	: m_pD3DRender(pD3DRender)
{
		FrameRender::SetGpuPriority(m_pD3DRender->GetDevice());
}


FrameRender::~FrameRender()
{
}

bool FrameRender::Startup()
{
	if (m_pStagingTexture) {
		if (cpureadcount  != 0)
		{
			//Info("cpuread %d",cpureadcount);
           //auto Copy_Start = std::chrono::steady_clock::now();
			HRESULT hr = CpuCopyTexture(m_pStagingTexture.Get());
			if (FAILED(hr))
			{
			Error("CpuCopyTexture Failed  %p %ls\n", hr, GetErrorStr(hr).c_str());
		    return false;
			}
			auto CopyEnd = std::chrono::steady_clock::now();
			//auto CopyLast = std::chrono::duration_cast<std::chrono::microseconds>(Copy_End - Copy_Start);
			//Info("CopyTexture To Cpu Cost %ld \n",CopyLast);
		}
		return true;
	}

	//
	// Create staging texture
	// This is input texture of Video Encoder and is render target of both eyes.
	//

	D3D11_TEXTURE2D_DESC compositionTextureDesc;
	ZeroMemory(&compositionTextureDesc, sizeof(compositionTextureDesc));
	compositionTextureDesc.Width = Settings::Instance().m_renderWidth;
	compositionTextureDesc.Height = Settings::Instance().m_renderHeight;
	compositionTextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	compositionTextureDesc.MipLevels = 1;
	compositionTextureDesc.ArraySize = 1;
	compositionTextureDesc.SampleDesc.Count = 1;
	compositionTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	compositionTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	ComPtr<ID3D11Texture2D> compositionTexture;

	if (FAILED(m_pD3DRender->GetDevice()->CreateTexture2D(&compositionTextureDesc, NULL, &compositionTexture)))
	{
		Error("Failed to create staging texture!\n");
		return false;
	}

	HRESULT hr = m_pD3DRender->GetDevice()->CreateRenderTargetView(compositionTexture.Get(), NULL, &m_pRenderTargetView);
	if (FAILED(hr)) {
		Error("CreateRenderTargetView %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = compositionTextureDesc.Width;
	descDepth.Height = compositionTextureDesc.Height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = m_pD3DRender->GetDevice()->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil);
	if (FAILED(hr)) {
		Error("CreateTexture2D %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}


	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	ZeroMemory(&descDSV, sizeof(descDSV));
	descDSV.Format = descDepth.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;
	hr = m_pD3DRender->GetDevice()->CreateDepthStencilView(m_pDepthStencil.Get(), &descDSV, &m_pDepthStencilView);
	if (FAILED(hr)) {
		Error("CreateDepthStencilView %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	m_pD3DRender->GetContext()->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());

	D3D11_VIEWPORT viewport;
	viewport.Width = (float)Settings::Instance().m_renderWidth;
	viewport.Height = (float)Settings::Instance().m_renderHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	m_pD3DRender->GetContext()->RSSetViewports(1, &viewport);

	//
	// Compile shaders
	//

	std::vector<uint8_t> vshader(FRAME_RENDER_VS_CSO_PTR, FRAME_RENDER_VS_CSO_PTR + FRAME_RENDER_VS_CSO_LEN);
	hr = m_pD3DRender->GetDevice()->CreateVertexShader((const DWORD*)&vshader[0], vshader.size(), NULL, &m_pVertexShader);
	if (FAILED(hr)) {
		Error("CreateVertexShader %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	std::vector<uint8_t> pshader(FRAME_RENDER_PS_CSO_PTR, FRAME_RENDER_PS_CSO_PTR + FRAME_RENDER_PS_CSO_LEN);
	hr = m_pD3DRender->GetDevice()->CreatePixelShader((const DWORD*)&pshader[0], pshader.size(), NULL, &m_pPixelShader);
	if (FAILED(hr)) {
		Error("CreatePixelShader %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	//
	// Create input layout
	//

	// Define the input layout
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "VIEW", 0, DXGI_FORMAT_R32_UINT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE(layout);


	// Create the input layout
	hr = m_pD3DRender->GetDevice()->CreateInputLayout(layout, numElements, &vshader[0],
		vshader.size(), &m_pVertexLayout);
	if (FAILED(hr)) {
		Error("CreateInputLayout %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	// Set the input layout
	m_pD3DRender->GetContext()->IASetInputLayout(m_pVertexLayout.Get());

	//
	// Create vertex buffer
	//

	// Src texture has various geometry and we should use the part of the textures.
	// That part are defined by uv-coordinates of "bounds" passed to IVRDriverDirectModeComponent::SubmitLayer.
	// So we should update uv-coordinates for every frames and layers.
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = sizeof(SimpleVertex) * 8;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = m_pD3DRender->GetDevice()->CreateBuffer(&bd, NULL, &m_pVertexBuffer);
	if (FAILED(hr)) {
		Error("CreateBuffer 1 %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	// Set vertex buffer
	UINT stride = sizeof(SimpleVertex);
	UINT offset = 0;
	m_pD3DRender->GetContext()->IASetVertexBuffers(0, 1, m_pVertexBuffer.GetAddressOf(), &stride, &offset);
	
	//
	// Create index buffer
	//

	WORD indices[] =
	{
		0,1,2,
		0,3,1,

		4,5,6,
		4,7,5
	};

	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(indices);
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = indices;

	hr = m_pD3DRender->GetDevice()->CreateBuffer(&bd, &InitData, &m_pIndexBuffer);
	if (FAILED(hr)) {
		Error("CreateBuffer 2 %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	// Set index buffer
	m_pD3DRender->GetContext()->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

	// Set primitive topology
	m_pD3DRender->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Create the sample state
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = m_pD3DRender->GetDevice()->CreateSamplerState(&sampDesc, &m_pSamplerLinear);
	if (FAILED(hr)) {
		Error("CreateSamplerState %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	//
	// Create alpha blend state
	// We need alpha blending to support layer.
	//

	// BlendState for first layer.
	// Some VR apps (like SteamVR Home beta) submit the texture that alpha is zero on all pixels.
	// So we need to ignore alpha of first layer.
	D3D11_BLEND_DESC BlendDesc;
	ZeroMemory(&BlendDesc, sizeof(BlendDesc));
	BlendDesc.AlphaToCoverageEnable = FALSE;
	BlendDesc.IndependentBlendEnable = FALSE;
	for (int i = 0; i < 8; i++) {
		BlendDesc.RenderTarget[i].BlendEnable = TRUE;
		BlendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
		BlendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
		BlendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
	}

	hr = m_pD3DRender->GetDevice()->CreateBlendState(&BlendDesc, &m_pBlendStateFirst);
	if (FAILED(hr)) {
		Error("CreateBlendState %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	// BleandState for other layers than first.
	BlendDesc.AlphaToCoverageEnable = FALSE;
	BlendDesc.IndependentBlendEnable = FALSE;
	for (int i = 0; i < 8; i++) {
		BlendDesc.RenderTarget[i].BlendEnable = TRUE;
		BlendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
		BlendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	}

	hr = m_pD3DRender->GetDevice()->CreateBlendState(&BlendDesc, &m_pBlendState);
	if (FAILED(hr)) {
		Error("CreateBlendState %p %ls\n", hr, GetErrorStr(hr).c_str());
		return false;
	}

	m_pStagingTexture = compositionTexture;

	std::vector<uint8_t> quadShaderCSO(QUAD_SHADER_CSO_PTR, QUAD_SHADER_CSO_PTR + QUAD_SHADER_CSO_LEN);
	ComPtr<ID3D11VertexShader> quadVertexShader = CreateVertexShader(m_pD3DRender->GetDevice(), quadShaderCSO);

	enableColorCorrection = Settings::Instance().m_enableColorCorrection;
	if (enableColorCorrection) {
		std::vector<uint8_t> colorCorrectionShaderCSO(COLOR_CORRECTION_CSO_PTR, COLOR_CORRECTION_CSO_PTR + COLOR_CORRECTION_CSO_LEN);

		ComPtr<ID3D11Texture2D> colorCorrectedTexture = CreateTexture(m_pD3DRender->GetDevice(),
			Settings::Instance().m_renderWidth, Settings::Instance().m_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

		struct ColorCorrection {
			float renderWidth;
			float renderHeight;
			float brightness;
			float contrast;
			float saturation;
			float gamma;
			float sharpening;
			float _align;
		};
		ColorCorrection colorCorrectionStruct = { (float)Settings::Instance().m_renderWidth, (float)Settings::Instance().m_renderHeight,
												  Settings::Instance().m_brightness, Settings::Instance().m_contrast + 1.f,
												  Settings::Instance().m_saturation + 1.f, Settings::Instance().m_gamma,
												  Settings::Instance().m_sharpening };
		ComPtr<ID3D11Buffer> colorCorrectionBuffer = CreateBuffer(m_pD3DRender->GetDevice(), colorCorrectionStruct);

		m_colorCorrectionPipeline = std::make_unique<RenderPipeline>(m_pD3DRender->GetDevice());
		m_colorCorrectionPipeline->Initialize({ m_pStagingTexture.Get() }, quadVertexShader.Get(), colorCorrectionShaderCSO,
											  colorCorrectedTexture.Get(), colorCorrectionBuffer.Get());

		m_pStagingTexture = colorCorrectedTexture;
	}

	enableFFR = Settings::Instance().m_enableFoveatedRendering;
	if (enableFFR) {
		m_ffr = std::make_unique<FFR>(m_pD3DRender->GetDevice());
		m_ffr->Initialize(m_pStagingTexture.Get());

		m_pStagingTexture = m_ffr->GetOutputTexture();
	}

	Debug("Staging Texture created\n");

	return true;
}


bool FrameRender::RenderFrame(ID3D11Texture2D *pTexture[][2], vr::VRTextureBounds_t bounds[][2], int layerCount, bool recentering, const std::string &message, const std::string& debugText, FfiGazeOPOffset NDCLeftGaze, FfiGazeOPOffset NDCRightGaze)
{
	// Set render target
	m_pD3DRender->GetContext()->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());

	// Set viewport
	D3D11_VIEWPORT viewport;
	viewport.Width = (float)Settings::Instance().m_renderWidth;
	viewport.Height = (float)Settings::Instance().m_renderHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	m_pD3DRender->GetContext()->RSSetViewports(1, &viewport);

	// Clear the back buffer
	m_pD3DRender->GetContext()->ClearRenderTargetView(m_pRenderTargetView.Get(), DirectX::Colors::MidnightBlue);

	// Overlay recentering texture on top of all layers.
	int recenterLayer = -1;
	if (recentering) {
		recenterLayer = layerCount;
		layerCount++;
	}

	for (int i = 0; i < layerCount; i++) {
		ID3D11Texture2D *textures[2];
		vr::VRTextureBounds_t bound[2];

		if (i == recenterLayer) {
			textures[0] = (ID3D11Texture2D *)m_recenterTexture.Get();
			textures[1] = (ID3D11Texture2D *)m_recenterTexture.Get();
			bound[0].uMin = bound[0].vMin = bound[1].uMin = bound[1].vMin = 0.0f;
			bound[0].uMax = bound[0].vMax = bound[1].uMax = bound[1].vMax = 1.0f;
		}
		else {
			textures[0] = pTexture[i][0];
			textures[1] = pTexture[i][1];
			bound[0] = bounds[i][0];
			bound[1] = bounds[i][1];
		}
		if (textures[0] == NULL || textures[1] == NULL) {
			Debug("Ignore NULL layer. layer=%d/%d%s%s\n", i, layerCount
				, recentering ? L" (recentering)" : L"", !message.empty() ? L" (message)" : L"");
			continue;
		}

		D3D11_TEXTURE2D_DESC srcDesc;
		textures[0]->GetDesc(&srcDesc);
		  if (Settings::Instance().m_gazevisual )
		{
			// Test Eye Tracking Visualization screen  piexl coordinate
            //Here, we do not consider the case that the fixation point falls on the edge of the screen, on the one hand, 
		    //because our visualization block itself is small, and on the other hand, 
		    //the available data show that the eye tracking range is in the middle range of the plane center.
	       struct GazePoint
	       {  UINT x;
	         UINT y;
	       } GazePoint[2];
			GazePoint[0].x = NDCLeftGaze.x * srcDesc.Width;
		    GazePoint[0].y = NDCLeftGaze.y * srcDesc.Height;
		    GazePoint[1].x = NDCRightGaze.x * srcDesc.Width;
		    GazePoint[1].y = NDCRightGaze.y * srcDesc.Height;
	       
            UINT W = srcDesc.Width/32;
		    UINT H = srcDesc.Height/32; 
			// TxtPrint("Frame Render Gazexy (%lf %lf) \n", NDCLeftGaze.x, NDCLeftGaze.y);
			// TxtPrint("texture[%d/%d]: %dx%d  \n", i,layerCount,srcDesc.Width, srcDesc.Height);
			// TxtPrint("Left gaze point: %d %d\n",GazePoint[0].x, GazePoint[0].y);
			// TxtPrint("Right gaze point: %d %d\n",GazePoint[1].x, GazePoint[1].y);
          
		  // Gaze Point Texture Subresource Region
		    UINT ScreenCenter_X = 0.5*srcDesc.Width; 
		    UINT ScreenCenter_y = 0.5*srcDesc.Height; 
            D3D11_BOX sourceRegion;
	        sourceRegion.left  = 0;
	        sourceRegion.right = W;
	        sourceRegion.top   = 0;
	        sourceRegion.bottom = H;
	        sourceRegion.front = 0;
	        sourceRegion.back  = 1;
			//TxtPrint(" begin to creat Gazepoint Texture \n");
			 CreateGazepointTexture(srcDesc);
			// //m_pD3DRender->GetContext()->CopySubresourceRegion(textures[0],0,ScreenCenter_X-W/2,ScreenCenter_y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
			// //m_pD3DRender->GetContext()->CopySubresourceRegion(textures[1],0,ScreenCenter_X-W/2,ScreenCenter_y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
		     m_pD3DRender->GetContext()->CopySubresourceRegion(textures[0],0,GazePoint[0].x-W/2,GazePoint[0].y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
		     m_pD3DRender->GetContext()->CopySubresourceRegion(textures[1],0,GazePoint[1].x-W/2,GazePoint[1].y-H/2,0,GazepointTexture.Get(),0,&sourceRegion);
		}
		//   else {

		// 	Info("begaincount=%ld\n",begaincount);

		//   }
		

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = srcDesc.Format;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MostDetailedMip = 0;
		SRVDesc.Texture2D.MipLevels = 1;

		ComPtr<ID3D11ShaderResourceView> pShaderResourceView[2];

		HRESULT hr = m_pD3DRender->GetDevice()->CreateShaderResourceView(textures[0], &SRVDesc, pShaderResourceView[0].ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			Error("CreateShaderResourceView %p %ls\n", hr, GetErrorStr(hr).c_str());
			return false;
		}
		hr = m_pD3DRender->GetDevice()->CreateShaderResourceView(textures[1], &SRVDesc, pShaderResourceView[1].ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			Error("CreateShaderResourceView %p %ls\n", hr, GetErrorStr(hr).c_str());
			return false;
		}

		if (i == 0) {
			m_pD3DRender->GetContext()->OMSetBlendState(m_pBlendStateFirst.Get(), NULL, 0xffffffff);
		}
		else {
			m_pD3DRender->GetContext()->OMSetBlendState(m_pBlendState.Get(), NULL, 0xffffffff);
		}
		
		// Clear the depth buffer to 1.0 (max depth)
		// We need clear depth buffer to correctly render layers.
		m_pD3DRender->GetContext()->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

		//
		// Update uv-coordinates in vertex buffer according to bounds.
		//

		SimpleVertex vertices[] =
		{
			// Left View
		{ DirectX::XMFLOAT3(-1.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(bound[0].uMin, bound[0].vMax), 0 },
		{ DirectX::XMFLOAT3(0.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(bound[0].uMax, bound[0].vMin), 0 },
		{ DirectX::XMFLOAT3(0.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(bound[0].uMax, bound[0].vMax), 0 },
		{ DirectX::XMFLOAT3(-1.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(bound[0].uMin, bound[0].vMin), 0 },
		// Right View
		{ DirectX::XMFLOAT3(0.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(bound[1].uMin, bound[1].vMax), 1 },
		{ DirectX::XMFLOAT3(1.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(bound[1].uMax, bound[1].vMin), 1 },
		{ DirectX::XMFLOAT3(1.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(bound[1].uMax, bound[1].vMax), 1 },
		{ DirectX::XMFLOAT3(0.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(bound[1].uMin, bound[1].vMin), 1 },
		};

		// TODO: Which is better? UpdateSubresource or Map
		//m_pD3DRender->GetContext()->UpdateSubresource(m_pVertexBuffer.Get(), 0, nullptr, &vertices, 0, 0);

		D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
		hr = m_pD3DRender->GetContext()->Map(m_pVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) {
			Error("Map %p %ls\n", hr, GetErrorStr(hr).c_str());
			return false;
		}
		memcpy(mapped.pData, vertices, sizeof(vertices));

		m_pD3DRender->GetContext()->Unmap(m_pVertexBuffer.Get(), 0);

		// Set the input layout
		m_pD3DRender->GetContext()->IASetInputLayout(m_pVertexLayout.Get());

		//
		// Set buffers
		//

		UINT stride = sizeof(SimpleVertex);
		UINT offset = 0;
		m_pD3DRender->GetContext()->IASetVertexBuffers(0, 1, m_pVertexBuffer.GetAddressOf(), &stride, &offset);

		m_pD3DRender->GetContext()->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_pD3DRender->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//
		// Set shaders
		//

		m_pD3DRender->GetContext()->VSSetShader(m_pVertexShader.Get(), nullptr, 0);
		m_pD3DRender->GetContext()->PSSetShader(m_pPixelShader.Get(), nullptr, 0);

		ID3D11ShaderResourceView *shaderResourceView[2] = { pShaderResourceView[0].Get(), pShaderResourceView[1].Get() };
		m_pD3DRender->GetContext()->PSSetShaderResources(0, 2, shaderResourceView);

		m_pD3DRender->GetContext()->PSSetSamplers(0, 1, m_pSamplerLinear.GetAddressOf());
		
		//
		// Draw
		//

		m_pD3DRender->GetContext()->DrawIndexed(VERTEX_INDEX_COUNT, 0, 0);
	}

	if (enableColorCorrection) {
		m_colorCorrectionPipeline->Render();
	}

	if (enableFFR) {
		m_ffr->Render();
	}


	m_pD3DRender->GetContext()->Flush();
	cpureadcount++;
	begaincount++;

	return true;
}

void FrameRender::CreateGazepointTexture(D3D11_TEXTURE2D_DESC m_srcDesc)
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
        //初始化纹理数据 
	    const UINT pixelSize = 4; // 
        const UINT rowPitch = gazeDesc.Width* pixelSize;
        const UINT textureSize = rowPitch * gazeDesc.Height;
        std::vector<float> pixels(textureSize / sizeof(float));
        for (UINT y = 0; y < gazeDesc.Height; ++y)
        {
          for (UINT x = 0; x < gazeDesc.Width; ++x)
          {
            UINT pixelIndex = y * rowPitch / sizeof(float) + x * pixelSize / sizeof(float);
            pixels[pixelIndex + 0] = 0.0f; // 红色通道
            pixels[pixelIndex + 1] = 0.0f; // 绿色通道
            pixels[pixelIndex + 2] = 1.0f; // 蓝色通道
            pixels[pixelIndex + 3] = 0.5f; // 透明度通道
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

ComPtr<ID3D11Texture2D> FrameRender::GetTexture()
{
	return m_pStagingTexture;
}

void FrameRender::GetEncodingResolution(uint32_t *width, uint32_t *height) {
	if (enableFFR) {
		m_ffr->GetOptimizedResolution(width, height);
	}
	else {
		*width = Settings::Instance().m_renderWidth;
		*height = Settings::Instance().m_renderHeight;
	}
	
}

HRESULT FrameRender::CpuCopyTexture(ID3D11Texture2D *pTexture) {
    // Time start
	auto time_start = std::chrono::steady_clock::now();

	//Create  m_stagingTexture
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc(&desc);
	m_stagingTextureDesc.Width = desc.Width;
	m_stagingTextureDesc.Height = desc.Height;
	m_stagingTextureDesc.MipLevels = 1;
	m_stagingTextureDesc.ArraySize = desc.ArraySize;
	m_stagingTextureDesc.Format = desc.Format;
	m_stagingTextureDesc.SampleDesc = desc.SampleDesc;
	m_stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
	m_stagingTextureDesc.BindFlags = 0;
	m_stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	m_stagingTextureDesc.MiscFlags = 0;

	m_pD3DRender->GetDevice()->CreateTexture2D(&m_stagingTextureDesc, nullptr, &m_stagingTexture);

	//TexPassBack::CaptureTexture(m_pD3DRender->GetDevice(),m_stagingTexture.Get(),m_stagingTextureDesc,m_stagingTexture);
    // CopyResource from pTexture to stage
	m_pD3DRender->GetContext()->CopyResource(m_stagingTexture.Get(), pTexture);
	//time 
	auto time_stagcopy = std::chrono::steady_clock::now();
	auto duration_gpucopy = std::chrono::duration_cast<std::chrono::microseconds>(time_stagcopy - time_start);
    //Get surface information for a particular format
    size_t rowPitch, slicePitch, rowCount;
    HRESULT hr = GetSurfaceInfo(desc.Width, desc.Height, desc.Format, &slicePitch, &rowPitch, &rowCount);
    if (FAILED(hr))
	{
        return hr;
	}
    //creat cpu pixels buffers 
	std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[slicePitch]);
    if (!pixels)
	{
		return E_OUTOFMEMORY;
	}

    // Map
	m_pD3DRender->GetContext()->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &m_stagingTextureMap);
	
	auto sptr = static_cast<const uint8_t*>(m_stagingTextureMap.pData);
    if (!sptr)
    {
    m_pD3DRender->GetContext()->Unmap(m_stagingTexture.Get(),0);
    return E_POINTER;
    }

	uint8_t* dptr = pixels.get();
    const size_t msize = std::min<size_t>(rowPitch, m_stagingTextureMap.RowPitch);
	//const size_t msize = rowPitch;

	unsigned concurrent_count = std::thread::hardware_concurrency();
	
	int thread_nums = (concurrent_count % 4)*4 ;
	if (thread_nums == 0)
	{
		thread_nums = (concurrent_count % 2)*2;
	}
	
	Info("thread_nums %d \n",concurrent_count);

	if (thread_nums > 1) 
	{
		// Per thread row counts 
		size_t rowCount_thr = rowCount / thread_nums;


        // threadbytes: Per thread  sum Bytes
		size_t threadbytes = slicePitch / thread_nums;// Which is to better? rowbyte_thr*Rowcount or slicePitch / thread_nums 
		
		//ptr move
		std::vector<std::thread> copythreads;
		uint8_t *dptr_thr[32];
		const uint8_t* sptr_thr[32];
		bool endflag[32] ;
		for (size_t i = 0; i < thread_nums; i++)
		{
			endflag[i] = false;
			dptr_thr[i] = dptr + i * threadbytes;
			sptr_thr[i] = sptr + i * threadbytes;
			copythreads.emplace_back(&FrameRender::Memcpythread,this,dptr_thr[i],sptr_thr[i],rowCount_thr,rowPitch,rowPitch,std::ref(endflag[i]));
		}

		for (size_t i = 0; i < thread_nums; i++)
		{
			copythreads[i].join();
		}
		bool endflag_allthr = false;
 
		for (size_t i = 0; i < thread_nums; i++)
		{
			endflag_allthr = endflag_allthr & endflag[i];
		}
		
		if (endflag_allthr)
		{
			Info("All Threads Success!\n");
		}
		
	}
	else
	{  
		Memcpythread(dptr,sptr,rowCount,rowPitch,msize,false);
	}
	//Info("%x  -- %x --",desc.Format , desc.ArraySize);
	//Info("slicePitch: %lld rowPitch: %lld rowCount: %lld  in mycopy \n",slicePitch,rowPitch,rowCount);
	m_pD3DRender->GetContext()->Unmap(m_stagingTexture.Get(),0);
    auto time_endcopy = std::chrono::steady_clock::now(); 
	auto duration_cpucopy = std::chrono::duration_cast<std::chrono::microseconds>(time_endcopy - time_stagcopy);
    Info("duration_gpucopy %d" ,duration_gpucopy );
    Info("duration_cpucopy %d " ,duration_cpucopy );
}

void FrameRender::Memcpythread( uint8_t *dptr_tmp , const uint8_t *sptr_tmp ,size_t rowCount ,size_t rowBytes, size_t msize ,bool endflag)
{
	for (size_t i = 0; i < rowCount; ++i)
	{
		memcpy_s(dptr_tmp, rowBytes, sptr_tmp, msize);
		dptr_tmp = dptr_tmp + rowBytes;
		sptr_tmp = sptr_tmp + msize;		
	}
	endflag = true;
}

HRESULT FrameRender::GetSurfaceInfo(
	_In_ size_t width,
    _In_ size_t height,
    _In_ DXGI_FORMAT fmt,
    _Out_opt_ size_t* outNumBytes,
    _Out_opt_ size_t* outRowBytes,
    _Out_opt_ size_t* outNumRows)
	{
		uint64_t numBytes = 0;
        uint64_t rowBytes = 0;
        uint64_t numRows = 0;

        bool bc = false;
        bool packed = false;
        bool planar = false;
        size_t bpe = 0;
		// check format

		switch (fmt)
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            bc = true;
            bpe = 8;
            break;

        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            bc = true;
            bpe = 16;
            break;

        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_YUY2:
            packed = true;
            bpe = 4;
            break;

        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            packed = true;
            bpe = 8;
            break;

        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
            planar = true;
            bpe = 2;
            break;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            planar = true;
            bpe = 4;
            break;

        default:
            break;
        }
		//
		if (bc)
        {
            uint64_t numBlocksWide = 0;
            if (width > 0)
            {
                numBlocksWide = std::max<uint64_t>(1u, (uint64_t(width) + 3u) / 4u);
            }
            uint64_t numBlocksHigh = 0;
            if (height > 0)
            {
                numBlocksHigh = std::max<uint64_t>(1u, (uint64_t(height) + 3u) / 4u);
            }
            rowBytes = numBlocksWide * bpe;
            numRows = numBlocksHigh;
            numBytes = rowBytes * numBlocksHigh;
        }
        else if (packed)
        {
            rowBytes = ((uint64_t(width) + 1u) >> 1) * bpe;
            numRows = uint64_t(height);
            numBytes = rowBytes * height;
        }
        else if (fmt == DXGI_FORMAT_NV11)
        {
            rowBytes = ((uint64_t(width) + 3u) >> 2) * 4u;
            numRows = uint64_t(height) * 2u; // Direct3D makes this simplifying assumption, although it is larger than the 4:1:1 data
            numBytes = rowBytes * numRows;
        }
        else if (planar)
        {
            rowBytes = ((uint64_t(width) + 1u) >> 1) * bpe;
            numBytes = (rowBytes * uint64_t(height)) + ((rowBytes * uint64_t(height) + 1u) >> 1);
            numRows = height + ((uint64_t(height) + 1u) >> 1);
        }
        else
        {
            const size_t bpp = BitsPerPixel(fmt);
            if (!bpp)
                return E_INVALIDARG;

            rowBytes = (uint64_t(width) * bpp + 7u) / 8u; // round up to nearest byte
            numRows = uint64_t(height);
            numBytes = rowBytes * height;
        }

        //

		    #if defined(_M_IX86) || defined(_M_ARM) || defined(_M_HYBRID_X86_ARM64)
        static_assert(sizeof(size_t) == 4, "Not a 32-bit platform!");
        if (numBytes > UINT32_MAX || rowBytes > UINT32_MAX || numRows > UINT32_MAX)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    #else
        static_assert(sizeof(size_t) == 8, "Not a 64-bit platform!");
    #endif

        if (outNumBytes)
        {
            *outNumBytes = static_cast<size_t>(numBytes);
        }
        if (outRowBytes)
        {
            *outRowBytes = static_cast<size_t>(rowBytes);
        }
        if (outNumRows)
        {
            *outNumRows = static_cast<size_t>(numRows);
        }


			
			
			return S_OK;
	}

	size_t FrameRender::BitsPerPixel(_In_ DXGI_FORMAT fmt) 
    {
        switch (fmt)
        {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 128;

        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 96;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            return 64;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_YUY2:
            return 32;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return 24;

        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_B4G4R4A4_UNORM:
            return 16;

        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_NV11:
            return 12;

        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
            return 8;

        case DXGI_FORMAT_R1_UNORM:
            return 1;

        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return 4;

        default:
            return 0;
        }
    }
