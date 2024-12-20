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

#pragma once

#include "DXFramework.h"
#include "StepTimer.h"
#include "Ocl12.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().

class OclDX12Interop : public DXFramework
{
public:
	OclDX12Interop(uint32_t width, uint32_t height, std::wstring name);
	virtual ~OclDX12Interop();

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	virtual void OnKeyUp(uint8_t /*key*/);

	virtual void ParseCommandLineArgs(wchar_t* argv[], int argc);

private:
	enum DeviceType : uint8_t
	{
		DEVICE_DISCRETE,
		DEVICE_UMA,
		DEVICE_WARP
	};

	enum CL_DX11_EXT
	{
		CL_DX11_EXT_NONE,
		CL_DX11_EXT_KHR,
		CL_DX11_EXT_NV
	};

	static const uint8_t FrameCount = 3;

	XUSG::SwapChain::uptr			m_swapChain;
	XUSG::CommandAllocator::uptr	m_commandAllocators[FrameCount];
	XUSG::CommandQueue::uptr		m_commandQueue;

	XUSG::Device::uptr				m_device;
	XUSG::RenderTarget::uptr		m_renderTargets[FrameCount];
	XUSG::CommandList::uptr			m_commandList;

	XUSG::com_ptr<ID3D11Resource>	m_backBuffer11;

	XUSG::OclContext11 m_oclContext;

	// App resources.
	std::unique_ptr<Ocl12> m_ocl12;

	// Synchronization objects.
	uint32_t	m_frameIndex;
	HANDLE		m_fenceEvent;
	XUSG::Fence::uptr m_fence;
	uint64_t	m_fenceValues[FrameCount];

	// Application state
	DeviceType	m_deviceType;
	StepTimer	m_timer;
	CL_DX11_EXT m_clDX11Ext;
	bool		m_showFPS;
	bool		m_isPaused;

	// User external settings
	std::wstring m_fileName;

	void LoadPipeline(XUSG::Texture::sptr& source, std::vector<XUSG::Resource::uptr>& uploaders);
	void LoadAssets();

	void PopulateCommandList();
	void WaitForGpu();
	void MoveToNextFrame();
	double CalculateFrameStats(float* fTimeStep = nullptr);
};
