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
#include <Mferror.h>
#include "D3D11VideoProcessor.h"

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
		0,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&m_pD3D11Device,
		&featurelevel,
		nullptr);
	if (FAILED(hr)) {
		return;
	}

	hr = m_pD3D11Device->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pD3D11VideoDevice);
	if (FAILED(hr)) {
		// TODO DLog here
		return; // need Windows 8+
	}
}

CD3D11VideoProcessor::~CD3D11VideoProcessor()
{
	m_pD3D11VideoDevice.Release();
	m_pD3D11Device.Release();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CD3D11VideoProcessor::Initialize(UINT width, UINT height)
{
	if (!m_pD3D11VideoDevice) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcessorEnum = nullptr;
	CComPtr<ID3D11VideoProcessor>           pVideoProcessor = nullptr;

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
	ContentDesc.InputWidth = width;
	ContentDesc.InputHeight = height;
	ContentDesc.OutputWidth = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	hr = m_pD3D11VideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcessorEnum);
	if (FAILED(hr)) {
		return hr;
	}

	UINT uiFlags;
	DXGI_FORMAT VP_Output_Format = DXGI_FORMAT_B8G8R8X8_UNORM;

	hr = pVideoProcessorEnum->CheckVideoProcessorFormat(VP_Output_Format, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	return S_OK;
}
