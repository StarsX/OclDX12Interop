//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#ifdef GetModuleFileName
#undef GetModuleFileName
#define GetModuleFileName GetModuleFileNameW
#endif

#include "Core/XUSG.h"
#include "Helper/XUSGOpenCL11.h"

//#define m_useClExternalMem 1

class Ocl12
{
public:
	Ocl12(const XUSG::OclContext11& oclContext);
	virtual ~Ocl12();

	bool Init(XUSG::CommandList* pCommandList, std::vector<XUSG::Resource::uptr>& uploaders,
		XUSG::Format rtFormat, const wchar_t* fileName, bool useClExternalMem);
	bool InitOcl();
	bool Init11();

	void Process();
	void CopyToBackBuffer11(const XUSG::com_ptr<ID3D11Resource>& backbuffer);

	void GetImageSize(uint32_t& width, uint32_t& height) const;

	XUSG::Resource* GetResult();

protected:
	const XUSG::OclContext11*		m_pClContext;
	cl_kernel						m_clKernel;

	XUSG::Texture::sptr				m_source;
	XUSG::RenderTarget::uptr		m_result;

	XUSG::com_ptr<ID3D11Texture2D>	m_shared11;
	XUSG::com_ptr<ID3D11Texture2D>	m_source11;
	XUSG::com_ptr<ID3D11Texture2D>	m_result11;

	cl_mem							m_sourceOCL;
	cl_mem							m_resultOCL;

	DirectX::XMUINT2				m_imageSize;

	bool							m_useClExternalMem;
};
