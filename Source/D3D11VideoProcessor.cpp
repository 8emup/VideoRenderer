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
#include <uuids.h>
#include <dvdmedia.h>
#include <Mferror.h>
#include <Mfidl.h>
#include "D3D11VideoProcessor.h"

static const struct FormatEntry {
	GUID            Subtype;
	DXGI_FORMAT     DXGIFormat;
}
s_DXGIFormatMapping[] = {
	{ MEDIASUBTYPE_RGB32,   DXGI_FORMAT_B8G8R8X8_UNORM },
	{ MEDIASUBTYPE_ARGB32,  DXGI_FORMAT_R8G8B8A8_UNORM },
	{ MEDIASUBTYPE_AYUV,    DXGI_FORMAT_AYUV },
	{ MEDIASUBTYPE_YUY2,    DXGI_FORMAT_YUY2 },
	{ MEDIASUBTYPE_NV12,    DXGI_FORMAT_NV12 },
	{ MEDIASUBTYPE_P010,    DXGI_FORMAT_P010 },
};

DXGI_FORMAT MediaSubtype2DXGIFormat(GUID subtype)
{
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
	for (unsigned i = 0; i < ARRAYSIZE(s_DXGIFormatMapping); i++) {
		const FormatEntry& e = s_DXGIFormatMapping[i];
		if (e.Subtype == subtype) {
			dxgiFormat = e.DXGIFormat;
			break;
		}
	}
	return dxgiFormat;
}

// CD3D11VideoProcessor

CD3D11VideoProcessor::CD3D11VideoProcessor()
{
	m_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
	if (!m_hD3D11Lib) {
		return;
	}

	HRESULT(WINAPI *pfnD3D11CreateDevice)(
		IDXGIAdapter            *pAdapter,
		D3D_DRIVER_TYPE         DriverType,
		HMODULE                 Software,
		UINT                    Flags,
		const D3D_FEATURE_LEVEL *pFeatureLevels,
		UINT                    FeatureLevels,
		UINT                    SDKVersion,
		ID3D11Device            **ppDevice,
		D3D_FEATURE_LEVEL       *pFeatureLevel,
		ID3D11DeviceContext     **ppImmediateContext
	);

	(FARPROC &)pfnD3D11CreateDevice = GetProcAddress(m_hD3D11Lib, "D3D11CreateDevice");
	if (!pfnD3D11CreateDevice) {
		return;
	}

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
	D3D_FEATURE_LEVEL featurelevel;

	HRESULT hr = pfnD3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_DEBUG, // need SDK for Windows 8
#else
		0,
#endif
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&m_pDevice,
		&featurelevel,
		nullptr);
	if (FAILED(hr)) {
		return;
	}

	hr = m_pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
	if (FAILED(hr)) {
		// TODO DLog here
		return; // need Windows 8+
	}
}

CD3D11VideoProcessor::~CD3D11VideoProcessor()
{
	m_pSrcTexture2D.Release();
	m_pVideoProcessor.Release();
	m_pVideoDevice.Release();
	m_pDevice.Release();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CheckInputMediaType(ID3D11VideoDevice* pVideoDevice, const GUID subtype, const UINT width, const UINT height)
{
	DXGI_FORMAT dxgiFormat = MediaSubtype2DXGIFormat(subtype);
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	//Check if the format is supported
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
	ContentDesc.InputWidth = width;
	ContentDesc.InputHeight = height;
	ContentDesc.OutputWidth = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.InputFrameRate.Numerator = 30000;
	ContentDesc.InputFrameRate.Denominator = 1001;
	ContentDesc.OutputFrameRate.Numerator = 30000;
	ContentDesc.OutputFrameRate.Denominator = 1001;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcessorEnum;
	hr = pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcessorEnum);
	if (FAILED(hr)) {
		return hr;
	}

	UINT uiFlags;
	hr = pVideoProcessorEnum->CheckVideoProcessorFormat(dxgiFormat, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	return hr;
}

HRESULT CD3D11VideoProcessor::IsMediaTypeSupported(const GUID subtype, const UINT width, const UINT height)
{
	if (!m_pVideoDevice) {
		return E_FAIL;
	}

	return CheckInputMediaType(m_pVideoDevice, subtype, width, height);
}


HRESULT CD3D11VideoProcessor::Initialize(const GUID subtype, const UINT width, const UINT height)
{
	if (!m_pVideoDevice) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	hr = CheckInputMediaType(m_pVideoDevice, subtype, width, height);
	if (FAILED(hr)) {
		return hr;
	}
	DXGI_FORMAT dxgiFormat = MediaSubtype2DXGIFormat(subtype);

	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcessorEnum;

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
	ContentDesc.InputWidth = width;
	ContentDesc.InputHeight = height;
	ContentDesc.OutputWidth = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	hr = m_pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcessorEnum);
	if (FAILED(hr)) {
		return hr;
	}

	UINT uiFlags;
	DXGI_FORMAT VP_Output_Format = DXGI_FORMAT_B8G8R8X8_UNORM;

	hr = pVideoProcessorEnum->CheckVideoProcessorFormat(VP_Output_Format, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	D3D11_VIDEO_PROCESSOR_CAPS caps = {};
	hr = pVideoProcessorEnum->GetVideoProcessorCaps(&caps);
	if (FAILED(hr)) {
		return hr;
	}

	UINT proccaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION;
	D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS convCaps = {};	
	UINT index;
	for (index = 0; index < caps.RateConversionCapsCount; index++) {
		hr = pVideoProcessorEnum->GetVideoProcessorRateConversionCaps(index, &convCaps);
		if (S_OK == hr) {
			// Check the caps to see which deinterlacer is supported
			if ((convCaps.ProcessorCaps & proccaps) != 0) {
				break;
			}
		}
	}
	if (index >= caps.RateConversionCapsCount) {
		return E_FAIL;
	}

	hr = m_pVideoDevice->CreateVideoProcessor(pVideoProcessorEnum, index, &m_pVideoProcessor);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = dxgiFormat;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_STREAM_OUTPUT; // ???
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, NULL, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		return hr;
	}

	m_srcFormat = dxgiFormat;
	m_srcWidth  = width;
	m_srcHeight = height;

	return S_OK;
}

HRESULT CD3D11VideoProcessor::CopySample(IMediaSample* pSample, const AM_MEDIA_TYPE* pmt)
{
	if (!m_pSrcTexture2D) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	if (CComQIPtr<IMFGetService> pService = pSample) {
		return S_FALSE;
	}
	else if (pmt->formattype == FORMAT_VideoInfo2) {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)(pmt->pbFormat);

			UINT srcLines = vih2->bmiHeader.biHeight;
			if (pmt->subtype == MEDIASUBTYPE_NV12 || pmt->subtype == MEDIASUBTYPE_YV12 || pmt->subtype == MEDIASUBTYPE_P010) {
					srcLines = srcLines * 3 / 2;
			}
			UINT srcPitch = size / srcLines;

			CComPtr<ID3D11DeviceContext> pImmediateContext;
			m_pDevice->GetImmediateContext(&pImmediateContext);
			if (pImmediateContext) {
				pImmediateContext->UpdateSubresource(m_pSrcTexture2D, 0, NULL, data, srcPitch, size);
			}
		}
	}
	
	return hr;
}
