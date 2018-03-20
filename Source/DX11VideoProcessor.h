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

#pragma once

#include <atltypes.h>
#include <ntverp.h>
#include <DXGI1_2.h>
#include <dxva2api.h>
#include <strmif.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include "IVideoRenderer.h"

class CDX11VideoProcessor
{
private:
	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pImmediateContext;
	CComPtr<ID3D11VideoContext> m_pVideoContext;
#if VER_PRODUCTBUILD >= 10000
	CComPtr<ID3D11VideoContext1> m_pVideoContext1;
#endif
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D_Decode;
	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain> m_pDXGISwapChain;

	CMediaType m_mt;
	D3DFORMAT m_srcD3DFormat = D3DFMT_UNKNOWN;
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	UINT m_srcPitch = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;
	RECT m_srcRect = {};
	RECT m_trgRect = {};

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	DXGI_FORMAT m_D3D11_Src_Format = DXGI_FORMAT_UNKNOWN;
	UINT m_D3D11_Src_Width = 0;
	UINT m_D3D11_Src_Height = 0;

	CRect m_nativeVideoRect;
	CRect m_videoRect;
	CRect m_windowRect;

	HWND m_hWnd = nullptr;

	DWORD m_VendorId = 0;
	CString m_strAdapterDescription;

public:
	CDX11VideoProcessor();
	~CDX11VideoProcessor();

	HRESULT Init();
	void ClearD3D11();

	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);

	HRESULT InitSwapChain(const HWND hwnd);

	BOOL InitMediaType(const CMediaType* pmt);
	HRESULT Initialize(const UINT width, const UINT height, const DXGI_FORMAT dxgiFormat);

	HRESULT CopySample(IMediaSample* pSample);
	HRESULT Render(const FILTER_STATE filterState);
	void StopInputBuffer() {}

	void SetVideoRect(const CRect& videoRect) { m_videoRect = videoRect; }
	void SetWindowRect(const CRect& windowRect) { m_windowRect = windowRect; }

	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetFrameInfo(VRFrameInfo* pFrameInfo);

private:
	HRESULT ProcessDX11(ID3D11Texture2D* pRenderTarget);
};
