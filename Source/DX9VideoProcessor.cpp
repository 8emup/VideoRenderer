/*
* (C) 2018 see Authors.txt
*
* This file is part of MPC-BE.
*
* MPC-BE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-BE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "stdafx.h"
#include <vector>
#include <d3d9.h>
#include <dvdmedia.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <mfidl.h>
#include <Mferror.h>
#include <dwmapi.h>
#include "Time.h"
#include "DX9VideoProcessor.h"

#define STATS_W 330
#define STATS_H 150

#pragma pack(push, 1)
template<unsigned texcoords>
struct MYD3DVERTEX {
	float x, y, z, rhw;
	struct {
		float u, v;
	} t[texcoords];
};
#pragma pack(pop)

template<unsigned texcoords>
static HRESULT TextureBlt(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<texcoords> v[4], D3DTEXTUREFILTERTYPE filter)
{
	if (!pD3DDev) {
		return E_POINTER;
	}

	DWORD FVF = 0;

	switch (texcoords) {
	case 1: FVF = D3DFVF_TEX1; break;
	case 2: FVF = D3DFVF_TEX2; break;
	case 3: FVF = D3DFVF_TEX3; break;
	case 4: FVF = D3DFVF_TEX4; break;
	case 5: FVF = D3DFVF_TEX5; break;
	case 6: FVF = D3DFVF_TEX6; break;
	case 7: FVF = D3DFVF_TEX7; break;
	case 8: FVF = D3DFVF_TEX8; break;
	default:
		return E_FAIL;
	}

	HRESULT hr;

	hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);

	for (unsigned i = 0; i < texcoords; i++) {
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MAGFILTER, filter);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MINFILTER, filter);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

		hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}

	hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | FVF);
	//hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0]));
	std::swap(v[2], v[3]);
	hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));

	for (unsigned i = 0; i < texcoords; i++) {
		pD3DDev->SetTexture(i, nullptr);
	}

	return S_OK;
}

// CDX9VideoProcessor

static UINT GetAdapter(HWND hWnd, IDirect3D9Ex* pD3D)
{
	CheckPointer(hWnd, D3DADAPTER_DEFAULT);
	CheckPointer(pD3D, D3DADAPTER_DEFAULT);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	CheckPointer(hMonitor, D3DADAPTER_DEFAULT);

	for (UINT adp = 0, num_adp = pD3D->GetAdapterCount(); adp < num_adp; ++adp) {
		const HMONITOR hAdapterMonitor = pD3D->GetAdapterMonitor(adp);
		if (hAdapterMonitor == hMonitor) {
			return adp;
		}
	}

	return D3DADAPTER_DEFAULT;
}

CDX9VideoProcessor::CDX9VideoProcessor(CBaseRenderer* pFilter)
{
	m_pFilter = pFilter;

	if (!m_hD3D9Lib) {
		m_hD3D9Lib = LoadLibraryW(L"d3d9.dll");
	}
	if (!m_hD3D9Lib) {
		return;
	}

	HRESULT(WINAPI *pfnDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pfnDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	HRESULT hr = pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pfnDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
	}
	if (!m_pD3DEx) {
		return;
	}

	DXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		m_pD3DEx.Release();
	}

	// GDI+ handling
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);
}

CDX9VideoProcessor::~CDX9VideoProcessor()
{
	//StopWorkerThreads();

	ReleaseDevice();

	m_pD3DDeviceManager.Release();
	m_nResetTocken = 0;

	m_pD3DEx.Release();

	if (m_hD3D9Lib) {
		FreeLibrary(m_hD3D9Lib);
	}

	// GDI+ handling
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

HRESULT CDX9VideoProcessor::Init(const HWND hwnd, const int iSurfaceFmt, bool* pChangeDevice)
{
	CheckPointer(m_pD3DEx, E_FAIL);

	m_hWnd = hwnd;
	switch (iSurfaceFmt) {
	default:
	case 0:
		m_VPOutputFmt = D3DFMT_X8R8G8B8;
		break;
	case 1:
		m_VPOutputFmt = D3DFMT_A2R10G10B10;
		break;
	case 2:
		m_VPOutputFmt = D3DFMT_A16B16G16R16F;
		break;
	}

	const UINT currentAdapter = GetAdapter(m_hWnd, m_pD3DEx);
	bool bTryToReset = (currentAdapter == m_CurrentAdapter) && m_pD3DDevEx;
	if (!bTryToReset) {
		ReleaseDevice();
		m_CurrentAdapter = currentAdapter;
	}

	D3DADAPTER_IDENTIFIER9 AdapID9 = {};
	if (S_OK == m_pD3DEx->GetAdapterIdentifier(m_CurrentAdapter, 0, &AdapID9)) {
		m_VendorId = AdapID9.VendorId;
		m_strAdapterDescription.Format(L"%S (%04X:%04X)", AdapID9.Description, AdapID9.VendorId, AdapID9.DeviceId);
	}

	ZeroMemory(&m_DisplayMode, sizeof(D3DDISPLAYMODEEX));
	m_DisplayMode.Size = sizeof(D3DDISPLAYMODEEX);
	HRESULT hr = m_pD3DEx->GetAdapterDisplayModeEx(m_CurrentAdapter, &m_DisplayMode, nullptr);

#ifdef _DEBUG
	D3DCAPS9 DevCaps = {};
	if (S_OK == m_pD3DEx->GetDeviceCaps(m_CurrentAdapter, D3DDEVTYPE_HAL, &DevCaps)) {
		CStringW dbgstr = L"DeviceCaps:";
		dbgstr.AppendFormat(L"\nMaxTextureWidth                 : %u", DevCaps.MaxTextureWidth);
		dbgstr.AppendFormat(L"\nMaxTextureHeight                : %u", DevCaps.MaxTextureHeight);
		dbgstr.AppendFormat(L"\nPresentationInterval IMMEDIATE  : %s", DevCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\nPresentationInterval ONE        : %s", DevCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\nCaps READ_SCANLINE              : %s", DevCaps.Caps & D3DCAPS_READ_SCANLINE ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\nPixelShaderVersion              : %u.%u", D3DSHADER_VERSION_MAJOR(DevCaps.PixelShaderVersion), D3DSHADER_VERSION_MINOR(DevCaps.PixelShaderVersion));
		dbgstr.AppendFormat(L"\nMaxPixelShader30InstructionSlots: %u", DevCaps.MaxPixelShader30InstructionSlots);
		DLog(dbgstr);
	}
#endif

	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));
	m_d3dpp.Windowed = TRUE;
	m_d3dpp.hDeviceWindow = m_hWnd;
	m_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	m_d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
	m_d3dpp.BackBufferCount = 1;
	m_d3dpp.BackBufferWidth = m_DisplayMode.Width;
	m_d3dpp.BackBufferHeight = m_DisplayMode.Height;
	m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	if (bTryToReset) {
		bTryToReset = SUCCEEDED(hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr));
		DLog(L"    => ResetEx() : %s", HR2Str(hr));
	}

	if (!bTryToReset) {
		ReleaseDevice();
		hr = m_pD3DEx->CreateDeviceEx(
			m_CurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS,
			&m_d3dpp, nullptr, &m_pD3DDevEx);
		DLog(L"    => CreateDeviceEx() : %s", HR2Str(hr));
	}

	if (FAILED(hr)) {
		return hr;
	}
	if (!m_pD3DDevEx) {
		return E_FAIL;
	}

	while (hr == D3DERR_DEVICELOST) {
		DLog(L"    => D3DERR_DEVICELOST. Trying to Reset.");
		hr = m_pD3DDevEx->CheckDeviceState(m_hWnd);
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		DLog(L"    => D3DERR_DEVICENOTRESET");
		hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr);
	}

	if (S_OK == hr && !bTryToReset) {
		hr = m_pD3DDeviceManager->ResetDevice(m_pD3DDevEx, m_nResetTocken);
	}

	if (pChangeDevice) {
		*pChangeDevice = !bTryToReset;
	}

	m_pMemSurface.Release();
	m_pOSDTexture.Release();
	if (S_OK == m_pD3DDevEx->CreateTexture(STATS_W, STATS_H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pOSDTexture, nullptr)) {
		m_pD3DDevEx->CreateOffscreenPlainSurface(STATS_W, STATS_H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &m_pMemSurface, nullptr);
	}

	if (!m_pDXVA2_VPService) {
		// Create DXVA2 Video Processor Service.
		hr = DXVA2CreateVideoService(m_pD3DDevEx, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
		if (FAILED(hr)) {
			DLog(L"CDX9VideoProcessor::Init : DXVA2CreateVideoService() failed with error %s", HR2Str(hr));
			return FALSE;
		}
	}

	// TODO
	//StopWorkerThreads();
	//StartWorkerThreads();

	return hr;
}

void CDX9VideoProcessor::ReleaseVP()
{
	m_FrameStats.Reset();

	m_SrcSamples.Clear();
	m_DXVA2Samples.clear();
	m_pSrcVideoTexture.Release();

	m_D3D9_Src_Format = D3DFMT_UNKNOWN;
	m_D3D9_Src_Width = 0;
	m_D3D9_Src_Height = 0;

	m_pDXVA2_VP.Release();
}

void CDX9VideoProcessor::ReleaseDevice()
{
	ReleaseVP();

	m_pMemSurface.Release();
	m_pOSDTexture.Release();

	m_pDXVA2_VPService.Release();
	for (auto& shader : m_PixelShaders) {
		shader.pShader.Release();
	}

	m_pD3DDevEx.Release();
}

BOOL CDX9VideoProcessor::CheckInput(const D3DFORMAT d3dformat, const UINT width, const UINT height)
{
	BOOL ret = TRUE;

	if (d3dformat != m_D3D9_Src_Format || width != m_D3D9_Src_Width || height != m_D3D9_Src_Height) {

		if (m_VendorId != PCIV_INTEL && (d3dformat == D3DFMT_X8R8G8B8 || d3dformat == D3DFMT_A8R8G8B8)) {
			ret = InitializeTexVP(d3dformat, width, height);
		} else {
			ret = InitializeDXVA2VP(d3dformat, width, height);
		}

		if (ret) {
			m_D3D9_Src_Format = d3dformat;
			m_D3D9_Src_Width = width;
			m_D3D9_Src_Height = height;
		}
	}

	return ret;
}

BOOL CDX9VideoProcessor::InitializeDXVA2VP(const D3DFORMAT d3dformat, const UINT width, const UINT height)
{
	DLog("CDX9VideoProcessor::InitializeDXVA2VP: begin");

	ReleaseVP();

	HRESULT hr = S_OK;
	if (!m_pDXVA2_VPService) {
		return FALSE;
	}

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : Input surface: %s, %u x %u", D3DFormatToString(d3dformat), width, height);

	// Initialize the video descriptor.
	DXVA2_VideoDesc videodesc = {};
	videodesc.SampleWidth = width;
	videodesc.SampleHeight = height;
	//videodesc.SampleFormat.value = 0; // do not need to fill it here
	videodesc.SampleFormat.SampleFormat = m_bInterlaced ? DXVA2_SampleFieldInterleavedOddFirst : DXVA2_SampleProgressiveFrame;
	if (d3dformat == D3DFMT_X8R8G8B8 || d3dformat == D3DFMT_A8R8G8B8) {
		videodesc.Format = D3DFMT_YUY2; // hack
	} else {
		videodesc.Format = d3dformat;
	}
	videodesc.InputSampleFreq.Numerator = 60;
	videodesc.InputSampleFreq.Denominator = 1;
	videodesc.OutputFrameFreq.Numerator = 60;
	videodesc.OutputFrameFreq.Denominator = 1;

	// Query the video processor GUID.
	UINT count;
	GUID* guids = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorDeviceGuids(&videodesc, &count, &guids);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorDeviceGuids() failed with error %s", HR2Str(hr));
		return FALSE;
	}
	UINT NumRefSamples = 1;
	if (m_bInterlaced) {
		UINT PreferredDeintTech = DXVA2_DeinterlaceTech_EdgeFiltering // Intel
			| DXVA2_DeinterlaceTech_FieldAdaptive
			| DXVA2_DeinterlaceTech_PixelAdaptive // Nvidia, AMD
			| DXVA2_DeinterlaceTech_MotionVectorSteered;

		for (UINT i = 0; i < count; i++) {
			auto& devguid = guids[i];
			if (CreateDXVA2VPDevice(devguid, videodesc) && m_DXVA2VPcaps.DeinterlaceTechnology & PreferredDeintTech) {
				m_DXVA2VPGuid = devguid;
				break; // found!
			}
			m_pDXVA2_VP.Release();
		}

		if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcBobDevice, videodesc)) {
			m_DXVA2VPGuid = DXVA2_VideoProcBobDevice;
		}
	}

	CoTaskMemFree(guids);

	if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcProgressiveDevice, videodesc)) { // Progressive or fall-back for interlaced
		m_DXVA2VPGuid = DXVA2_VideoProcProgressiveDevice;
	}

	if (!m_pDXVA2_VP) {
		return FALSE;
	}

	NumRefSamples = 1 + m_DXVA2VPcaps.NumBackwardRefSamples + m_DXVA2VPcaps.NumForwardRefSamples;
	ASSERT(NumRefSamples <= MAX_DEINTERLACE_SURFACES);

	m_SrcSamples.Resize(NumRefSamples);
	m_DXVA2Samples.resize(NumRefSamples);

	for (unsigned i = 0; i < NumRefSamples; ++i) {
		hr = m_pDXVA2_VPService->CreateSurface(
			width,
			height,
			0,
			d3dformat,
			m_DXVA2VPcaps.InputPool,
			0,
			DXVA2_VideoProcessorRenderTarget,
			&m_SrcSamples.GetAt(i).pSrcSurface,
			nullptr
		);
		if (FAILED(hr)) {
			m_SrcSamples.Clear();
			m_DXVA2Samples.clear();
			return FALSE;
		}

		if (m_VendorId == PCIV_AMDATI) {
			// fix AMD driver bug, fill the surface in black
			m_pD3DDevEx->ColorFill(m_SrcSamples.GetAt(i).pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
		}

		m_DXVA2Samples[i].SampleFormat.value = m_srcExFmt.value;
		m_DXVA2Samples[i].SampleFormat.SampleFormat = DXVA2_SampleUnknown; // samples that are not used yet
		m_DXVA2Samples[i].SrcRect = { 0, 0, (LONG)width, (LONG)height };
		m_DXVA2Samples[i].PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();
	}

	m_D3D9_Src_Format = d3dformat;
	m_D3D9_Src_Width = width;
	m_D3D9_Src_Height = height;

	return TRUE;
}

BOOL CDX9VideoProcessor::CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc)
{
	if (!m_pDXVA2_VPService) {
		return FALSE;
	}

	HRESULT hr = S_OK;
	// Query the supported render target format.
	UINT i, count;
	D3DFORMAT* formats = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorRenderTargets(devguid, &videodesc, &count, &formats);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorRenderTargets() failed with error %s", HR2Str(hr));
		return FALSE;
	}
#ifdef _DEBUG
	{
		CStringW dbgstr = L"DXVA2-VP output formats:";
		for (UINT j = 0; j < count; j++) {
			dbgstr.AppendFormat(L"\n%s", D3DFormatToString(formats[j]));
		}
		DLog(dbgstr);
	}
#endif
	for (i = 0; i < count; i++) {
		if (formats[i] == D3DFMT_X8R8G8B8) {
			// Check only D3DFMT_X8R8G8B8. Other formats (D3DFMT_A2R10G10B10 and D3DFMT_A16B16G16R16F) are supported in spite of this list.
			break;
		}
	}
	CoTaskMemFree(formats);
	if (i >= count) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorRenderTargets() doesn't support D3DFMT_X8R8G8B8");
		return FALSE;
	}

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, m_VPOutputFmt, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorCaps() failed with error %s", HR2Str(hr));
		return FALSE;
	}
	// Check to see if the device is hardware device.
	if (!(m_DXVA2VPcaps.DeviceCaps & DXVA2_VPDev_HardwareDevice)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : The DXVA2 device isn't a hardware device");
		return FALSE;
	}
	// Check to see if the device supports all the VP operations we want.
	const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB | DXVA2_VideoProcess_StretchX | DXVA2_VideoProcess_StretchY;
	if ((m_DXVA2VPcaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : The DXVA2 device doesn't support the YUV2RGB & VP operations");
		return FALSE;
	}

	// Query ProcAmp ranges.
	for (i = 0; i < ARRAYSIZE(m_DXVA2ProcValueRange); i++) {
		if (m_DXVA2VPcaps.ProcAmpControlCaps & (1 << i)) {
			hr = m_pDXVA2_VPService->GetProcAmpRange(devguid, &videodesc, m_VPOutputFmt, 1 << i, &m_DXVA2ProcValueRange[i]);
			if (FAILED(hr)) {
				DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetProcAmpRange() failed with error %s", HR2Str(hr));
				return FALSE;
			}
		}
	}

	DXVA2_ValueRange range;
	// Query Noise Filter ranges.
	DXVA2_Fixed32 NFilterValues[6] = {};
	if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_NoiseFilter) {
		for (i = 0; i < 6u; i++) {
			if (S_OK == m_pDXVA2_VPService->GetFilterPropertyRange(devguid, &videodesc, m_VPOutputFmt, DXVA2_NoiseFilterLumaLevel + i, &range)) {
				NFilterValues[i] = range.DefaultValue;
			}
		}
	}
	// Query Detail Filter ranges.
	DXVA2_Fixed32 DFilterValues[6] = {};
	if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter) {
		for (i = 0; i < 6u; i++) {
			if (S_OK == m_pDXVA2_VPService->GetFilterPropertyRange(devguid, &videodesc, m_VPOutputFmt, DXVA2_DetailFilterLumaLevel + i, &range)) {
				DFilterValues[i] = range.DefaultValue;
			}
		}
	}

	ZeroMemory(&m_BltParams, sizeof(m_BltParams));
	m_BltParams.BackgroundColor              = { 128 * 0x100, 128 * 0x100, 16 * 0x100, 0xFFFF }; // black
	//m_BltParams.DestFormat.value           = 0; // output to RGB
	m_BltParams.DestFormat.SampleFormat      = DXVA2_SampleProgressiveFrame; // output to progressive RGB
	m_BltParams.ProcAmpValues.Brightness     = m_DXVA2ProcValueRange[0].DefaultValue;
	m_BltParams.ProcAmpValues.Contrast       = m_DXVA2ProcValueRange[1].DefaultValue;
	m_BltParams.ProcAmpValues.Hue            = m_DXVA2ProcValueRange[2].DefaultValue;
	m_BltParams.ProcAmpValues.Saturation     = m_DXVA2ProcValueRange[3].DefaultValue;
	m_BltParams.Alpha                        = DXVA2_Fixed32OpaqueAlpha();
	m_BltParams.NoiseFilterLuma.Level        = NFilterValues[0];
	m_BltParams.NoiseFilterLuma.Threshold    = NFilterValues[1];
	m_BltParams.NoiseFilterLuma.Radius       = NFilterValues[2];
	m_BltParams.NoiseFilterChroma.Level      = NFilterValues[3];
	m_BltParams.NoiseFilterChroma.Threshold  = NFilterValues[4];
	m_BltParams.NoiseFilterChroma.Radius     = NFilterValues[5];
	m_BltParams.DetailFilterLuma.Level       = DFilterValues[0];
	m_BltParams.DetailFilterLuma.Threshold   = DFilterValues[1];
	m_BltParams.DetailFilterLuma.Radius      = DFilterValues[2];
	m_BltParams.DetailFilterChroma.Level     = DFilterValues[3];
	m_BltParams.DetailFilterChroma.Threshold = DFilterValues[4];
	m_BltParams.DetailFilterChroma.Radius    = DFilterValues[5];

	// Finally create a video processor device.
	hr = m_pDXVA2_VPService->CreateVideoProcessor(devguid, &videodesc, m_VPOutputFmt, 0, &m_pDXVA2_VP);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : CreateVideoProcessor failed with error %s", HR2Str(hr));
		return FALSE;
	}

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : create %s processor ", CStringFromGUID(devguid));

	return TRUE;
}

BOOL CDX9VideoProcessor::InitializeTexVP(const D3DFORMAT d3dformat, const UINT width, const UINT height)
{
	// TODO: Add support for shaders
	ReleaseVP();

	if (!m_pDXVA2_VPService) {
		return FALSE;
	}
	HRESULT hr;

	m_pSrcVideoTexture.Release();
	hr = m_pD3DDevEx->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_pSrcVideoTexture, nullptr);
	if (FAILED(hr)) {
		return FALSE;
	}

	m_SrcSamples.Resize(1);
	hr = m_pSrcVideoTexture->GetSurfaceLevel(0, &m_SrcSamples.GetAt(0).pSrcSurface);
	if (FAILED(hr)) {
		m_SrcSamples.Clear();
		return FALSE;
	}

	if (m_VendorId == PCIV_AMDATI) {
		// fix AMD driver bug, fill the surface in black
		m_pD3DDevEx->ColorFill(m_SrcSamples.GetAt(0).pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
	}

	for (auto& shader: m_PixelShaders) {
		if (!shader.pShader) {
			HRESULT hr = CreateShaderFromResource(&shader.pShader, shader.resid);
			ASSERT(S_OK == hr);
		}
	}

	return TRUE;
}

HRESULT CDX9VideoProcessor::CreateShaderFromResource(IDirect3DPixelShader9** ppPixelShader, UINT resid)
{
	if (!m_pD3DDevEx || !ppPixelShader) {
		return E_POINTER;
	}

	HRSRC hrsrc = FindResourceW(nullptr, MAKEINTRESOURCEW(resid), L"SHADER");
	if (!hrsrc) {
		return E_INVALIDARG;
	}
	HGLOBAL hGlobal = LoadResource(nullptr, hrsrc);
	if (!hGlobal) {
		return E_FAIL;
	}
	DWORD size = SizeofResource(nullptr, hrsrc);
	if (size < 4) {
		return E_FAIL;
	}

	return m_pD3DDevEx->CreatePixelShader((const DWORD*)LockResource(hGlobal), ppPixelShader);
}


void CDX9VideoProcessor::StartWorkerThreads()
{
	DWORD dwThreadId;

	m_hEvtQuit = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	m_hSyncThread = ::CreateThread(nullptr, 0, SyncThreadStatic, (LPVOID)this, 0, &dwThreadId);
}

void CDX9VideoProcessor::StopWorkerThreads()
{
	SetEvent(m_hEvtQuit);

	if (m_hSyncThread && WaitForSingleObject(m_hSyncThread, 1000) == WAIT_TIMEOUT) {
		ASSERT(FALSE);
		TerminateThread(m_hSyncThread, 0xDEAD);
	}

	SAFE_CLOSE_HANDLE(m_hSyncThread);
}

DWORD WINAPI CDX9VideoProcessor::SyncThreadStatic(LPVOID lpParam)
{
	CDX9VideoProcessor* pThis = (CDX9VideoProcessor*)lpParam;
	pThis->SyncThread();
	return 0;
}

void CDX9VideoProcessor::SyncThread()
{
	struct {
		uint64_t tick;
		UINT scanline;
	} ScanLines[61] = {};
	unsigned ScanLinePos = 0;
	bool filled = false;
	UINT prevSL = UINT_MAX;
	UINT ScanlinesPerFrame = 0;

	bool bQuit = false;

	while (!bQuit) {
		DWORD dwObject = WaitForSingleObject(m_hEvtQuit, 2);
		switch (dwObject) {
		case WAIT_OBJECT_0:
			bQuit = true;
			break;
		case WAIT_TIMEOUT:
			// Do our stuff
		{
			if (!ScanlinesPerFrame) {
				D3DRASTER_STATUS rasterStatus;
				if (S_OK == m_pD3DDevEx->GetRasterStatus(0, &rasterStatus)) {
					while (rasterStatus.ScanLine == 0) { // skip zero scanline with unknown start time
						Sleep(1);
						m_pD3DDevEx->GetRasterStatus(0, &rasterStatus);
					}
					while (rasterStatus.ScanLine != 0) { // find new zero scanline
						m_pD3DDevEx->GetRasterStatus(0, &rasterStatus);
					}
					uint64_t tick0 = GetPreciseTick();
					while (rasterStatus.ScanLine == 0) {
						m_pD3DDevEx->GetRasterStatus(0, &rasterStatus);
					}
					uint64_t tick1 = GetPreciseTick();

					Sleep(1);
					prevSL = 0;
					while (rasterStatus.ScanLine != 0) {
						prevSL = rasterStatus.ScanLine;
						m_pD3DDevEx->GetRasterStatus(0, &rasterStatus);
					}
					uint64_t tickLast = GetPreciseTick();

					auto t = tickLast - tick1;
					ScanlinesPerFrame = (prevSL * (tickLast - tick0) + t/2) / t;

					DLog(L"Detected ScanlinesPerFrame = %u", ScanlinesPerFrame);
				}
			}
			else {
				D3DRASTER_STATUS rasterStatus;
				if (S_OK == m_pD3DDevEx->GetRasterStatus(0, &rasterStatus)) {
					uint64_t tick = GetPreciseTick();
					if (rasterStatus.ScanLine) { // ignore the zero scan line, it coincides with VBlanc and therefore is very long in time
						if (rasterStatus.ScanLine < prevSL) {
							ScanLines[ScanLinePos].tick = tick;
							ScanLines[ScanLinePos].scanline = rasterStatus.ScanLine;
							UINT lastpos = ScanLinePos++;
							if (ScanLinePos >= std::size(ScanLines)) {
								ScanLinePos = 0;
								filled = true;
							}

							double refreshRate = GetPreciseTicksPerSecond();
							if (filled) {
								refreshRate *= ScanlinesPerFrame * ((UINT)std::size(ScanLines) - 1) + ScanLines[lastpos].scanline - ScanLines[ScanLinePos].scanline;
								refreshRate /= ScanlinesPerFrame * (ScanLines[lastpos].tick - ScanLines[ScanLinePos].tick);
							} else {
								refreshRate *= ScanlinesPerFrame * ScanLinePos + ScanLines[lastpos].scanline - ScanLines[0].scanline;
								refreshRate /= ScanlinesPerFrame * (ScanLines[lastpos].tick - ScanLines[0].tick);
							}

							{
								CAutoLock Lock(&m_RefreshRateLock);
								m_DetectedRefreshRate = refreshRate;
							}
						}
						prevSL = rasterStatus.ScanLine;
					}
				}
			}
		}
			break;
		}
	}
}

BOOL CDX9VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	if (!FmtConvParams) {
		return FALSE;
	}

	m_srcSubType = pmt->subtype;
	UINT biSizeImage = 0;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcWidth = vih2->bmiHeader.biWidth;
		m_srcHeight = labs(vih2->bmiHeader.biHeight);
		biSizeImage = vih2->bmiHeader.biSizeImage;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (!FmtConvParams->bRGB && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
			m_srcExFmt.value = vih2->dwControlFlags;
			m_srcExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		} else {
			m_srcExFmt.value = 0; // ignore color info for RGB
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
	}
	else if (pmt->formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
		m_srcRect = vih->rcSource;
		m_trgRect = vih->rcTarget;
		m_srcWidth = vih->bmiHeader.biWidth;
		m_srcHeight = labs(vih->bmiHeader.biHeight);
		biSizeImage = vih->bmiHeader.biSizeImage;
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_srcExFmt.value = 0;
		m_bInterlaced = 0;
	}
	else {
		return FALSE;
	}

	if (m_srcRect.IsRectNull() && m_trgRect.IsRectNull()) {
		// Hmm
		m_srcRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
		m_trgRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
	}

	m_srcD3DFormat = FmtConvParams->D3DFormat;
	m_pConvertFn   = FmtConvParams->Func;
	m_srcPitch     = biSizeImage * 2 / (m_srcHeight * FmtConvParams->PitchCoeff);
	if (m_srcSubType == MEDIASUBTYPE_NV12 && biSizeImage % 4) {
		m_srcPitch = ALIGN(m_srcPitch, 4);
	}
	else if (m_srcSubType == MEDIASUBTYPE_P010) {
		m_srcPitch &= ~1u;
	}

	if (!CheckInput(m_srcD3DFormat, m_srcWidth, m_srcHeight)) {
		return FALSE;
	}

	return TRUE;
}

void CDX9VideoProcessor::Start()
{
	m_FrameStats.Reset();
}

HRESULT CDX9VideoProcessor::CopySample(IMediaSample* pSample)
{
	HRESULT hr = S_OK;

	// Get frame type
	m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame; // Progressive
	if (m_bInterlaced) {
		if (CComQIPtr<IMediaSample2> pMS2 = pSample) {
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
				m_CurrentSampleFmt = DXVA2_SampleFieldInterleavedOddFirst;      // Bottom-field first
				if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
					m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;          // Progressive
				}
				else if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
					m_CurrentSampleFmt = DXVA2_SampleFieldInterleavedEvenFirst; // Top-field first
				}
			}
		}
	}

	m_FieldDrawn = 0;

	if (CComQIPtr<IMFGetService> pService = pSample) {
		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			D3DSURFACE_DESC desc;
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr)) {
				return hr;
			}
			if (!CheckInput(desc.Format, desc.Width, desc.Height)) {
				return E_FAIL;
			}

#ifdef _DEBUG
			if (m_FrameStats.GetFrames() < 2) {
				CComPtr<IDirect3DDevice9> pD3DDev;
				pSurface->GetDevice(&pD3DDev);
				if (pD3DDev != m_pD3DDevEx) {
					DLog("WARNING: Different adapters for decoding and processing! StretchRect will fail.");
				}
			}
#endif
			m_SrcSamples.Next();
			hr = m_pD3DDevEx->StretchRect(pSurface, nullptr, m_SrcSamples.Get().pSrcSurface, nullptr, D3DTEXF_NONE);
			if (FAILED(hr)) {
				// sometimes StretchRect does not work on non-primary display on Intel GPU
				D3DLOCKED_RECT lr_dst;
				hr = m_SrcSamples.Get().pSrcSurface->LockRect(&lr_dst, nullptr, D3DLOCK_NOSYSLOCK);
				if (S_OK == hr) {
					D3DLOCKED_RECT lr_src;
					hr = pSurface->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
					if (S_OK == hr) {
						memcpy((BYTE*)lr_dst.pBits, (BYTE*)lr_src.pBits, lr_src.Pitch * desc.Height * 3 / 2);
						hr = pSurface->UnlockRect();
					}
					hr = m_SrcSamples.Get().pSrcSurface->UnlockRect();
				}
			}
		}
	}
	else {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			if (!CheckInput(m_srcD3DFormat, m_srcWidth, m_srcHeight)) {
				return E_FAIL;
			}

			m_SrcSamples.Next();
			D3DLOCKED_RECT lr;
			hr = m_SrcSamples.Get().pSrcSurface->LockRect(&lr, nullptr, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr)) {
				return hr;
			}

			ASSERT(m_pConvertFn);
			m_pConvertFn(m_srcHeight, (BYTE*)lr.pBits, lr.Pitch, data, m_srcPitch);

			hr = m_SrcSamples.Get().pSrcSurface->UnlockRect();
		}
	}

	const REFERENCE_TIME start_100ns = m_FrameStats.GetFrames() * 170000i64;
	const REFERENCE_TIME end_100ns = start_100ns + 170000i64;
	m_SrcSamples.Get().Start = start_100ns;
	m_SrcSamples.Get().End = end_100ns;
	m_SrcSamples.Get().SampleFormat = m_CurrentSampleFmt;

	for (unsigned i = 0; i < m_DXVA2Samples.size(); i++) {
		auto & SrcSample = m_SrcSamples.GetAt(i);
		m_DXVA2Samples[i].Start = SrcSample.Start;
		m_DXVA2Samples[i].End   = SrcSample.End;
		m_DXVA2Samples[i].SampleFormat.SampleFormat = SrcSample.SampleFormat;
		m_DXVA2Samples[i].SrcSurface = SrcSample.pSrcSurface;
	}

	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);
	m_FrameStats.Add(rtStart);

	return hr;
}

HRESULT CDX9VideoProcessor::Render(int field)
{
	if (m_SrcSamples.Empty()) return E_POINTER;

	CRefTime rtClock;
	REFERENCE_TIME rtFrame = m_FrameStats.GetTime();
	REFERENCE_TIME rtFrameDur = m_FrameStats.GetAverageFrameDuration();

	if (field) {
		m_pFilter->StreamTime(rtClock);

		if (rtFrameDur > 2 && rtFrame + rtFrameDur < rtClock) {
			return S_FALSE; // skip this frame
		}
		m_FieldDrawn = field;
	}

	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	if (!m_videoRect.IsRectEmpty()) {
		CRect rSrcRect(m_srcRect);
		CRect rDstRect(m_videoRect);
		D3DSURFACE_DESC desc = {};
		if (S_OK == pBackBuffer->GetDesc(&desc)) {
			ClipToSurface(desc.Width, desc.Height, rSrcRect, rDstRect);
		}

		if (m_pDXVA2_VP) {
			hr = ProcessDXVA2(pBackBuffer, rSrcRect, rDstRect, m_FieldDrawn == 2);
		} else {
			hr = ProcessTex(pBackBuffer, rSrcRect, rDstRect);
		}

		if (S_OK == hr && m_bShowStats) {
			hr = DrawStats();
		}
	}

	hr = m_pD3DDevEx->EndScene();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);

	m_pFilter->StreamTime(rtClock);
	if (m_FieldDrawn == 2) {
		rtFrame += rtFrameDur / 2;
	}
	m_SyncOffsetMS = std::round((double)(rtClock - rtFrame) / (UNITS / 1000));

	return hr;
}

HRESULT CDX9VideoProcessor::FillBlack()
{
	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	hr = m_pD3DDevEx->EndScene();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);

	return hr;
}

void CDX9VideoProcessor::StopInputBuffer()
{
	for (unsigned i = 0; i < m_SrcSamples.Size(); i++) {
		auto & SrcSample = m_SrcSamples.GetAt(i);
		SrcSample.Start = 0;
		SrcSample.End   = 0;
		SrcSample.SampleFormat = DXVA2_SampleUnknown;
		if (m_VendorId == PCIV_AMDATI) {
			m_pD3DDevEx->ColorFill(SrcSample.pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
		}
	}
	for (auto& DXVA2Sample : m_DXVA2Samples) {
		DXVA2Sample.Start = 0;
		DXVA2Sample.End   = 0;
		DXVA2Sample.SampleFormat.SampleFormat = DXVA2_SampleUnknown;
	}
}

HRESULT CDX9VideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	*pWidth = m_srcWidth;
	*pHeight = m_srcHeight;

	return S_OK;
}

HRESULT CDX9VideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	*plAspectX = m_srcAspectRatioX;
	*plAspectY = m_srcAspectRatioY;

	return S_OK;
}

HRESULT CDX9VideoProcessor::GetFrameInfo(VRFrameInfo* pFrameInfo)
{
	CheckPointer(pFrameInfo, E_POINTER);

	pFrameInfo->Width = m_srcWidth;
	pFrameInfo->Height = m_srcHeight;
	pFrameInfo->D3dFormat = m_srcD3DFormat;
	pFrameInfo->ExtFormat.value = m_srcExFmt.value;

	return S_OK;
}

HRESULT CDX9VideoProcessor::GetAdapterDecription(CStringW& str)
{
	str = m_strAdapterDescription;
	return S_OK;
}

HRESULT CDX9VideoProcessor::GetDXVA2VPCaps(DXVA2_VideoProcessorCaps* pDXVA2VPCaps)
{
	CheckPointer(pDXVA2VPCaps, E_POINTER);
	memcpy(pDXVA2VPCaps, &m_DXVA2VPcaps, sizeof(DXVA2_VideoProcessorCaps));
	return S_OK;
}

HRESULT CDX9VideoProcessor::ProcessDXVA2(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second)
{
	// https://msdn.microsoft.com/en-us/library/cc307964(v=vs.85).aspx
	HRESULT hr = S_OK;
	ASSERT(m_SrcSamples.Size() == m_DXVA2Samples.size());

	// Initialize VPBlt parameters
	if (second) {
		m_BltParams.TargetFrame = (m_SrcSamples.Get().Start + m_SrcSamples.Get().End) / 2;
	} else {
		m_BltParams.TargetFrame = m_SrcSamples.Get().Start;
	}
	m_BltParams.TargetRect = rDstRect;
	m_BltParams.ConstrictionSize.cx = rDstRect.Width();
	m_BltParams.ConstrictionSize.cy = rDstRect.Height();

	// Initialize main stream video samples
	for (unsigned i = 0; i < m_DXVA2Samples.size(); i++) {
		auto & SrcSample = m_SrcSamples.GetAt(i);
		m_DXVA2Samples[i].SrcRect = rSrcRect;
		m_DXVA2Samples[i].DstRect = rDstRect;
	}

	hr = m_pDXVA2_VP->VideoProcessBlt(pRenderTarget, &m_BltParams, m_DXVA2Samples.data(), m_DXVA2Samples.size(), nullptr);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::ProcessDXVA2 : VideoProcessBlt() failed with error %s", HR2Str(hr));
	}

	return hr;
}

HRESULT CDX9VideoProcessor::ProcessTex(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect)
{
	HRESULT hr = S_OK;
	const int w1 = rSrcRect.Width();
	const int h1 = rSrcRect.Height();
	const int w2 = rDstRect.Width();
	const int h2 = rDstRect.Height();

	const int resizerX = (w1 == w2) ? -1 : (w1 > 2 * w2) ? shader_downscaler_hamming_x : shader_catmull_x;
	const int resizerY = (h1 == h2) ? -1 : (h1 > 2 * h2) ? shader_downscaler_hamming_y : shader_catmull_y;

	if (resizerX < 0 && resizerY < 0) {
		// no resize
		return m_pD3DDevEx->StretchRect(m_SrcSamples.GetAt(0).pSrcSurface, rSrcRect, pRenderTarget, rDstRect, D3DTEXF_POINT);
		// alt
		// return TextureResize(m_pSrcVideoTexture, rSrcRect, rDstRect, D3DTEXF_POINT);
		// alt for sysmem surface?
		// return m_pD3DDevEx->UpdateSurface(m_SrcSamples.GetAt(0).pSrcSurface, rSrcRect, pRenderTarget, &rDstRect.TopLeft())
	}

	if (resizerX >= 0 && resizerY >= 0) {
		// two pass resize

		// check intermediate texture
		UINT texWidth = w2;
		UINT texHeight = h1;

		if (m_TexResize.pTexture) {
			if (texWidth != m_TexResize.Width || texHeight != m_TexResize.Height) {
				m_TexResize.Release(); // need new texture
			}
		}

		if (!m_TexResize.pTexture) {
			// use only float textures here
			hr = m_pD3DDevEx->CreateTexture(texWidth, texHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &m_TexResize.pTexture, nullptr);
			if (FAILED(hr) || FAILED(m_TexResize.Update())) {
				m_TexResize.Release();
				DLog(L"CDX9VideoProcessor::ProcessTex : filed create m_TexResize");
				return TextureResize(m_pSrcVideoTexture, rSrcRect, rDstRect, D3DTEXF_LINEAR);
			}
		}

		CRect resizeRect(0, 0, m_TexResize.Width, m_TexResize.Height);

		// remember current RenderTarget
		CComPtr<IDirect3DSurface9> pRenderTarget;
		hr = m_pD3DDevEx->GetRenderTarget(0, &pRenderTarget);
		// set temp RenderTarget
		hr = m_pD3DDevEx->SetRenderTarget(0, m_TexResize.pSurface);

		// resize width
		hr = TextureResizeShader(m_pSrcVideoTexture, rSrcRect, resizeRect, m_PixelShaders[resizerX].pShader);

		// restore current RenderTarget
		hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);

		// resize height
		hr = TextureResizeShader(m_TexResize.pTexture, resizeRect, rDstRect, m_PixelShaders[resizerY].pShader);
	}
	else if (resizerX >= 0) {
		// one pass resize for width
		hr = TextureResizeShader(m_pSrcVideoTexture, rSrcRect, rDstRect, m_PixelShaders[resizerX].pShader);
	}
	else { // resizerY >= 0
		// one pass resize for height
		hr = TextureResizeShader(m_pSrcVideoTexture, rSrcRect, rDstRect, m_PixelShaders[resizerY].pShader);
	}

	//HRESULT hr = TextureResize(m_pSrcVideoTexture, rSrcRect, rDstRect, D3DTEXF_LINEAR);

	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::ProcessTex : failed with error %s", HR2Str(hr));
	}

	return hr;
}

HRESULT CDX9VideoProcessor::TextureResize(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, D3DTEXTUREFILTERTYPE filter)
{
	HRESULT hr;

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	float dx = 1.0f / desc.Width;
	float dy = 1.0f / desc.Height;

	MYD3DVERTEX<1> v[] = {
		{(float)destRect.left - 0.5f,  (float)destRect.top - 0.5f,    0.5f, 2.0f, {srcRect.left  * dx, srcRect.top    * dy} },
		{(float)destRect.right - 0.5f, (float)destRect.top - 0.5f,    0.5f, 2.0f, {srcRect.right * dx, srcRect.top    * dy} },
		{(float)destRect.left - 0.5f,  (float)destRect.bottom - 0.5f, 0.5f, 2.0f, {srcRect.left  * dx, srcRect.bottom * dy} },
		{(float)destRect.right - 0.5f, (float)destRect.bottom - 0.5f, 0.5f, 2.0f, {srcRect.right * dx, srcRect.bottom * dy} },
	};

	hr = m_pD3DDevEx->SetTexture(0, pTexture);
	hr = TextureBlt(m_pD3DDevEx, v, filter);

	return hr;
}

HRESULT CDX9VideoProcessor::TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, IDirect3DPixelShader9* pShader)
{
	HRESULT hr = S_OK;

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	const float dx = 1.0f / desc.Width;
	const float dy = 1.0f / desc.Height;

	const float scale_x = (float)srcRect.Width() / destRect.Width();
	const float scale_y = (float)srcRect.Height() / destRect.Height();
	const float steps_x = floor(scale_x + 0.5f);
	const float steps_y = floor(scale_y + 0.5f);

	const float tx0 = (float)srcRect.left - 0.5f;
	const float ty0 = (float)srcRect.top - 0.5f;
	const float tx1 = (float)srcRect.right - 0.5f;
	const float ty1 = (float)srcRect.bottom - 0.5f;

	MYD3DVERTEX<1> v[] = {
		{(float)destRect.left - 0.5f,  (float)destRect.top - 0.5f,    0.5f, 2.0f, { tx0, ty0 } },
		{(float)destRect.right - 0.5f, (float)destRect.top - 0.5f,    0.5f, 2.0f, { tx1, ty0 } },
		{(float)destRect.left - 0.5f,  (float)destRect.bottom - 0.5f, 0.5f, 2.0f, { tx0, ty1 } },
		{(float)destRect.right - 0.5f, (float)destRect.bottom - 0.5f, 0.5f, 2.0f, { tx1, ty1 } },
	};

	float fConstData[][4] = {
		{ dx, dy, 0, 0 },
		{ steps_x, steps_y, 0, 0 },
		{ scale_x, scale_y, 0, 0 },
	};
	hr = m_pD3DDevEx->SetPixelShaderConstantF(0, (float*)fConstData, _countof(fConstData));
	hr = m_pD3DDevEx->SetPixelShader(pShader);

	hr = m_pD3DDevEx->SetTexture(0, pTexture);
	hr = TextureBlt(m_pD3DDevEx, v, D3DTEXF_POINT);
	m_pD3DDevEx->SetPixelShader(nullptr);

	return hr;
}

HRESULT CDX9VideoProcessor::AlphaBlt(RECT* pSrc, RECT* pDst, IDirect3DTexture9* pTexture)
{
	if (!pSrc || !pDst) {
		return E_POINTER;
	}

	CRect src(*pSrc), dst(*pDst);

	HRESULT hr;

	D3DSURFACE_DESC d3dsd;
	if (FAILED(pTexture->GetLevelDesc(0, &d3dsd))) {
		return E_FAIL;
	}

	float w = (float)d3dsd.Width;
	float h = (float)d3dsd.Height;

	struct {
		float x, y, z, rhw;
		float tu, tv;
	}
	pVertices[] = {
		{ (float)dst.left,  (float)dst.top,    0.5f, 2.0f, (float)src.left / w,  (float)src.top / h },
		{ (float)dst.right, (float)dst.top,    0.5f, 2.0f, (float)src.right / w, (float)src.top / h },
		{ (float)dst.left,  (float)dst.bottom, 0.5f, 2.0f, (float)src.left / w,  (float)src.bottom / h },
		{ (float)dst.right, (float)dst.bottom, 0.5f, 2.0f, (float)src.right / w, (float)src.bottom / h },
	};

	for (auto& pVertice : pVertices) {
		pVertice.x -= 0.5f;
		pVertice.y -= 0.5f;
	}

	hr = m_pD3DDevEx->SetTexture(0, pTexture);

	// GetRenderState fails for devices created with D3DCREATE_PUREDEVICE
	// so we need to provide default values in case GetRenderState fails
	DWORD abe, sb, db;
	if (FAILED(m_pD3DDevEx->GetRenderState(D3DRS_ALPHABLENDENABLE, &abe)))
		abe = FALSE;
	if (FAILED(m_pD3DDevEx->GetRenderState(D3DRS_SRCBLEND, &sb)))
		sb = D3DBLEND_ONE;
	if (FAILED(m_pD3DDevEx->GetRenderState(D3DRS_DESTBLEND, &db)))
		db = D3DBLEND_ZERO;

	hr = m_pD3DDevEx->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_LIGHTING, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_ZENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); // pre-multiplied src and ...
	hr = m_pD3DDevEx->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA); // ... inverse alpha channel for dst

	hr = m_pD3DDevEx->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	hr = m_pD3DDevEx->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	hr = m_pD3DDevEx->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	hr = m_pD3DDevEx->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	hr = m_pD3DDevEx->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pVertices, sizeof(pVertices[0]));

	m_pD3DDevEx->SetTexture(0, nullptr);

	m_pD3DDevEx->SetRenderState(D3DRS_ALPHABLENDENABLE, abe);
	m_pD3DDevEx->SetRenderState(D3DRS_SRCBLEND, sb);
	m_pD3DDevEx->SetRenderState(D3DRS_DESTBLEND, db);

	return S_OK;
}

HRESULT CDX9VideoProcessor::DrawStats()
{
	if (!m_pMemSurface) {
		return E_ABORT;
	}

	D3DSURFACE_DESC desc = {};
	HRESULT hr = m_pMemSurface->GetDesc(&desc);

	CStringW str = L"Direct3D 9Ex";
	str.AppendFormat(L"\nFrame rate   : %7.03f", m_FrameStats.GetAverageFps());
	if (m_CurrentSampleFmt >= DXVA2_SampleFieldInterleavedEvenFirst && m_CurrentSampleFmt <= DXVA2_SampleFieldSingleOdd) {
		str.Append(L" i");
	}
	str.AppendFormat(L"\nInput format : %s", D3DFormatToString(m_srcD3DFormat));
	str.AppendFormat(L"\nVP output fmt: %s", D3DFormatToString(m_VPOutputFmt));
	str.AppendFormat(L"\nSync offset  :%+4d ms", m_SyncOffsetMS);

	{
		CAutoLock Lock(&m_RefreshRateLock);
		str.AppendFormat(L"\nRefresh Rate : %7.03f Hz", m_DetectedRefreshRate);
	}

	HDC hdc;
	if (S_OK == m_pMemSurface->GetDC(&hdc)) {
		using namespace Gdiplus;

		Graphics   graphics(hdc);
		FontFamily fontFamily(L"Consolas");
		Font       font(&fontFamily, 20, FontStyleRegular, UnitPixel);
		PointF     pointF(5.0f, 5.0f);
		SolidBrush solidBrush(Color(255, 255, 255, 224));

		Status status = Gdiplus::Ok;

		status = graphics.Clear(Color(224, 0, 0, 0));
		status = graphics.DrawString(str, -1, &font, pointF, &solidBrush);
		graphics.Flush();

		m_pMemSurface->ReleaseDC(hdc);
	}
	

	CComPtr<IDirect3DSurface9> pOSDSurface;
	hr = m_pOSDTexture->GetSurfaceLevel(0, &pOSDSurface);
	if (S_OK == hr) {
		//hr = m_pD3DDevEx->StretchRect(m_pDCSurface, nullptr, pOSDSurface, nullptr, D3DTEXF_POINT);
		hr = m_pD3DDevEx->UpdateSurface(m_pMemSurface, nullptr, pOSDSurface, nullptr);
	}

	hr = AlphaBlt(CRect(0, 0, STATS_W, STATS_H), CRect(10, 10, STATS_W + 10, STATS_H + 10), m_pOSDTexture);

	return hr;
}

// IUnknown
STDMETHODIMP CDX9VideoProcessor::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv) {
		return E_POINTER;
	}
	if (riid == IID_IUnknown) {
		*ppv = static_cast<IUnknown*>(static_cast<IMFVideoProcessor*>(this));
	}
	else if (riid == IID_IMFVideoProcessor) {
		*ppv = static_cast<IMFVideoProcessor*>(this);
	}
	else {
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CDX9VideoProcessor::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CDX9VideoProcessor::Release()
{
	ULONG uCount = InterlockedDecrement(&m_nRefCount);
	if (uCount == 0) {
		delete this;
	}
	// For thread safety, return a temporary variable.
	return uCount;
}

// IMFVideoProcessor

STDMETHODIMP CDX9VideoProcessor::GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange)
{
	CheckPointer(m_pDXVA2_VP, MF_E_INVALIDREQUEST);
	CheckPointer(pPropRange, E_POINTER);
	switch (dwProperty) {
	case DXVA2_ProcAmp_Brightness: memcpy(pPropRange, &m_DXVA2ProcValueRange[0], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Contrast:   memcpy(pPropRange, &m_DXVA2ProcValueRange[1], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Hue:        memcpy(pPropRange, &m_DXVA2ProcValueRange[2], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Saturation: memcpy(pPropRange, &m_DXVA2ProcValueRange[3], sizeof(DXVA2_ValueRange)); break;
	default:
		return E_INVALIDARG;
	}
	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values)
{
	CheckPointer(m_pDXVA2_VP, MF_E_INVALIDREQUEST);
	CheckPointer(Values, E_POINTER);
	if (dwFlags&DXVA2_ProcAmp_Brightness) { Values->Brightness = m_BltParams.ProcAmpValues.Brightness; }
	if (dwFlags&DXVA2_ProcAmp_Contrast)   { Values->Contrast   = m_BltParams.ProcAmpValues.Contrast; }
	if (dwFlags&DXVA2_ProcAmp_Hue)        { Values->Hue        = m_BltParams.ProcAmpValues.Hue; }
	if (dwFlags&DXVA2_ProcAmp_Saturation) { Values->Saturation = m_BltParams.ProcAmpValues.Saturation; }
	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(m_pDXVA2_VP, MF_E_INVALIDREQUEST);
	CheckPointer(pValues, E_POINTER);
	if (dwFlags&DXVA2_ProcAmp_Brightness) {
		m_BltParams.ProcAmpValues.Brightness.ll = std::clamp(pValues->Brightness.ll, m_DXVA2ProcValueRange[0].MinValue.ll, m_DXVA2ProcValueRange[0].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Contrast) {
		m_BltParams.ProcAmpValues.Contrast.ll = std::clamp(pValues->Contrast.ll, m_DXVA2ProcValueRange[1].MinValue.ll, m_DXVA2ProcValueRange[1].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Hue) {
		m_BltParams.ProcAmpValues.Hue.ll = std::clamp(pValues->Hue.ll, m_DXVA2ProcValueRange[2].MinValue.ll, m_DXVA2ProcValueRange[2].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Saturation) {
		m_BltParams.ProcAmpValues.Saturation.ll = std::clamp(pValues->Saturation.ll, m_DXVA2ProcValueRange[3].MinValue.ll, m_DXVA2ProcValueRange[3].MaxValue.ll);
	}

	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::GetBackgroundColor(COLORREF *lpClrBkg)
{
	CheckPointer(m_pDXVA2_VP, MF_E_INVALIDREQUEST);
	CheckPointer(lpClrBkg, E_POINTER);
	*lpClrBkg = RGB(0, 0, 0);
	return S_OK;
}
