//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#ifdef GetModuleFileName
#undef GetModuleFileName
#define GetModuleFileName GetModuleFileNameW
#endif

#include "Core/XUSG.h"

#define USE_CL_KHR_EXTERNAL_MEM 0

//typedef CL_API_ENTRY cl_mem (CL_API_CALL* clCreateImageFromExternalMemoryKHR_fn)(
//	cl_context context,
//	const cl_mem_properties* properties,
//	cl_mem_flags flags,
//	cl_external_mem_desc_khr extMem,
//	const cl_image_format* image_format,
//	const cl_image_desc* image_desc,
//	cl_int* errcode_ret) CL_API_SUFFIX__VERSION_1_2;

const std::map<cl_external_memory_handle_type_khr, const char*> g_handleTypeNames =
{
	{ 0,												"CL_EXTERNAL_MEMORY_HANDLE_NULL" },
	{ CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_FD_KHR,			"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_FD_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR,		"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KMT_KHR,	"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32KMT_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KHR,		"CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KMT_KHR,	"CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KMT_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_D3D12_HEAP_KHR,			"CL_EXTERNAL_MEMORY_HANDLE_D3D12_HEAP_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_D3D12_RESOURCE_KHR,		"CL_EXTERNAL_MEMORY_HANDLE_D3D12_RESOURCE_KHR" },
	{ CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR,			"CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR" }
};

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

	XUSG::Resource* GetResult();

protected:
	OclContext m_oclContext;
	cl_kernel m_clKernel;

	XUSG::Resource::uptr			m_shared;
	XUSG::RenderTarget::uptr		m_result;

	XUSG::com_ptr<ID3D11Device1>	m_device11;
	XUSG::com_ptr<ID3D11Texture2D>	m_source11;
	XUSG::com_ptr<ID3D11Texture2D>	m_shared11;
	XUSG::com_ptr<ID3D11Texture2D>	m_result11;

	cl_mem m_sourceOCL;
	cl_mem m_resultOCL;

	DirectX::XMUINT2				m_imageSize;
};
