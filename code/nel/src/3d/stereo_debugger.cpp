/**
 * \file stereo_debugger.cpp
 * \brief CStereoDebugger
 * \date 2013-07-03 20:17GMT
 * \author Jan Boon (Kaetemi)
 * CStereoDebugger
 */

/* 
 * Copyright (C) 2013  by authors
 * 
 * This file is part of NL3D.
 * NL3D is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 * 
 * NL3D is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public
 * License along with NL3D.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#if !FINAL_VERSION
#include <nel/misc/types_nl.h>
#include <nel/3d/stereo_debugger.h>

// STL includes

// NeL includes
// #include <nel/misc/debug.h>

// Project includes
#include <nel/3d/u_camera.h>
#include <nel/3d/u_driver.h>
#include <nel/3d/material.h>
#include <nel/3d/texture_bloom.h>
#include <nel/3d/texture_user.h>
#include <nel/3d/driver_user.h>
#include <nel/3d/u_texture.h>

using namespace std;
// using namespace NLMISC;

namespace NL3D {

namespace {

const char *a_arbfp1 =
	"!!ARBfp1.0\n"
	"PARAM c[1] = { { 1, 0, 0.5 } };\n"
	"TEMP R0;\n"
	"TEMP R1;\n"
	"TEMP R2;\n"
	"TEX R0, fragment.texcoord[0], texture[0], 2D;\n"
	"TEX R1, fragment.texcoord[0], texture[1], 2D;\n"
	"ADD R2, R0, -R1;\n"
	"ADD R1, R0, R1;\n"
	"MUL R1, R1, c[0].z;\n"
	"ABS R2, R2;\n"
	"CMP R2, -R2, c[0].x, c[0].y;\n"
	"ADD_SAT R2.x, R2, R2.y;\n"
	"ADD_SAT R2.x, R2, R2.z;\n"
	"ADD_SAT R2.x, R2, R2.w;\n"
	"ABS R2.x, R2;\n"
	"CMP R2.x, -R2, c[0].y, c[0];\n"
	"ABS R0.x, R2;\n"
	"CMP R2.x, -R0, c[0].y, c[0];\n"
	"MOV R0.xzw, R1;\n"
	"MAD R0.y, R1, c[0].z, c[0].z;\n"
	"CMP R0, -R2.x, R1, R0;\n"
	"MAD R1.x, R0, c[0].z, c[0].z;\n"
	"CMP result.color.x, -R2, R1, R0;\n"
	"MOV result.color.yzw, R0;\n"
	"END\n";

class CStereoDebuggerFactory : public IStereoDeviceFactory
{
public:
	IStereoDisplay *createDevice() const
	{
		return new CStereoDebugger();
	}
};

} /* anonymous namespace */

CStereoDebugger::CStereoDebugger() : m_Driver(NULL), m_Stage(0), m_SubStage(0), m_LeftTexU(NULL), m_RightTexU(NULL), m_PixelProgram(NULL)
{
	
}

CStereoDebugger::~CStereoDebugger()
{
	releaseTextures();

	if (!m_Mat.empty())
	{
		m_Driver->deleteMaterial(m_Mat);
	}

	delete m_PixelProgram;
	m_PixelProgram = NULL;

	m_Driver = NULL;
}

/// Sets driver and generates necessary render targets
void CStereoDebugger::setDriver(NL3D::UDriver *driver)
{
	nlassert(!m_PixelProgram);

	NL3D::IDriver *drvInternal = (static_cast<CDriverUser *>(driver))->getDriver();	
	/*if (drvInternal->supportPixelProgram(CPixelProgram::fp40) && drvInternal->supportBloomEffect() && drvInternal->supportNonPowerOfTwoTextures())
	{
		nldebug("VR: fp40");
		m_PixelProgram = new CPixelProgram(a_fp40);		
	}
	else*/ if (drvInternal->supportPixelProgram(CPixelProgram::arbfp1) && drvInternal->supportBloomEffect() && drvInternal->supportNonPowerOfTwoTextures())
	{
		nldebug("VR: arbfp1");
		m_PixelProgram = new CPixelProgram(a_arbfp1);		
	}
	/*else if (drvInternal->supportPixelProgram(CPixelProgram::ps_2_0))
	{
		nldebug("VR: ps_2_0");
		m_PixelProgram = new CPixelProgram(a_ps_2_0);	
	}*/

	if (m_PixelProgram)
	{
		m_Driver = driver;

		initTextures();

		m_Mat = m_Driver->createMaterial();
		m_Mat.initUnlit();
		m_Mat.setColor(CRGBA::White);
		m_Mat.setBlend (false);
		m_Mat.setAlphaTest (false);
		NL3D::CMaterial *mat = m_Mat.getObjectPtr();
		mat->setShader(NL3D::CMaterial::PostProcessing);
		mat->setBlendFunc(CMaterial::one, CMaterial::zero);
		mat->setZWrite(false);
		mat->setZFunc(CMaterial::always);
		mat->setDoubleSided(true);

		setTextures();		

		m_QuadUV.V0 = CVector(0.f, 0.f, 0.5f);
		m_QuadUV.V1 = CVector(1.f, 0.f, 0.5f);
		m_QuadUV.V2 = CVector(1.f, 1.f, 0.5f);
		m_QuadUV.V3 = CVector(0.f, 1.f, 0.5f);

		m_QuadUV.Uv0 = CUV(0.f,  0.f);
		m_QuadUV.Uv1 = CUV(1.f, 0.f);
		m_QuadUV.Uv2 = CUV(1.f, 1.f);
		m_QuadUV.Uv3 = CUV(0.f,  1.f);
	}
}

void CStereoDebugger::releaseTextures()
{
	if (!m_Mat.empty())
	{
		m_Mat.getObjectPtr()->setTexture(0, NULL);
		m_Mat.getObjectPtr()->setTexture(1, NULL);
		m_Driver->deleteMaterial(m_Mat);
	}

	delete m_LeftTexU;
	m_LeftTexU = NULL;
	m_LeftTex = NULL; // CSmartPtr

	delete m_RightTexU;
	m_RightTexU = NULL;
	m_RightTex = NULL; // CSmartPtr
}

void CStereoDebugger::initTextures()
{
	uint32 width, height;
	m_Driver->getWindowSize(width, height);
	NL3D::IDriver *drvInternal = (static_cast<CDriverUser *>(m_Driver))->getDriver();	

	m_LeftTex = new CTextureBloom();
	m_LeftTex->setRenderTarget(true);
	m_LeftTex->setReleasable(false);
	m_LeftTex->resize(width, height);
	m_LeftTex->setFilterMode(ITexture::Linear, ITexture::LinearMipMapOff);
	m_LeftTex->setWrapS(ITexture::Clamp);
	m_LeftTex->setWrapT(ITexture::Clamp);
	drvInternal->setupTexture(*m_LeftTex);
	m_LeftTexU = new CTextureUser(m_LeftTex);
	nlassert(!drvInternal->isTextureRectangle(m_LeftTex)); // not allowed

	m_RightTex = new CTextureBloom();
	m_RightTex->setRenderTarget(true);
	m_RightTex->setReleasable(false);
	m_RightTex->resize(width, height);
	m_RightTex->setFilterMode(ITexture::Linear, ITexture::LinearMipMapOff);
	m_RightTex->setWrapS(ITexture::Clamp);
	m_RightTex->setWrapT(ITexture::Clamp);
	drvInternal->setupTexture(*m_RightTex);
	m_RightTexU = new CTextureUser(m_RightTex);
	nlassert(!drvInternal->isTextureRectangle(m_RightTex)); // not allowed
}

void CStereoDebugger::setTextures()
{
	NL3D::CMaterial *mat = m_Mat.getObjectPtr();
	mat->setTexture(0, m_LeftTex);
	mat->setTexture(1, m_RightTex);
}

void CStereoDebugger::verifyTextures()
{
	if (m_Driver)
	{
		uint32 width, height;
		m_Driver->getWindowSize(width, height);
		if (m_LeftTex->getWidth() != width 
			|| m_RightTex->getWidth() != width 
			|| m_LeftTex->getHeight() != height 
			|| m_RightTex->getHeight() != height)
		{
			nldebug("Rebuild textures");
			releaseTextures();
			initTextures();
			setTextures();
		}
	}
}

/// Gets the required screen resolution for this device
bool CStereoDebugger::getScreenResolution(uint &width, uint &height)
{
	return false;
}

/// Set latest camera position etcetera
void CStereoDebugger::updateCamera(uint cid, const NL3D::UCamera *camera)
{
	m_Frustum[cid] = camera->getFrustum();
}

/// Get the frustum to use for clipping
void CStereoDebugger::getClippingFrustum(uint cid, NL3D::UCamera *camera) const
{
	// do nothing
}

/// Is there a next pass
bool CStereoDebugger::nextPass()
{
	if (m_Driver->getPolygonMode() == UDriver::Filled)
	{
		switch (m_Stage)
		{
		case 0:
			++m_Stage;
			m_SubStage = 0;
			return true;
		case 1:
			++m_Stage;
			m_SubStage = 0;
			return true;
		case 2:
			++m_Stage;
			m_SubStage = 0;
			return true;
		case 3:
			m_Stage = 0;
			m_SubStage = 0;
			return false;
		}
	}
	else
	{
		switch (m_Stage)
		{
		case 0:
			++m_Stage;
			m_SubStage = 0;
			return true;
		case 1:
			m_Stage = 0;
			m_SubStage = 0;
			return false;
		}
	}
	return false;
}

/// Gets the current viewport
const NL3D::CViewport &CStereoDebugger::getCurrentViewport() const
{
	if (m_Stage % 2) return m_LeftViewport;
	else return m_RightViewport;
}

/// Gets the current camera frustum
const NL3D::CFrustum &CStereoDebugger::getCurrentFrustum(uint cid) const
{
	return m_Frustum[cid];
}

/// Gets the current camera frustum
void CStereoDebugger::getCurrentFrustum(uint cid, NL3D::UCamera *camera) const
{
	// do nothing
}

/// Gets the current camera matrix
void CStereoDebugger::getCurrentMatrix(uint cid, NL3D::UCamera *camera) const
{
	// do nothing
}

/// At the start of a new render target
bool CStereoDebugger::wantClear()
{
	m_SubStage = 1;
	return m_Stage != 3;
}
	
/// The 3D scene
bool CStereoDebugger::wantScene()
{
	m_SubStage = 2;
	return m_Stage != 3;
}

/// Interface within the 3D scene
bool CStereoDebugger::wantInterface3D()
{
	m_SubStage = 3;
	return m_Stage == 3;
}
	
/// 2D Interface
bool CStereoDebugger::wantInterface2D()
{
	m_SubStage = 4;
	return m_Stage == 3;
}

/// Returns true if a new render target was set, always fase if not using render targets
bool CStereoDebugger::beginRenderTarget()
{
	if (m_Stage != 3 && m_Driver && (m_Driver->getPolygonMode() == UDriver::Filled))
	{
		if (m_Stage % 2) static_cast<CDriverUser *>(m_Driver)->setRenderTarget(*m_RightTexU, 0, 0, 0, 0);
		else static_cast<CDriverUser *>(m_Driver)->setRenderTarget(*m_LeftTexU, 0, 0, 0, 0);
		return true;
	}
	return false;
}

/// Returns true if a render target was fully drawn, always false if not using render targets
bool CStereoDebugger::endRenderTarget()
{
	if (m_Stage != 3 && m_Driver && (m_Driver->getPolygonMode() == UDriver::Filled))
	{
		CTextureUser cu;
		(static_cast<CDriverUser *>(m_Driver))->setRenderTarget(cu);
		bool fogEnabled = m_Driver->fogEnabled();
		m_Driver->enableFog(false);

		m_Driver->setMatrixMode2D11();
		CViewport vp = CViewport();
		m_Driver->setViewport(vp);
		uint32 width, height;
		NL3D::IDriver *drvInternal = (static_cast<CDriverUser *>(m_Driver))->getDriver();
		NL3D::CMaterial *mat = m_Mat.getObjectPtr();
		mat->setTexture(0, m_LeftTex);
		mat->setTexture(1, m_RightTex);
		drvInternal->activePixelProgram(m_PixelProgram);

		m_Driver->drawQuad(m_QuadUV, m_Mat);

		drvInternal->activePixelProgram(NULL);
		m_Driver->enableFog(fogEnabled);

		return true;
	}
	return false;
}

void CStereoDebugger::listDevices(std::vector<CStereoDeviceInfo> &devicesOut)
{
	CStereoDeviceInfo devInfo;
	devInfo.Factory = new CStereoDebuggerFactory();
	devInfo.Library = CStereoDeviceInfo::NeL3D;
	devInfo.Class = CStereoDeviceInfo::StereoDisplay;
	devInfo.Manufacturer = "NeL";
	devInfo.ProductName = "Stereo Debugger";
	devInfo.Serial = "NL-3D-DEBUG";
	devicesOut.push_back(devInfo);
}

} /* namespace NL3D */

#endif /* #if !FINAL_VERSION */

/* end of file */
