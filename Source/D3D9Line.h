/*
 * (C) 2019 see Authors.txt
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

#include <d3d9.h>
#include <DirectXMath.h>
#include "Helper.h"

class CD3D9Line
{
	struct LINEVERTEX {
		FLOAT x, y, z, rhw;
		DWORD color;
	};
	LINEVERTEX m_Vertices[6] = {};
	IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;

public:
	HRESULT InitDeviceObjects(IDirect3DDevice9* pD3DDev)
	{
		InvalidateDeviceObjects();
		if (!pD3DDev) {
			return E_POINTER;
		}
		HRESULT hr = pD3DDev->CreateVertexBuffer(6 * sizeof(LINEVERTEX), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVertexBuffer, nullptr);

		return hr;
	}

	void InvalidateDeviceObjects()
	{
		SAFE_RELEASE(m_pVertexBuffer);
	}

	HRESULT Set(const float x1, const float y1, const float x2, const float y2, const int thickness, const D3DCOLOR color)
	{
		HRESULT hr = S_OK;

		const float a = atan2f(y1 - y2, x2 - x1);
		const float xt = thickness * sinf(a);
		const float yt = thickness * cosf(a);
		const float x3 = x2 + xt;
		const float y3 = y2 + yt;
		const float x4 = x1 + xt;
		const float y4 = y1 + yt;

		m_Vertices[0] = { x1, y1, 0.5f, 1.0f, color };
		m_Vertices[1] = { x2, y2, 0.5f, 1.0f, color };
		m_Vertices[2] = { x3, y3, 0.5f, 1.0f, color };
		m_Vertices[3] = { x1, y1, 0.5f, 1.0f, color };
		m_Vertices[4] = { x3, y3, 0.5f, 1.0f, color };
		m_Vertices[5] = { x4, y4, 0.5f, 1.0f, color };

		if (m_pVertexBuffer) {
			VOID* pVertices;
			hr = m_pVertexBuffer->Lock(0, sizeof(m_Vertices), (void**)&pVertices, 0);
			if (S_OK == hr) {
				memcpy(pVertices, m_Vertices, sizeof(m_Vertices));
				m_pVertexBuffer->Unlock();
			};
		}

		return hr;
	}

	HRESULT Draw(IDirect3DDevice9* pD3DDev)
	{
		HRESULT hr = pD3DDev->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(LINEVERTEX));
		if (S_OK == hr) {
			hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
			hr =  pD3DDev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
		}

		return hr;
	}

	~CD3D9Line()
	{
		InvalidateDeviceObjects();
	}
};
