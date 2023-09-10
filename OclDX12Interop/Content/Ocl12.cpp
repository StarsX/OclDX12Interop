//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "DXFrameworkHelper.h"
#include "Ocl12.h"
#define _INDEPENDENT_DDS_LOADER_
#include "Advanced/XUSGDDSLoader.h"
#undef _INDEPENDENT_DDS_LOADER_

using namespace std;
using namespace DirectX;
using namespace XUSG;

Ocl12::Ocl12(const OclContext11& oclContext) :
	m_pClContext(&oclContext),
	m_clKernel(nullptr),
	m_sourceOCL(nullptr),
	m_resultOCL(nullptr),
	m_imageSize(1, 1)
{
}

Ocl12::~Ocl12()
{
	auto status = clReleaseKernel(m_clKernel);
	if (status != CL_SUCCESS) cerr << "Error: Fail to releasing kernel" << endl;
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
	if (USE_CL_KHR_EXTERNAL_MEM)
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
		//auto status = m_pClContext->CheckExternalMemoryHandleType(CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR);
		//clExtMemProps.push_back((cl_mem_properties)CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR);
		auto status = m_pClContext->CheckExternalMemoryHandleType(CL_EXTERNAL_MEMORY_HANDLE_D3D12_RESOURCE_KHR);
		clExtMemProps.push_back((cl_mem_properties)CL_EXTERNAL_MEMORY_HANDLE_D3D12_RESOURCE_KHR);
		clExtMemProps.push_back((cl_mem_properties)hResult);

		cl_device_id devList[] = { m_pClContext->GetDevice(), nullptr };
		clExtMemProps.push_back((cl_mem_properties)CL_DEVICE_HANDLE_LIST_KHR);
		clExtMemProps.push_back((cl_mem_properties)devList[0]);
		clExtMemProps.push_back((cl_mem_properties)CL_DEVICE_HANDLE_LIST_END_KHR);

		clExtMemProps.push_back(0);

		//cl_external_memory_desc_khr clExtMem;
		//clExtMem.type = CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR;
		//clExtMem.handle_ptr = hResult;
		//clExtMem.offset = 0;
		//clExtMem.size = 0;// sizeof(uint32_t)* m_imageSize.x* m_imageSize.y;

		/*m_resultOCL = clCreateImageFromExternalMemory(m_pClContext->GetContext(),
			&clExtMemProps[2],
			CL_MEM_READ_WRITE,
			clExtMem,
			&clImageFormat,
			&clImageDesc,
			&status);*/

		m_resultOCL = clCreateImageWithProperties(m_pClContext->GetContext(),
			clExtMemProps.data(),
			CL_MEM_READ_WRITE,
			&clImageFormat,
			&clImageDesc,
			nullptr,
			&status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);

		clExtMemProps[1] = (cl_mem_properties)hSource;
		m_sourceOCL = clCreateImageWithProperties(m_pClContext->GetContext(),
			clExtMemProps.data(),
			CL_MEM_READ_ONLY,
			&clImageFormat,
			&clImageDesc,
			nullptr,
			&status);
		XUSG_C_RETURN(status != CL_SUCCESS, false);
	}
	else
	{
		// Create DX11 resource
		CD3D11_TEXTURE2D_DESC texDesc(DXGI_FORMAT_R8G8B8A8_UNORM, m_imageSize.x, m_imageSize.y, 1, 1);
		texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		m_pClContext->GetDevice11()->CreateTexture2D(&texDesc, nullptr, m_shared11.put());

		// Share DX11 resource
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

		// Create DX11 textures with wrapped OpenCL texture
		XUSG_X_RETURN(m_source11, m_pClContext->CreateTexture2D11(DXGI_FORMAT_R8G8B8A8_UNORM, m_imageSize.x, m_imageSize.y,
			1, 1, D3D11_BIND_SHADER_RESOURCE, &m_sourceOCL, CL_MEM_READ_ONLY), false);
		XUSG_X_RETURN(m_result11, m_pClContext->CreateTexture2D11(DXGI_FORMAT_R8G8B8A8_UNORM, m_imageSize.x, m_imageSize.y,
			1, 1, D3D11_BIND_UNORDERED_ACCESS, &m_resultOCL, CL_MEM_WRITE_ONLY), false);
	}

	// Init OpenCL program
	XUSG_N_RETURN(InitOcl(), false);

	if (!USE_CL_KHR_EXTERNAL_MEM)
	{
		ResourceBarrier barrier;
		auto numBarriers = m_shared->SetBarrier(&barrier, ResourceState::COPY_DEST);
		pCommandList->Barrier(numBarriers, &barrier);
		pCommandList->CopyResource(m_shared.get(), source.get());
		numBarriers = m_shared->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
		pCommandList->Barrier(numBarriers, &barrier);
	}

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
	const char* clProgramString = R"(
#define GROUP_SIZE		8
#define RADIUS			16
#define SHARED_MEM_SIZE	(GROUP_SIZE + 2 * RADIUS)

#define DIV_UP(x, n)	(((x) + (n) - 1) / (n))

__constant sampler_t smpl = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

float GaussianSigmaFromRadius(float r)
{
	return (r + 1.0f) / 3.0f;
}

float Gaussian(float r, float sigma)
{
	const float a = r / sigma;

	return exp(-0.5f * a * a);
}

kernel void clKernel(__write_only image2d_t result, __read_only image2d_t source)
{
	local float4 srcs[SHARED_MEM_SIZE][SHARED_MEM_SIZE];
	local float4 dsts[SHARED_MEM_SIZE][GROUP_SIZE];

	const float2 imageSize = (float2)(get_image_width(result), get_image_width(result));
	const int2 DTid = (int2)(get_global_id(0), get_global_id(1));
	const int2 Gid = (int2)(get_group_id(0), get_group_id(1));
	const int2 GTid = (int2)(get_local_id(0), get_local_id(1));

	// Load data into group-shared memory
	const uint n = DIV_UP(SHARED_MEM_SIZE, GROUP_SIZE);
	const int2 uvStart = GROUP_SIZE * Gid - RADIUS;
	for (int i = 0; i < n; ++i)
	{
		const int x = GROUP_SIZE * i + GTid.x;
		if (x < SHARED_MEM_SIZE)
		{
			for (int j = 0; j < n; ++j)
			{
				const int y = GROUP_SIZE * j + GTid.y;
				if (y < SHARED_MEM_SIZE)
				{
					const int2 uv = uvStart + (int2)(x, y);
					srcs[y][x] = read_imagef(source, smpl, uv);
				}
			}
		}
	}

	work_group_barrier(CLK_LOCAL_MEM_FENCE);

	const float sigma = GaussianSigmaFromRadius(RADIUS);
	const int x = GTid.x + RADIUS;

	// Horizontal filter
	for (uint k = 0; k < n; ++k)
	{
		const uint y = GROUP_SIZE * k + GTid.y;
		if (y >= SHARED_MEM_SIZE) break;

		float4 mu = 0.0f;
		float ws = 0.0f;
		for (int i = -RADIUS; i <= RADIUS; ++i)
		{
			const int xi = x + i;
			const float4 src = srcs[y][xi];

			const float w = Gaussian(i, sigma);
			mu += src * w;
			ws += w;
		}

		dsts[y][GTid.x] = mu / ws;
	}

	work_group_barrier(CLK_LOCAL_MEM_FENCE);

	// Vertical filter
	const int y = GTid.y + RADIUS;

	float4 mu = 0.0f;
	float ws = 0.0f;
	for (int i = -RADIUS; i <= RADIUS; ++i)
	{
		const int yi = y + i;
		const float4 src = dsts[yi][GTid.x];

		const float w = Gaussian(i, sigma);
		mu += src * w;
		ws += w;
	}

	write_imagef(result, DTid,  mu / ws);
}
)";

	const size_t sourceSize = strlen(clProgramString);

	const auto clDevice = m_pClContext->GetDevice();
	auto clProgram = clCreateProgramWithSource(m_pClContext->GetContext(), 1, (const char**)&clProgramString, &sourceSize, &status);
	if (status != CL_SUCCESS)
	{
		cerr << "Error: clCreateProgramWithSource error" << endl;
		return false;
	}

	status = clBuildProgram(clProgram, 1, &clDevice, nullptr, nullptr, nullptr);
	if (status == CL_BUILD_PROGRAM_FAILURE)
		return HandleCompileError(clProgram, clDevice);

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
	if (!USE_CL_KHR_EXTERNAL_MEM)
	{
		const auto device11 = m_pClContext->GetDevice11();
		if (device11)
		{
			com_ptr<ID3D11DeviceContext> context;
			device11->GetImmediateContext(context.put());
			if (context) context->CopyResource(m_source11.get(), m_shared11.get());
		}
	}
}

void Ocl12::Process()
{
	cl_int status;
	const auto clQueue = m_pClContext->GetQueue();
	const cl_mem pResourcesOCL[] = { m_sourceOCL, m_resultOCL };
	const auto numResouces = static_cast<cl_uint>(size(pResourcesOCL));
	if (USE_CL_KHR_EXTERNAL_MEM)
	{
		status = clEnqueueAcquireExternalMemObjects(clQueue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
		if (status != CL_SUCCESS) cerr << "Error: clEnqueueAcquireExternalMemObjectsKHR fails" << endl;
	}
	else

	{
		status = clEnqueueAcquireD3D11Objects(clQueue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
		if (status != CL_SUCCESS) cerr << "Error: clEnqueueAcquireD3D11Objects fails" << endl;
	}

	status = clSetKernelArg(m_clKernel, 0, sizeof(cl_mem), &m_resultOCL);
	if (status != CL_SUCCESS) cerr << "Error: clSetKernelArg fails" << endl;

	status = clSetKernelArg(m_clKernel, 1, sizeof(cl_mem), &m_sourceOCL);
	if (status != CL_SUCCESS) cerr << "Error: clSetKernelArg fails" << endl;

	size_t globalDim[2];
	globalDim[0] = m_imageSize.y;
	globalDim[1] = m_imageSize.x;

	const size_t localDim[] = { 8, 8 };

	status = clEnqueueNDRangeKernel(clQueue, m_clKernel, 2, nullptr, globalDim, localDim, 0, nullptr, nullptr);
	if (status != CL_SUCCESS) cerr << "Error: clEnqueueNDRangeKernel fails" << endl;

	if (USE_CL_KHR_EXTERNAL_MEM)
	{
		status = clEnqueueReleaseExternalMemObjects(clQueue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
		if (status != CL_SUCCESS) cerr << "Error: clEnqueueReleaseExternalMemObjectsKHR fails" << endl;
	}
	else
	{
		status = clEnqueueReleaseD3D11Objects(clQueue, numResouces, pResourcesOCL, 0, nullptr, nullptr);
		if (status != CL_SUCCESS) cerr << "Error: clEnqueueReleaseD3D11Objects fails" << endl;
	}

	clFinish(clQueue);
}

void Ocl12::CopyToBackBuffer11(const com_ptr<ID3D11Resource>& backbuffer)
{
	const auto device11 = m_pClContext->GetDevice11();
	if (device11)
	{
		com_ptr<ID3D11DeviceContext> context;
		device11->GetImmediateContext(context.put());
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
