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

#include "./BaseClasses/streams.h"
#include <atltypes.h>
#include <evr.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <dxvahd.h>

const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P010},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_AYUV}
};

class __declspec(uuid("71F080AA-8661-4093-B15E-4F6903E77D0A"))
	CMpcVideoRenderer : public CBaseRenderer,
	public IMFGetService,
	public IBasicVideo,
	public IVideoWindow
{
private:
	enum VIDEOPROC_TYPE {
		VP_DXVA2,
		VP_DXVAHD,
	};
	VIDEOPROC_TYPE m_VPType = VP_DXVA2;

	CMediaType m_mt;
	D3DFORMAT m_srcFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	RECT m_srcRect = {};
	RECT m_trgRect = {};
	UINT m_srcLines = 0;
	INT  m_srcPitch = 0;
	CComPtr<IDirect3DSurface9> m_pSrcSurface;

	CRect m_nativeVideoRect;
	CRect m_videoRect;
	CRect m_windowRect;

	HWND m_hWnd = nullptr;
	UINT m_CurrentAdapter = D3DADAPTER_DEFAULT;

	HMODULE m_hD3D9Lib = nullptr;
	CComPtr<IDirect3D9Ex>       m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex> m_pD3DDevEx;

	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	HMODULE m_hDxva2Lib = nullptr;

	CComPtr<IDXVAHD_Device>         m_pDXVAHD_Device;
	CComPtr<IDXVAHD_VideoProcessor> m_pDXVAHD_VP;
	DXVAHD_VPDEVCAPS m_DXVAHDDevCaps = {};

	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	DXVA2_Fixed32 m_DXVA2ProcAmpValues[4] = {};

	typedef HRESULT (__stdcall *PTR_DXVA2CreateDirect3DDeviceManager9)(UINT* pResetToken, IDirect3DDeviceManager9** ppDeviceManager);
	typedef HRESULT (__stdcall *PTR_DXVA2CreateVideoService)(IDirect3DDevice9* pDD, REFIID riid, void** ppService);

	PTR_DXVA2CreateDirect3DDeviceManager9 pDXVA2CreateDirect3DDeviceManager9 = nullptr;
	PTR_DXVA2CreateVideoService           pDXVA2CreateVideoService = nullptr;
	CComPtr<IDirect3DDeviceManager9>      m_pD3DDeviceManager;
	UINT                                  m_nResetTocken = 0;
	HANDLE                                m_hDevice = nullptr;

public:
	CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr);
	~CMpcVideoRenderer();

private:
	HRESULT InitDirect3D9();

	BOOL InitializeDXVAHDVP(D3DSURFACE_DESC& desc);
	BOOL InitializeDXVAHDVP(const UINT width, const UINT height, const D3DFORMAT d3dformat);
	HRESULT ResizeDXVAHD(IDirect3DSurface9* pSurface, IDirect3DSurface9* pRenderTarget);
	HRESULT ResizeDXVAHD(BYTE* data, const long size, IDirect3DSurface9* pRenderTarget);

	BOOL InitializeDXVA2VP(D3DSURFACE_DESC& desc);
	BOOL InitializeDXVA2VP(const UINT width, const UINT height, const D3DFORMAT d3dformat);
	HRESULT ResizeDXVA2(IDirect3DSurface9* pSurface, IDirect3DSurface9* pRenderTarget);
	HRESULT ResizeDXVA2(BYTE* data, const long size, IDirect3DSurface9* pRenderTarget);

	void CopyFrameData(BYTE* dst, int dst_pitch, BYTE* src, long src_size);

public:
	// CBaseRenderer
	HRESULT CheckMediaType(const CMediaType *pmt) override;
	HRESULT SetMediaType(const CMediaType *pmt) override;
	HRESULT DoRenderSample(IMediaSample* pSample) override;

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDispatch
	STDMETHODIMP GetTypeInfoCount(UINT* pctinfo) { return E_NOTIMPL; }
	STDMETHODIMP GetTypeInfo(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID riid, OLECHAR** rgszNames, UINT cNames, LCID lcid, DISPID* rgdispid) { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID dispidMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr) { return E_NOTIMPL; }

	// IBasicVideo
	STDMETHODIMP get_AvgTimePerFrame(REFTIME *pAvgTimePerFrame) { return E_NOTIMPL; }
	STDMETHODIMP get_BitRate(long *pBitRate) { return E_NOTIMPL; }
	STDMETHODIMP get_BitErrorRate(long *pBitErrorRate) { return E_NOTIMPL; }
	STDMETHODIMP get_VideoWidth(long *pVideoWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_VideoHeight(long *pVideoHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceLeft(long SourceLeft) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceLeft(long *pSourceLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceWidth(long SourceWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceWidth(long *pSourceWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceTop(long SourceTop) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceTop(long *pSourceTop) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceHeight(long SourceHeight) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceHeight(long *pSourceHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationLeft(long DestinationLeft) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationLeft(long *pDestinationLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationWidth(long DestinationWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationWidth(long *pDestinationWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationTop(long DestinationTop) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationTop(long *pDestinationTop) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationHeight(long DestinationHeight) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationHeight(long *pDestinationHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetSourcePosition(long Left, long Top, long Width, long Height) { return E_NOTIMPL; }
	STDMETHODIMP GetSourcePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetDefaultSourcePosition(void) { return E_NOTIMPL; }
	STDMETHODIMP SetDestinationPosition(long Left, long Top, long Width, long Height);
	STDMETHODIMP GetDestinationPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetDefaultDestinationPosition(void) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoSize(long *pWidth, long *pHeight);
	STDMETHODIMP GetVideoPaletteEntries(long StartIndex, long Entries, long *pRetrieved, long *pPalette) { return E_NOTIMPL; }
	STDMETHODIMP GetCurrentImage(long *pBufferSize, long *pDIBImage) { return E_NOTIMPL; }
	STDMETHODIMP IsUsingDefaultSource(void) { return E_NOTIMPL; }
	STDMETHODIMP IsUsingDefaultDestination(void) { return E_NOTIMPL; }

	// IVideoWindow
	STDMETHODIMP put_Caption(BSTR strCaption) { return E_NOTIMPL; }
	STDMETHODIMP get_Caption(BSTR *strCaption) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowStyle(long WindowStyle) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowStyle(long *WindowStyle) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowStyleEx(long WindowStyleEx) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowStyleEx(long *WindowStyleEx) { return E_NOTIMPL; }
	STDMETHODIMP put_AutoShow(long AutoShow) { return E_NOTIMPL; }
	STDMETHODIMP get_AutoShow(long *AutoShow) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowState(long WindowState) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowState(long *WindowState) { return E_NOTIMPL; }
	STDMETHODIMP put_BackgroundPalette(long BackgroundPalette) { return E_NOTIMPL; }
	STDMETHODIMP get_BackgroundPalette(long *pBackgroundPalette) { return E_NOTIMPL; }
	STDMETHODIMP put_Visible(long Visible) { return E_NOTIMPL; }
	STDMETHODIMP get_Visible(long *pVisible) { return E_NOTIMPL; }
	STDMETHODIMP put_Left(long Left) { return E_NOTIMPL; }
	STDMETHODIMP get_Left(long *pLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_Width(long Width) { return E_NOTIMPL; }
	STDMETHODIMP get_Width(long *pWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_Top(long Top) { return E_NOTIMPL; }
	STDMETHODIMP get_Top(long *pTop) { return E_NOTIMPL; }
	STDMETHODIMP put_Height(long Height) { return E_NOTIMPL; }
	STDMETHODIMP get_Height(long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_Owner(OAHWND Owner);
	STDMETHODIMP get_Owner(OAHWND *Owner);
	STDMETHODIMP put_MessageDrain(OAHWND Drain) { return E_NOTIMPL; }
	STDMETHODIMP get_MessageDrain(OAHWND *Drain) { return E_NOTIMPL; }
	STDMETHODIMP get_BorderColor(long *Color) { return E_NOTIMPL; }
	STDMETHODIMP put_BorderColor(long Color) { return E_NOTIMPL; }
	STDMETHODIMP get_FullScreenMode(long *FullScreenMode) { return E_NOTIMPL; }
	STDMETHODIMP put_FullScreenMode(long FullScreenMode) { return E_NOTIMPL; }
	STDMETHODIMP SetWindowForeground(long Focus) { return E_NOTIMPL; }
	STDMETHODIMP NotifyOwnerMessage(OAHWND hwnd, long uMsg, LONG_PTR wParam, LONG_PTR lParam) { return E_NOTIMPL; }
	STDMETHODIMP SetWindowPosition(long Left, long Top, long Width, long Height);
	STDMETHODIMP GetWindowPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetMinIdealImageSize(long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetMaxIdealImageSize(long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetRestorePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP HideCursor(long HideCursor) { return E_NOTIMPL; }
	STDMETHODIMP IsCursorHidden(long *CursorHidden) { return E_NOTIMPL; }
};
