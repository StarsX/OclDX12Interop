//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "stdafx.h"
#include "Ocl12.h"
#define _INDEPENDENT_DDS_LOADER_
#include "Advanced/XUSGDDSLoader.h"
#undef _INDEPENDENT_DDS_LOADER_

using namespace std;
using namespace DirectX;
using namespace XUSG;

extern clCreateFromD3D11Texture2DKHR_fn clCreateFromD3D11Texture2D;
extern clEnqueueAcquireD3D11ObjectsKHR_fn clEnqueueAcquireD3D11Objects;
extern clEnqueueReleaseD3D11ObjectsKHR_fn clEnqueueReleaseD3D11Objects;
extern clEnqueueAcquireD3D11ObjectsKHR_fn clEnqueueAcquireExternalMemObjects;
extern clEnqueueReleaseD3D11ObjectsKHR_fn clEnqueueReleaseExternalMemObjects;
extern clCreateImageFromExternalMemoryKHR_fn clCreateImageFromExternalMemory;

Ocl12::Ocl12(const OclContext& oclContext, const com_ptr<ID3D11Device>& device11) :
	m_oclContext(oclContext),
	m_clKernel(nullptr),
	m_sourceOCL(nullptr),
	m_resultOCL(nullptr),
	m_imageSize(1, 1)
{
	ThrowIfFailed(device11->QueryInterface<ID3D11Device1>(m_device11.put()));
}

Ocl12::~Ocl12()
{
	auto status = clReleaseKernel(m_clKernel);
	if (status != CL_SUCCESS) cerr << "Error: Fail to releasing kernel" << endl;
}

cl_int check_external_memory_handle_type(cl_device_id deviceID, cl_external_mem_handle_type_khr requiredHandleType)
{
	unsigned int i;
	cl_external_mem_handle_type_khr* handle_type;
	size_t handle_type_size = 0;

	cl_int errNum = CL_SUCCESS;

	errNum = clGetDeviceInfo(deviceID, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, 0, NULL, &handle_type_size);
	handle_type = (cl_external_mem_handle_type_khr*)malloc(handle_type_size);

	errNum = clGetDeviceInfo(deviceID, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, handle_type_size, handle_type, NULL);

	if (CL_SUCCESS != errNum) {
		printf(" Unable to query CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR \n");
		return errNum;
	}

	for (i = 0; i < handle_type_size; i++)
	{
		const auto a = handle_type[i];
		if (requiredHandleType == handle_type[i]) {
			return CL_SUCCESS;
		}
	}
	printf(" cl_khr_external_memory extension is missing support for %d\n",
		requiredHandleType);

	return CL_INVALID_VALUE;
}

bool Ocl12::Init(CommandList* pCommandList, Texture::sptr& source,
	vector<Resource::uptr>& uploaders, Format rtFormat, const wchar_t* fileName)
{
	const auto pDevice = pCommandList->GetDevice();

	// Load input image
	{
		DDS::Loader textureLoader;
		DDS::AlphaMode alphaMode;

		uploaders.emplace_back(Resource::MakeUnique());
		XUSG_N_RETURN(textureLoader.CreateTextureFromFile(pCommandList, fileName,
			8192, false, source, uploaders.back().get(), &alphaMode, ResourceState::COMMON,
			MemoryFlag::SHARED), false);
	}

	// Create resources
	m_imageSize.x = static_cast<uint32_t>(source->GetWidth());
	m_imageSize.y = source->GetHeight();

	const auto pDevice12 = static_cast<ID3D12Device*>(pDevice->GetHandle());
#if USE_CL_KHR_EXTERNAL_MEM
	{
		m_result = RenderTarget::MakeUnique();
		XUSG_N_RETURN(m_result->Create(pDevice, m_imageSize.x, m_imageSize.y, rtFormat, 1,
			ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS,
			1, 1, nullptr, false, MemoryFlag::SHARED, L"Result"), false);

		/*m_shared = Texture::MakeUnique();
		XUSG_N_RETURN(m_shared->Create(pDevice, m_imageSize.x, m_imageSize.y, rtFormat, 1,
			ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS,
			1, 1, false, MemoryFlag::SHARED, L"Source"), false);*/

		// Share DX12 resources
		HANDLE hResult, hSource;
		XUSG_M_RETURN(FAILED(pDevice12->CreateSharedHandle(static_cast<ID3D12Resource*>(m_result->GetHandle()),
			nullptr, GENERIC_ALL, nullptr, &hResult)), cerr, "Failed to share Result.", false);
		XUSG_M_RETURN(FAILED(pDevice12->CreateSharedHandle(static_cast<ID3D12Resource*>(source->GetHandle()),
			nullptr, GENERIC_ALL, nullptr, &hSource)), cerr, "Failed to share Source.", false);

		assert(rtFormat == Format::B8G8R8A8_UNORM);
		const cl_image_format clImageFormat = { CL_BGRA, CL_UNORM_INT8 };
		cl_image_desc clImageDesc =
		{
			CL_MEM_OBJECT_IMAGE2D,
			m_imageSize.x, m_imageSize.y, 1,
			1,
			0, 0,
			1,
			0,
			nullptr
		};

		vector<cl_mem_properties> clExtMemProps;
		auto status = check_external_memory_handle_type(m_oclContext.Device, CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR);
		clExtMemProps.push_back((cl_mem_properties_khr)CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR);
		//auto status = check_external_memory_handle_type(m_oclContext.Device, 0x2066);
		//clExtMemProperties.push_back((cl_mem_properties_khr)0x2066);
		clExtMemProps.push_back((cl_mem_properties_khr)hResult);

		cl_device_id devList[] = { m_oclContext.Device, nullptr };
		clExtMemProps.push_back((cl_mem_properties_khr)CL_DEVICE_HANDLE_LIST_KHR);
		clExtMemProps.push_back((cl_mem_properties_khr)devList[0]);
		clExtMemProps.push_back((cl_mem_properties_khr)CL_DEVICE_HANDLE_LIST_END_KHR);

		clExtMemProps.push_back(0);

		cl_external_mem_desc_khr clExtMem;
		clExtMem.type = CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR;
		clExtMem.handle_ptr = hResult;
		clExtMem.offset = 0;
		clExtMem.size = 0;// sizeof(uint32_t)* m_imageSize.x* m_imageSize.y;

		/*m_resultOCL = clCreateImageFromExternalMemory(m_oclContext.Context,
			&clExtMemProps[2],
			CL_MEM_READ_WRITE,
			clExtMem,
			&clImageFormat,
			&clImageDesc,
			&status);*/

		m_resultOCL = clCreateImageWithProperties(m_oclContext.Context,
			clExtMemProps.data(),
			CL_MEM_READ_WRITE,
			&clImageFormat,
			&clImageDesc,
			nullptr,
			&status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);

		clExtMemProps[1] = (cl_mem_properties_khr)hSource;
		m_sourceOCL = clCreateImageWithProperties(m_oclContext.Context,
			clExtMemProps.data(),
			CL_MEM_READ_ONLY,
			&clImageFormat,
			&clImageDesc,
			nullptr,
			&status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);
	}
#else
	{
		// Create DX11 resources
		CD3D11_TEXTURE2D_DESC texDesc(DXGI_FORMAT_B8G8R8A8_UNORM, m_imageSize.x, m_imageSize.y,
			1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1);
		m_device11->CreateTexture2D(&texDesc, nullptr, m_source11.put());

		texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		m_device11->CreateTexture2D(&texDesc, nullptr, m_shared11.put());

		texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;
		m_device11->CreateTexture2D(&texDesc, nullptr, m_result11.put());

		// Share DX11 resources
		HANDLE hShared;
		com_ptr<IDXGIResource1> pResource;
		XUSG_M_RETURN(FAILED(m_shared11->QueryInterface(pResource.put())), cerr, "Failed to query DXGI resource.", false);
		XUSG_M_RETURN(FAILED(pResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ |
			DXGI_SHARED_RESOURCE_WRITE, nullptr, &hShared)), cerr, "Failed to share Source.", false);

		// Open resource handles on DX12
		com_ptr<ID3D12Resource> resource12;
		XUSG_M_RETURN(FAILED(pDevice12->OpenSharedHandle(hShared, IID_PPV_ARGS(&resource12))),
			cerr, "Failed to open shared Source on DX12.", false);
		m_shared = Resource::MakeUnique();
		m_shared->Create(pDevice12, resource12.get());
		//CloseHandle(hShared);

		// Open resource handles on DX11
		//XUSG_M_RETURN(FAILED(m_device11->OpenSharedResource1(hResult, IID_PPV_ARGS(&m_result11))),
			//cerr, "Failed to open shared Result on DX11.", false);

		// Wrap OpenCL resources
		cl_int status = CL_SUCCESS;
		m_sourceOCL = clCreateFromD3D11Texture2D(m_oclContext.Context, CL_MEM_READ_ONLY, m_source11.get(), 0, &status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);
		m_resultOCL = clCreateFromD3D11Texture2D(m_oclContext.Context, CL_MEM_WRITE_ONLY, m_result11.get(), 0, &status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);
	}
#endif

	// Init OpenCL program
	XUSG_N_RETURN(InitOcl(), false);

#if !USE_CL_KHR_EXTERNAL_MEM
	ResourceBarrier barriers[2];
	//m_result->SetBarrier(barriers, ResourceState::COPY_SOURCE);
	auto numBarriers = m_shared->SetBarrier(barriers, ResourceState::COPY_DEST);
	pCommandList->Barrier(numBarriers, barriers);
	pCommandList->CopyResource(m_shared.get(), source.get());
	numBarriers = m_shared->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	pCommandList->Barrier(numBarriers, barriers);
#endif

	return true;
}

bool HandleCompileError(cl_program program, cl_device_id deivce)
{
	string buildLog = "";
	size_t buildLogSize = 0;

	auto logStatus = clGetProgramBuildInfo(program, deivce, CL_PROGRAM_BUILD_LOG, buildLogSize, nullptr, &buildLogSize);
	if(logStatus != CL_SUCCESS)
	{
		printf("logging error\n");
		exit(EXIT_FAILURE);
	}

	buildLog.resize(buildLogSize);
	logStatus = clGetProgramBuildInfo (program, deivce, CL_PROGRAM_BUILD_LOG, buildLogSize, &buildLog[0], NULL);
	if(logStatus != CL_SUCCESS) return false;

	cout << endl;
	cout << "BUILD LOG" << endl;
	cout << "************************************************************************" << endl;
	cout << buildLog;
	cout << "END" << endl;
	cout << "************************************************************************" << endl;

	return true;
}

bool Ocl12::InitOcl()
{
	cl_int status;
	const char* clProgramString =
		"__constant sampler_t smpl = CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;"

		"kernel void clKernel(__write_only image2d_t result, __read_only image2d_t source)\n"
		"{\n"
		"	const float2 imageSize = (float2)(get_image_width(result), get_image_width(result));\n"
		"	const int2 idx = (int2)(get_global_id(0), get_global_id(1));\n"
		"	const float2 uv = ((float2)(idx.x, idx.y) + 0.5f) / imageSize;\n"

		"	const float4 src = read_imagef(source, smpl, uv);\n"
		"	const float4 dst = src.x * 0.299f + src.y * 0.587f + src.z * 0.114f;\n"
		
		"	write_imagef(result, idx, dst);\n"
		"}";

	const size_t sourceSize = strlen(clProgramString);

	auto clProgram = clCreateProgramWithSource(m_oclContext.Context, 1, (const char**)&clProgramString, &sourceSize, &status);
	if (status != CL_SUCCESS)
	{
		cerr << "Error: clCreateProgramWithSource error" << endl;
		return false;
	}

	status = clBuildProgram(clProgram, 1, &m_oclContext.Device, nullptr, nullptr, nullptr);
	if (status == CL_BUILD_PROGRAM_FAILURE)
		return HandleCompileError(clProgram, m_oclContext.Device);

	m_clKernel = clCreateKernel(clProgram, "clKernel", &status);
	if (status != CL_SUCCESS)
	{
		cerr << "Error: clCreateKernel error" << endl;
		return false;
	}

	status = clReleaseProgram(clProgram);
	if (status != CL_SUCCESS) cerr << "Error: Fail to releasing program" << endl;

	return true;
}

void Ocl12::Init11()
{
#if !USE_CL_KHR_EXTERNAL_MEM
	if (m_device11)
	{
		com_ptr<ID3D11DeviceContext> context;
		m_device11->GetImmediateContext(context.put());
		if (context) context->CopyResource(m_source11.get(), m_shared11.get());
	}
#endif
}

void Ocl12::Process()
{
	const cl_mem pResourcesOCL[] = { m_sourceOCL, m_resultOCL };
	const auto numResouces = static_cast<cl_uint>(size(pResourcesOCL));
#if USE_CL_KHR_EXTERNAL_MEM
	auto status = clEnqueueAcquireExternalMemObjects(m_oclContext.Queue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueAcquireExternalMemObjectsKHR fails" << endl;
#else
	auto status = clEnqueueAcquireD3D11Objects(m_oclContext.Queue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueAcquireD3D11Objects fails" << endl;
#endif

	status = clSetKernelArg(m_clKernel, 0, sizeof(cl_mem), &m_resultOCL);
	if (status != CL_SUCCESS) cerr << "Error: clSetKernelArg fails" << endl;

	status = clSetKernelArg(m_clKernel, 1, sizeof(cl_mem), &m_sourceOCL);
	if (status != CL_SUCCESS) cerr << "Error: clSetKernelArg fails" << endl;

	size_t globalDim[2];
	globalDim[0] = m_imageSize.y;
	globalDim[1] = m_imageSize.x;

	status = clEnqueueNDRangeKernel(m_oclContext.Queue, m_clKernel, 2, nullptr, globalDim, nullptr, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueNDRangeKernel fails" << endl;

#if USE_CL_KHR_EXTERNAL_MEM
	status = clEnqueueReleaseExternalMemObjects(m_oclContext.Queue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueReleaseExternalMemObjectsKHR fails" << endl;
#else
	status = clEnqueueReleaseD3D11Objects(m_oclContext.Queue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueReleaseD3D11Objects fails" << endl;
#endif

	clFinish(m_oclContext.Queue);
}

void Ocl12::CopyToBackBuffer11(const com_ptr<ID3D11Resource>& backbuffer)
{
	if (m_device11)
	{
		com_ptr<ID3D11DeviceContext> context;
		m_device11->GetImmediateContext(context.put());
		if (context) context->CopyResource(backbuffer.get(), m_result11.get());
	}
}

void Ocl12::GetImageSize(uint32_t& width, uint32_t& height) const
{
	width = m_imageSize.x;
	height = m_imageSize.y;
}

Resource* Ocl12::GetResult()
{
	return m_result.get();
}
