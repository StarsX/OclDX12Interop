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
clEnqueueAcquireExternalMemObjectsKHR_fn clEnqueueAcquireExternalMemObjects;
clEnqueueReleaseExternalMemObjectsKHR_fn clEnqueueReleaseExternalMemObjects;
//clCreateImageFromExternalMemoryKHR_fn clCreateImageFromExternalMemory;

OclDX12Interop::OclDX12Interop(uint32_t width, uint32_t height, wstring name) :
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

OclDX12Interop::~OclDX12Interop()
{
#if defined (_DEBUG)
	FreeConsole();
#endif
}

void OclDX12Interop::OnInit()
{
	Texture::sptr source;
	vector<Resource::uptr> uploaders(0);
	LoadPipeline(source, uploaders);
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void OclDX12Interop::LoadPipeline(Texture::sptr& source, vector<Resource::uptr>& uploaders)
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
	XUSG_N_RETURN(m_commandQueue->Create(m_device.get(), CommandListType::DIRECT, CommandQueueFlag::NONE,
		0, 0, L"CommandQueue"), ThrowIfFailed(E_FAIL));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	// Create a command allocator for each frame.
	for (uint8_t n = 0u; n < FrameCount; ++n)
	{
		m_commandAllocators[n] = CommandAllocator::MakeUnique();
		XUSG_N_RETURN(m_commandAllocators[n]->Create(m_device.get(), CommandListType::DIRECT,
			(L"CommandAllocator" + to_wstring(n)).c_str()), ThrowIfFailed(E_FAIL));
	}

	// Create the command list.
	m_commandList = CommandList::MakeUnique();
	const auto pCommandList = m_commandList.get();
	XUSG_N_RETURN(pCommandList->Create(m_device.get(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));

	// Create OpenCL context from DX11 device
	XUSG_N_RETURN(m_oclContext.Init(dxgiAdapter.get()), ThrowIfFailed(E_FAIL));

	m_ocl12 = make_unique<Ocl12>(m_oclContext);
	if (!m_ocl12->Init(pCommandList, source, uploaders, Format::R8G8B8A8_UNORM, m_fileName.c_str()))
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
	const auto pSwapChainDevice = USE_CL_KHR_EXTERNAL_MEM ? m_commandQueue->GetHandle() : m_oclContext.GetDevice11().get();
	m_swapChain = SwapChain::MakeUnique();
	XUSG_N_RETURN(m_swapChain->Create(factory.get(), Win32Application::GetHwnd(), pSwapChainDevice,
		FrameCount, m_width, m_height, Format::R8G8B8A8_UNORM, SwapChainFlag::ALLOW_TEARING), ThrowIfFailed(E_FAIL));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create frame resources.
	// Create a RTV for each frame.
	if (USE_CL_KHR_EXTERNAL_MEM)
	{
		for (uint8_t n = 0; n < FrameCount; ++n)
		{

			m_renderTargets[n] = RenderTarget::MakeUnique();
			XUSG_N_RETURN(m_renderTargets[n]->CreateFromSwapChain(m_device.get(), m_swapChain.get(), n), ThrowIfFailed(E_FAIL));
		}
	}
	else
	{
		// Create DX11 backbuffer resource
		const auto pDxgiSwapChain = static_cast<IDXGISwapChain3*>(m_swapChain->GetHandle());
		ThrowIfFailed(pDxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer11)));
	}
}

// Load the sample assets.
void OclDX12Interop::LoadAssets()
{
	// Close the command list and execute it to begin the initial GPU setup.
	XUSG_N_RETURN(m_commandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if (!m_fence)
		{
			m_fence = Fence::MakeUnique();
			XUSG_N_RETURN(m_fence->Create(m_device.get(), m_fenceValues[m_frameIndex]++, FenceFlag::NONE, L"Fence"), ThrowIfFailed(E_FAIL));
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
void OclDX12Interop::OnUpdate()
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
void OclDX12Interop::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	m_ocl12->Process();
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	if (!USE_CL_KHR_EXTERNAL_MEM) m_ocl12->CopyToBackBuffer11(m_backBuffer11);

	// Present the frame.
	XUSG_N_RETURN(m_swapChain->Present(0, PresentFlag::ALLOW_TEARING), ThrowIfFailed(E_FAIL));

	MoveToNextFrame();
}

void OclDX12Interop::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);

	m_oclContext.Destroy();
}

// User hot-key interactions.
void OclDX12Interop::OnKeyUp(uint8_t key)
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

void OclDX12Interop::ParseCommandLineArgs(wchar_t* argv[], int argc)
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

void OclDX12Interop::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	const auto pCommandAllocator = m_commandAllocators[m_frameIndex].get();
	XUSG_N_RETURN(pCommandAllocator->Reset(), ThrowIfFailed(E_FAIL));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	const auto pCommandList = m_commandList.get();
	XUSG_N_RETURN(pCommandList->Reset(pCommandAllocator, nullptr), ThrowIfFailed(E_FAIL));

	if (USE_CL_KHR_EXTERNAL_MEM)
	{
		// Record commands.
		ResourceBarrier barriers[2];
		auto numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::COPY_DEST);
		pCommandList->Barrier(numBarriers, barriers);

		pCommandList->CopyResource(m_renderTargets[m_frameIndex].get(), m_ocl12->GetResult());

		numBarriers = m_renderTargets[m_frameIndex]->SetBarrier(barriers, ResourceState::PRESENT);
		//numBarriers = m_ocl12->GetResult()->SetBarrier(barriers, ResourceState::COMMON, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);
	}

	XUSG_N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
}

// Wait for pending GPU work to complete.
void OclDX12Interop::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	XUSG_N_RETURN(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]), ThrowIfFailed(E_FAIL));

	// Wait until the fence has been processed.
	XUSG_N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void OclDX12Interop::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const auto currentFenceValue = m_fenceValues[m_frameIndex];
	XUSG_N_RETURN(m_commandQueue->Signal(m_fence.get(), currentFenceValue), ThrowIfFailed(E_FAIL));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		XUSG_N_RETURN(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), ThrowIfFailed(E_FAIL));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

double OclDX12Interop::CalculateFrameStats(float* pTimeStep)
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
