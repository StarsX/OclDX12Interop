//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#ifdef GetModuleFileName
#undef GetModuleFileName
#define GetModuleFileName GetModuleFileNameW
#endif

#include "DXFramework.h"
#include "Core/XUSG.h"

class Ocl12
{
public:
	struct OclContext
	{
		cl_device_id Device;
		cl_context Context;
		cl_command_queue Queue;
	};

	Ocl12(const OclContext& oclContext, const XUSG::com_ptr<ID3D11Device>& device11);
	virtual ~Ocl12();

	bool Init(XUSG::CommandList* pCommandList, XUSG::Texture::sptr& source,
		std::vector<XUSG::Resource::uptr>& uploaders, XUSG::Format rtFormat, const wchar_t* fileName);
	bool InitOcl();

	void Init11();
	void Process();
	void CopyToBackBuffer11(const XUSG::com_ptr<ID3D11Resource>& backbuffer);

	void GetImageSize(uint32_t& width, uint32_t& height) const;

protected:
	OclContext m_oclContext;
	cl_kernel m_clKernel;

	XUSG::Resource::uptr			m_shared;
	//XUSG::RenderTarget::uptr		m_result;

	XUSG::com_ptr<ID3D11Device1>	m_device11;
	XUSG::com_ptr<ID3D11Texture2D>	m_source11;
	XUSG::com_ptr<ID3D11Texture2D>	m_shared11;
	XUSG::com_ptr<ID3D11Texture2D>	m_result11;

	cl_mem m_sourceOCL;
	cl_mem m_resultOCL;

	DirectX::XMUINT2				m_imageSize;
};
