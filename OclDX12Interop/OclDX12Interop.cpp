//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "OclDX12Interop.h"

using namespace std;
using namespace XUSG;

clCreateFromD3D11Texture2DKHR_fn clCreateFromD3D11Texture2D;
clEnqueueAcquireD3D11ObjectsKHR_fn clEnqueueAcquireD3D11Objects;
clEnqueueReleaseD3D11ObjectsKHR_fn clEnqueueReleaseD3D11Objects;

SyclDX12Interop::SyclDX12Interop(uint32_t width, uint32_t height, wstring name) :
	DXFramework(width, height, name),
	m_frameIndex(0),
	m_showFPS(true),
	m_fileName(L"Assets/Sashimi.dds")
{
#if defined (_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w+t", stdout);
	freopen_s(&stream, "CONIN$", "r+t", stdin);
	freopen_s(&stream, "CONOUT$", "w+t", stderr);
#endif
}

SyclDX12Interop::~SyclDX12Interop()
{
#if defined (_DEBUG)
	FreeConsole();
#endif
}

//if an error occurs we exit
//it would be better to cleanup state then exit, for sake of simplicity going to omit the cleanup
cl_int testStatus(int status, char* errorMsg)
{
	if (status != CL_SUCCESS)
	{
		if (errorMsg == nullptr) cerr << "Error" << endl;
		else cerr << "Error: " << errorMsg << endl;
		ThrowIfFailed(E_FAIL);
	}

	return status;
}

cl_int SyclDX12Interop::InitCL(Ocl12::OclContext& oclContext, const ID3D11Device* pd3dDevice)
{
	cl_uint numPlatforms = 0;

	// [1] get the platform
	auto status = clGetPlatformIDs(0, nullptr, &numPlatforms);
	C_RETURN(testStatus(status, "clGetPlatformIDs error"), status);

	vector<cl_platform_id> platforms(numPlatforms);
	status = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
	C_RETURN(testStatus(status, "clGetPlatformIDs error"), status);

	// [2] get device ids for the platform i have obtained
	vector<cl_device_id> devices;
	for (const auto& platform : platforms)
	{
		size_t platformNameSize = 0;
		status = clGetPlatformInfo(platform, CL_PLATFORM_NAME, 0, nullptr, &platformNameSize);
		C_RETURN(testStatus(status, "clGetPlatformInfo error"), status);

		string platformName(platformNameSize, '\0');
		status = clGetPlatformInfo(platform, CL_PLATFORM_NAME, platformName.size(), &platformName[0], nullptr);
		C_RETURN(testStatus(status, "clGetPlatformInfo error"), status);

		size_t extensionsSize = 0;
		status = clGetPlatformInfo(platform, CL_PLATFORM_EXTENSIONS, 0, nullptr, &extensionsSize);
		C_RETURN(testStatus(status, "clGetPlatformInfo error"), status);

		string extensions(extensionsSize, '\0');
		status = clGetPlatformInfo(platform, CL_PLATFORM_EXTENSIONS, extensions.size(), &extensions[0], nullptr);
		C_RETURN(testStatus(status, "clGetPlatformInfo error"), status);
		cout << "Platform " << platformName << "extensions supported: " << extensions << endl;

		const char* extKHR = strstr(extensions.c_str(), "cl_khr_d3d11_sharing");
		const char* extNV = strstr(extensions.c_str(), "cl_nv_d3d11_sharing");
		if (extKHR) m_clDX11Ext = CL_DX11_EXT_KHR;
		else if (extNV) m_clDX11Ext = CL_DX11_EXT_NV;
		else
		{
			cerr << "Platform " << platformName << " does support any cl_*_d3d11_sharing" << endl;

			return CL_INVALID_PLATFORM;
		}

		assert(m_clDX11Ext == CL_DX11_EXT_KHR || m_clDX11Ext == CL_DX11_EXT_NV);
		static_assert(CL_D3D11_DEVICE_KHR == CL_D3D11_DEVICE_NV, "CL_D3D11_DEVICE_KHR == CL_D3D11_DEVICE_NV");
		static_assert(CL_PREFERRED_DEVICES_FOR_D3D11_KHR == CL_PREFERRED_DEVICES_FOR_D3D11_NV, "CL_PREFERRED_DEVICES_FOR_D3D11_KHR == CL_PREFERRED_DEVICES_FOR_D3D11_NV");
		static_assert(CL_CONTEXT_D3D11_DEVICE_KHR == CL_CONTEXT_D3D11_DEVICE_NV, "CL_CONTEXT_D3D11_DEVICE_KHR == CL_CONTEXT_D3D11_DEVICE_NV");

		const auto clGetDeviceIDsFromD3D11 = m_clDX11Ext == CL_DX11_EXT_KHR ?
			(clGetDeviceIDsFromD3D11KHR_fn)clGetExtensionFunctionAddressForPlatform(platform, "clGetDeviceIDsFromD3D11KHR") :
			(clGetDeviceIDsFromD3D11NV_fn)clGetExtensionFunctionAddressForPlatform(platform, "clGetDeviceIDsFromD3D11NV");

		X_RETURN(clCreateFromD3D11Texture2D, m_clDX11Ext == CL_DX11_EXT_KHR ?
			(clCreateFromD3D11Texture2DKHR_fn)clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromD3D11Texture2DKHR") :
			(clCreateFromD3D11Texture2DNV_fn)clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromD3D11Texture2DNV"), CL_INVALID_PLATFORM);

		X_RETURN(clEnqueueAcquireD3D11Objects, m_clDX11Ext == CL_DX11_EXT_KHR ?
			(clEnqueueAcquireD3D11ObjectsKHR_fn)clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireD3D11ObjectsKHR") :
			(clEnqueueAcquireD3D11ObjectsNV_fn)clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireD3D11ObjectsNV"), CL_INVALID_PLATFORM);

		X_RETURN(clEnqueueReleaseD3D11Objects, m_clDX11Ext == CL_DX11_EXT_KHR ?
			(clEnqueueReleaseD3D11ObjectsKHR_fn)clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseD3D11ObjectsKHR") :
			(clEnqueueReleaseD3D11ObjectsKHR_fn)clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseD3D11ObjectsNV"), CL_INVALID_PLATFORM);

		cl_uint numDevices = 0;
		status = clGetDeviceIDsFromD3D11(platform, CL_D3D11_DEVICE_KHR, (void*)pd3dDevice, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 0, nullptr, &numDevices);
		C_RETURN(testStatus(status, "Failed on clGetDeviceIDsFromD3D11"), status);
		
		if (numDevices > 0)
		{
			devices.resize(numDevices);
			status = clGetDeviceIDsFromD3D11(platform, CL_D3D11_DEVICE_KHR, (void*)pd3dDevice, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, numDevices, devices.data(), nullptr);
			C_RETURN(testStatus(status, "Failed on clGetDeviceIDsFromD3D11"), status);

			// create an OCL context from the device we are using as our DX11 rendering device
			cl_context_properties cps[] =
			{
				CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
				CL_CONTEXT_D3D11_DEVICE_KHR, (cl_context_properties)pd3dDevice,
				//CL_CONTEXT_INTEROP_USER_SYNC, CL_FALSE,
				0
			};
			oclContext.Context = clCreateContext(cps, numDevices, devices.data(), nullptr, nullptr, &status);
			C_RETURN(testStatus(status, "clCreateContext error"), status);

			for (const auto& device : devices)
			{
				size_t deviceNameSize = 0;
				status = clGetDeviceInfo(device, CL_DEVICE_NAME, 0, nullptr, &deviceNameSize);
				C_RETURN(testStatus(status, "clGetDeviceInfo error"), status);

				string deviceName(deviceNameSize, '\0');
				status = clGetDeviceInfo(device, CL_DEVICE_NAME, deviceName.size(), &deviceName[0], nullptr);
				C_RETURN(testStatus(status, "clGetDeviceInfo error"), status);

				size_t extensionsSize = 0;
				status = clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, nullptr, &extensionsSize);
				C_RETURN(testStatus(status, "clGetDeviceInfo error"), status);

				string extensions(extensionsSize, '\0');
				status = clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, extensions.size(), &extensions[0], nullptr);
				C_RETURN(testStatus(status, "clGetDeviceInfo error"), status);
				cout << "Device " << deviceName << "extensions supported: " << extensions << endl;
			}

			//create an openCL commandqueue
			//the queue and move on, the sample is about sharing, not about robust device call/response/create patterns
			oclContext.Queue = clCreateCommandQueueWithProperties(oclContext.Context, devices[0], 0, &status);
			//clQueue = clCreateCommandQueue(clContext, devices[0], 0, &status);
			C_RETURN(testStatus(status, "clCreateCommandQueue error"), status);

			oclContext.Device = devices[0];

			break;
		}
	}

	return CL_SUCCESS;
}

void SyclDX12Interop::OnInit()
{
	Texture::sptr source;
	vector<Resource::uptr> uploaders(0);
	LoadPipeline(source, uploaders);
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void SyclDX12Interop::LoadPipeline(Texture::sptr& source, vector<Resource::uptr>& uploaders)
{
	auto dxgiFactoryFlags = 0u;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		com_ptr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			//debugController->SetEnableGPUBasedValidation(TRUE);

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	com_ptr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	DXGI_ADAPTER_DESC1 dxgiAdapterDesc;
	com_ptr<IDXGIAdapter1> dxgiAdapter;
	auto hr = DXGI_ERROR_UNSUPPORTED;
	for (auto i = 0u; hr == DXGI_ERROR_UNSUPPORTED; ++i)
	{
		dxgiAdapter = nullptr;
		ThrowIfFailed(factory->EnumAdapters1(i, dxgiAdapter.put()));

		m_device = Device::MakeUnique();
		hr = m_device->Create(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0);
	}

	dxgiAdapter->GetDesc1(&dxgiAdapterDesc);
	if (dxgiAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		m_title += dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c ? L" (WARP)" : L" (Software)";
	ThrowIfFailed(hr);

	// Create the command queue.
	m_commandQueue = CommandQueue::MakeUnique();
	N_RETURN(m_commandQueue->Create(m_device.get(), CommandListType::DIRECT, CommandQueueFlag::NONE,
		0, 0, L"CommandQueue"), ThrowIfFailed(E_FAIL));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	// Create a command allocator for each frame.
	for (uint8_t n = 0u; n < FrameCount; ++n)
	{
		m_commandAllocators[n] = CommandAllocator::MakeUnique();
		N_RETURN(m_commandAllocators[n]->Create(m_device.get(), CommandListType::DIRECT,
			(L"CommandAllocator" + to_wstring(n)).c_str()), ThrowIfFailed(E_FAIL));
	}

	// Create the command list.
	m_commandList = CommandList::MakeUnique();
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Create(m_device.get(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));

	// Create DX11on12 device
	const auto pCommandQueue = reinterpret_cast<ID3D12CommandQueue*>(m_commandQueue->GetHandle());
	uint32_t d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
	d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	com_ptr<ID3D11Device> d3d11Device;
	/*ThrowIfFailed(D3D11On12CreateDevice(
		static_cast<ID3D12Device*>(m_device->GetHandle()),
		d3d11DeviceFlags,
		nullptr,
		0,
		reinterpret_cast<IUnknown* const*>(&pCommandQueue),
		1,
		0,
		&d3d11Device,
		nullptr,
		nullptr
	));*/
	ThrowIfFailed(D3D11CreateDevice(dxgiAdapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
		d3d11DeviceFlags, nullptr, 0, D3D11_SDK_VERSION, d3d11Device.put(), nullptr, nullptr));

	// Create OpenCL context from DX11 device
	if (InitCL(m_oclContext, d3d11Device.get()) != CL_SUCCESS) ThrowIfFailed(E_FAIL);

	m_ocl12 = make_unique<Ocl12>(m_oclContext, d3d11Device);
	if (!m_ocl12) ThrowIfFailed(E_FAIL);

	if (!m_ocl12->Init(pCommandList, source, uploaders, Format::B8G8R8A8_UNORM, m_fileName.c_str()))
		ThrowIfFailed(E_FAIL);
	
	m_ocl12->GetImageSize(m_width, m_height);

	// Resize window
	{
		RECT windowRect;
		GetWindowRect(Win32Application::GetHwnd(), &windowRect);
		windowRect.right = windowRect.left + m_width;
		windowRect.bottom = windowRect.top + m_height;

		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
		SetWindowPos(Win32Application::GetHwnd(), HWND_TOP, windowRect.left, windowRect.top,
			windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0);
	}

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.SampleDesc.Count = 1;

	com_ptr<IDXGISwapChain1> swapChain = nullptr;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		d3d11Device.get(),
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		swapChain.put()
	));
	ThrowIfFailed(swapChain->QueryInterface(m_dxgiSwapChain.put()));

	//m_swapChain = SwapChain::MakeUnique();
	//N_RETURN(m_swapChain->Create(factory.get(), Win32Application::GetHwnd(), m_commandQueue.get(),
		//FrameCount, m_width, m_height, Format::B8G8R8A8_UNORM), ThrowIfFailed(E_FAIL));

	//m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_frameIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

	// Create frame resources.
	// Create a RTV for each frame.
	com_ptr<ID3D11Device1> device11;
	const auto pDevice12 = static_cast<ID3D12Device*>(m_device->GetHandle());
	ThrowIfFailed(d3d11Device->QueryInterface(device11.put()));
	for (uint8_t n = 0; n < FrameCount; ++n)
	{
		//m_renderTargets[n] = RenderTarget::MakeUnique();
		//N_RETURN(m_renderTargets[n]->CreateFromSwapChain(m_device.get(), m_swapChain.get(), n), ThrowIfFailed(E_FAIL));

		// Create DX11 backbuffer resource
		auto resource = Resource::MakeUnique();
		ThrowIfFailed(m_dxgiSwapChain->GetBuffer(n, IID_PPV_ARGS(&m_backBuffers11[n])));
	}
}

// Load the sample assets.
void SyclDX12Interop::LoadAssets()
{
	//array_view = 

	// Close the command list and execute it to begin the initial GPU setup.
	N_RETURN(m_commandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if (!m_fence)
		{
			m_fence = Fence::MakeUnique();
			N_RETURN(m_fence->Create(m_device.get(), m_fenceValues[m_frameIndex]++, FenceFlag::NONE, L"Fence"), ThrowIfFailed(E_FAIL));
		}

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

	m_ocl12->Init11();
}

// Update frame-based values.
void SyclDX12Interop::OnUpdate()
{
	// Timer
	static auto time = 0.0, pauseTime = 0.0;

	m_timer.Tick();
	float timeStep;
	const auto totalTime = CalculateFrameStats(&timeStep);
	pauseTime = m_isPaused ? totalTime - time : pauseTime;
	timeStep = m_isPaused ? 0.0f : timeStep;
	time = totalTime - pauseTime;
}

// Render the scene.
void SyclDX12Interop::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	m_ocl12->Process();
	m_commandQueue->ExecuteCommandList(m_commandList.get());
	m_ocl12->CopyToBackBuffer11(m_backBuffers11[m_frameIndex]);

	// Present the frame.
	//N_RETURN(m_swapChain->Present(0, 0), ThrowIfFailed(E_FAIL));
	ThrowIfFailed(m_dxgiSwapChain->Present(0, 0));

	MoveToNextFrame();
}

void SyclDX12Interop::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);

	auto status = clReleaseCommandQueue(m_oclContext.Queue);
	testStatus(status, "Fail to releasing queue");

	status = clReleaseContext(m_oclContext.Context);
	testStatus(status, "Fail to releasing context");
}

// User hot-key interactions.
void SyclDX12Interop::OnKeyUp(uint8_t key)
{
	switch (key)
	{
	case VK_SPACE:
		m_isPaused = !m_isPaused;
		break;
	case VK_F1:
		m_showFPS = !m_showFPS;
		break;
	case VK_ESCAPE:
		PostQuitMessage(0);
		break;
	}
}

void SyclDX12Interop::ParseCommandLineArgs(wchar_t* argv[], int argc)
{
	DXFramework::ParseCommandLineArgs(argv, argc);

	for (auto i = 1; i < argc; ++i)
	{
		if (_wcsnicmp(argv[i], L"-image", wcslen(argv[i])) == 0 ||
			_wcsnicmp(argv[i], L"/image", wcslen(argv[i])) == 0)
		{
			if (i + 1 < argc) m_fileName = argv[i + 1];
		}
	}
}

void SyclDX12Interop::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	const auto pCommandAllocator = m_commandAllocators[m_frameIndex].get();
	N_RETURN(pCommandAllocator->Reset(), ThrowIfFailed(E_FAIL));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	const auto pCommandList = m_commandList.get();
	N_RETURN(pCommandList->Reset(pCommandAllocator, nullptr), ThrowIfFailed(E_FAIL));

	// Record commands.
	/*ResourceBarrier barrier;
	auto numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(&barrier, ResourceState::COPY_DEST);
	pCommandList->Barrier(numBarriers, &barrier);

	pCommandList->CopyResource(m_renderTargets[m_frameIndex].get(), m_ocl12->GetResult());

	numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(&barrier, ResourceState::PRESENT);
	pCommandList->Barrier(numBarriers, &barrier);*/

	N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
}

// Wait for pending GPU work to complete.
void SyclDX12Interop::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	N_RETURN(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]), ThrowIfFailed(E_FAIL));

	// Wait until the fence has been processed.
	N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void SyclDX12Interop::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const auto currentFenceValue = m_fenceValues[m_frameIndex];
	N_RETURN(m_commandQueue->Signal(m_fence.get(), currentFenceValue), ThrowIfFailed(E_FAIL));

	// Update the frame index.
	//m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_frameIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

double SyclDX12Interop::CalculateFrameStats(float* pTimeStep)
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0;
	static double previousTime = 0.0;
	const auto totalTime = m_timer.GetTotalSeconds();
	++frameCnt;

	const auto timeStep = static_cast<float>(totalTime - elapsedTime);

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float fps = static_cast<float>(frameCnt) / timeStep;	// Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		wstringstream windowText;
		windowText << L"    fps: ";
		if (m_showFPS) windowText << setprecision(2) << fixed << fps;
		else windowText << L"[F1]";

		SetCustomWindowText(windowText.str().c_str());
	}

	if (pTimeStep)* pTimeStep = static_cast<float>(totalTime - previousTime);
	previousTime = totalTime;

	return totalTime;
}
