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

#include <dxva2api.h>

enum :int {
	TEXFMT_AUTOINT = 0,
	TEXFMT_8INT = 8,
	TEXFMT_10INT = 10,
	TEXFMT_16FLOAT = 16,
};

enum :int {
	CHROMA_Bilinear = 0,
	CHROMA_CatmullRom,
	CHROMA_COUNT,
};

enum :int {
	UPSCALE_Nearest = 0,
	//UPSCALE_Bilinear,
	UPSCALE_Mitchell,
	UPSCALE_CatmullRom,
	UPSCALE_Lanczos2,
	UPSCALE_Lanczos3,
	UPSCALE_COUNT,
};

enum :int {
	DOWNSCALE_Box = 0,
	DOWNSCALE_Bilinear,
	DOWNSCALE_Hamming,
	DOWNSCALE_Bicubic,
	DOWNSCALE_Bicubic_sharp,
	DOWNSCALE_Lanczos,
	DOWNSCALE_COUNT,
};

enum :int {
	SWAPEFFECT_Discard = 0,
	SWAPEFFECT_Flip,
	SWAPEFFECT_COUNT,
};

struct VPEnableFormats_t {
	bool bNV12;
	bool bP01x;
	bool bYUY2;
	bool bOther;
};

struct Settings_t {
	bool bUseD3D11;
	bool bShowStats;
	int  iTextureFmt;
	VPEnableFormats_t VPFmts;
	bool bDeintDouble;
	int  bVPScaling;
	int  iChromaScaling;
	int  iUpscaling;
	int  iDownscaling;
	bool bInterpolateAt50pct;
	int  iSwapEffect;

	Settings_t() {
		SetDefault();
	}

	void SetDefault() {
		bUseD3D11           = false;
		bShowStats          = false;
		iTextureFmt         = TEXFMT_AUTOINT;
		VPFmts.bNV12        = true;
		VPFmts.bP01x        = true;
		VPFmts.bYUY2        = true;
		VPFmts.bOther       = true;
		bDeintDouble        = true;
		bVPScaling          = true;
		iChromaScaling      = CHROMA_Bilinear;
		iUpscaling          = UPSCALE_CatmullRom;
		iDownscaling        = DOWNSCALE_Hamming;
		bInterpolateAt50pct = true;
		iSwapEffect         = SWAPEFFECT_Discard;
	}
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	STDMETHOD(GetVideoProcessorInfo) (CStringW& str) PURE;
	STDMETHOD_(bool, GetActive()) PURE;

	STDMETHOD_(void, GetSettings(Settings_t& setings)) PURE;
	STDMETHOD_(void, SetSettings(const Settings_t setings)) PURE; // use copy of setings here

	STDMETHOD(SaveSettings()) PURE;
};
