/*
* (C) 2018-2019 see Authors.txt
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
#include <d2d1.h>
#include <dwrite.h>
#include <evr9.h> // for IMFVideoProcessor
#include <DirectXMath.h>
#include "IVideoRenderer.h"
#include "Helper.h"
#include "FrameStats.h"
#include "StatsDrawing.h"

class CMpcVideoRenderer;

class CDX11VideoProcessor
	: public IMFVideoProcessor
{
private:
	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	bool m_bDeintDouble = false;
	bool m_bShowStats = false;

	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pDeviceContext;
	ID3D11SamplerState* m_pSamplerPoint = nullptr;
	CComPtr<ID3D11VertexShader> m_pVS_Simple;
	CComPtr<ID3D11PixelShader>  m_pPS_Simple;
	CComPtr<ID3D11InputLayout>  m_pInputLayout;

	CComPtr<ID3D11Texture2D> m_pSrcTexture2D_CPU;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D;

	// D3D11 Video Processor
	CComPtr<ID3D11VideoContext> m_pVideoContext;
#if VER_PRODUCTBUILD >= 10000
	CComPtr<ID3D11VideoContext1> m_pVideoContext1;
#endif
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11VideoProcessorInputView> m_pInputView;

	// D3D11 Shader Video Processor
	CComPtr<ID3D11PixelShader> m_pPS_ConvertColor;
	ID3D11ShaderResourceView* m_pShaderResource = nullptr;
	ID3D11SamplerState* m_pSamplerLinear = nullptr;
	ID3D11Buffer* m_pVertexBuffer = nullptr;
	ID3D11Buffer* m_pPixelShaderConstants = nullptr;

	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;

	// Input parameters
	GUID        m_srcSubType      = GUID_NULL;
	DXGI_FORMAT m_srcDXGIFormat   = DXGI_FORMAT_UNKNOWN;
	UINT        m_srcWidth        = 0;
	UINT        m_srcHeight       = 0;
	UINT        m_srcRectWidth    = 0;
	UINT        m_srcRectHeight   = 0;
	int         m_srcPitch        = 0;
	DWORD       m_srcAspectRatioX = 0;
	DWORD       m_srcAspectRatioY = 0;
	CRect m_srcRect;
	CRect m_trgRect;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;

	// Input MediaType. Used in SetDevice() that is called from CVideoRendererInputPin::ActivateD3D11Decoding()
	CMediaType m_inputMT;

	// D3D11 decoder texture parameters
	UINT m_TextureWidth  = 0;
	UINT m_TextureHeight = 0;

	// Output parameters
	DXGI_FORMAT m_VPOutputFmt = DXGI_FORMAT_B8G8R8X8_UNORM;

	typedef void(*CopyFrameDataFn)(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch);
	CopyFrameDataFn m_pConvertFn = nullptr;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	int m_FieldDrawn = 0;

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};
	D3D11_VIDEO_PROCESSOR_FILTER_RANGE m_VPFilterRange[4] = {};
	struct {
		BOOL Enabled;
		int Level;
	} m_VPFilterSettings[4] = {};

	CRect m_videoRect;
	CRect m_windowRect;

	HWND m_hWnd = nullptr;

	DWORD m_VendorId = 0;
	CString m_strAdapterDescription;

	CComPtr<ID3D11Texture2D> m_pOSDTex2D;
	ID3D11Buffer* m_pOSDVertexBuffer = nullptr;

	CRenderStats m_RenderStats;

	CStatsDrawing m_StatsDrawing;
	CStringW m_strStatsStatic;
	bool m_bSrcFromGPU = false;

	bool resetmt = false;

public:
	CDX11VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX11VideoProcessor();

	HRESULT Init(const int iSurfaceFmt);

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid);

public:
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	HRESULT InitSwapChain(const HWND hwnd, UINT width = 0, UINT height = 0);

	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);
	HRESULT InitializeD3D11VP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height, bool only_update_surface);
	HRESULT InitializeTexVP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height);
	HRESULT SetVertices(UINT dstW, UINT dstH);

	void Start();
	void Stop();

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	bool SecondFramePossible() { return m_bDeintDouble && m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; }

	void GetSourceRect(CRect& sourceRect) { sourceRect = m_srcRect; }
	void GetVideoRect(CRect& videoRect) { videoRect = m_videoRect; }
	void SetVideoRect(const CRect& videoRect) { m_videoRect = videoRect; }
	void SetWindowRect(const CRect& windowRect) { m_windowRect = windowRect; }

	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetCurentImage(long *pDIBImage);
	HRESULT GetVPInfo(CStringW& str);

	void SetDeintDouble(bool value) { m_bDeintDouble = value; };
	void SetShowStats(bool value) { m_bShowStats = value; };

private:
	HRESULT ProcessD3D11(ID3D11Texture2D* pRenderTarget, const RECT* pSrcRect, const RECT* pDstRect, const RECT* pWndRect, const bool second);
	HRESULT ProcessTex(ID3D11Texture2D* pRenderTarget, const RECT* pSrcRect, const RECT* pDstRect, const RECT* pWndRect);
	void UpdateStatsStatic();
	HRESULT DrawStats(ID3D11Texture2D* pRenderTarget);

public:
	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IMFVideoProcessor
	STDMETHODIMP GetAvailableVideoProcessorModes(UINT *lpdwNumProcessingModes, GUID **ppVideoProcessingModes) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorCaps(LPGUID lpVideoProcessorMode, DXVA2_VideoProcessorCaps *lpVideoProcessorCaps) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP SetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange);
	STDMETHODIMP GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values);
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues);
	STDMETHODIMP GetFilteringRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange) { return E_NOTIMPL; }
	STDMETHODIMP GetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP SetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP GetBackgroundColor(COLORREF *lpClrBkg);
	STDMETHODIMP SetBackgroundColor(COLORREF ClrBkg) { return E_NOTIMPL; }
};
