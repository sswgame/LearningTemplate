#include "pch.h"

#include "Engine/Graphics/RHI/RHI.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Shader/LiveShaderManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
	SW_LOG_CALLER( "RHI" );

	SW_GLOBAL_VARIABLE_ENUM( gv_rhiBackend, RHIBackend, RHIBackend::Vulkan, "Current RHI Backend" );
	SW_GLOBAL_VARIABLE_ENUM( gv_rhiCommandListMode, RHICommandListMode, RHICommandListMode::Deferred, "RHI 커맨드 리스트 모드: 0=Deferred, 1=Immediate" );

	RHIPipelineStateDesc::RHIPipelineStateDesc() noexcept
		: _topology{ RHIPrimitiveTopology::TriangleList }
		, _fillMode{ RHIFillMode::Solid }
		, _cullMode{ RHICullMode::None }
		, _numRenderTargets{ 1 }
		, _arrRtvFormats{}
		, _depthStencilFormat{ RHIFormat::D24_UNORM_S8_UINT }
		, _bEnableDepthTest{ 0 }
		, _bEnableDepthWrite{ 1 }
		, _bEnableBlend{ 0 }
		, _reservedFlags{ 0 }
	{
		for ( uint32 attachmentIndex = 0; attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
		{
			_arrRtvFormats[attachmentIndex] = RHIFormat::R8G8B8A8_UNORM;
		}
	}

	RHIRenderPassDesc::RHIRenderPassDesc() noexcept
		: _listColorAttachment{}
		, _clearDepth{ 1.0f }
		, _clearStencil{ 0 }
		, _bHasDepthStencil{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	RHITextureDesc::RHITextureDesc() noexcept
		: _width{ 1 }
		, _height{ 1 }
		, _depth{ 1 }
		, _mipLevels{ 1 }
		, _format{ RHIFormat::R8G8B8A8_UNORM }
		, _arrClearColor{ 0.0f, 0.0f, 0.0f, 0.0f }
		, _clearDepth{ 1.0f }
		, _clearStencil{ 0 }
		, _bIsRenderTarget{ 0 }
		, _bIsDepthStencil{ 0 }
		, _bIsShaderResource{ 1 }
		, _bIsUnorderedAccess{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	RHIRenderPassBeginInfo::RHIRenderPassBeginInfo() noexcept
		: _renderPass{ 0 }
		, _colorTarget{ 0 }
		, _arrColorTargets{}
		, _colorTargetCount{ 0 }
		, _depthTarget{ 0 }
		, _width{ 0 }
		, _height{ 0 }
		, _arrClearColor{ 0.1f, 0.1f, 0.1f, 1.0f }
		, _arrClearColors{}
		, _clearDepth{ 1.0f }
		, _loadOp{ RHIRenderPassLoadOp::Clear }
		, _arrLoadOps{}
		, _depthLoadOp{ RHIRenderPassLoadOp::Clear }
		, _bBindColor{ 1 }
		, _reservedFlags{ 0 }
	{
		for ( uint32 attachmentIndex = 0; attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
		{
			_arrClearColors[attachmentIndex][0] = 0.1f;
			_arrClearColors[attachmentIndex][1] = 0.1f;
			_arrClearColors[attachmentIndex][2] = 0.1f;
			_arrClearColors[attachmentIndex][3] = 1.0f;
			_arrLoadOps[attachmentIndex]		= RHIRenderPassLoadOp::Clear;
		}
	}

	RHI::RHI()
		: _liveShaderManager{ nullptr }
		, _device{ nullptr }
		, _pendingRHIBackend{ RHIBackend::DirectX12 }
		, _committedRHIBackend{ RHIBackend::DirectX12 }
		, _bPreferredVSync{ false }
		, _bPendingBackendChange{ false }
	{
	}

	RHI::~RHI() = default;

	bool RHI::initialize()
	{
		// Priority: explicit CLI > current GVM value > OS default > first available
		RHIBackend currentBackend = gv_rhiBackend;
		bool	   bCommandLineOverride{ false };

		const CommandLineManager& commandLineManager = engine::getCommandLineManager();
		bool					  bFlag{ false };
		if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
		{
			currentBackend		 = RHIBackend::DirectX11;
			bCommandLineOverride = true;
		}
		else if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
		{
			currentBackend		 = RHIBackend::DirectX12;
			bCommandLineOverride = true;
		}
		else if ( commandLineManager.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
		{
			currentBackend		 = RHIBackend::Vulkan;
			bCommandLineOverride = true;
		}
		else if ( commandLineManager.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
		{
			currentBackend		 = RHIBackend::OpenGL;
			bCommandLineOverride = true;
		}

		if ( bCommandLineOverride == false )
		{
			if ( RHIAvailability::isAvailable( currentBackend ) == false )
				currentBackend = getDefaultPlatformBackend();
		}

		if ( RHIAvailability::isAvailable( currentBackend ) == false )
		{
			SW_LOG_ERROR( "Requested RHI backend is unavailable on this platform." );
			return false;
		}

		gv_rhiBackend = currentBackend;
		SW_LOG_INFO( "Initializing RHI with backend: %#", getBackendTypeName( currentBackend ) );

		_device = createDevice( currentBackend );
		if ( _device == nullptr )
		{
			SW_LOG_ERROR( "Failed to create RHI Device!" );
			return false;
		}

		IWindow* pWindow = IWindow::getActiveWindow();
		if ( pWindow != nullptr )
			_device->setInitWindow( pWindow );
		_device->setPreferredVSync( _bPreferredVSync );

		if ( _device->initialize() == false )
		{
			SW_LOG_ERROR( "Failed to initialize RHI Device!" );
			_device.reset();
			return false;
		}

		_device->setDefaultCommandListMode( gv_rhiCommandListMode );

#if defined( SW_DEBUG )
		_liveShaderManager = make_unique<LiveShaderManager>();
		if ( _liveShaderManager->initialize( "Shaders" ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize LiveShaderManager!" );
			_device.reset();
			return false;
		}
#endif

		SW_LOG_INFO( "RHI initialized successfully." );
		_committedRHIBackend = currentBackend;
		_pendingRHIBackend	 = currentBackend;
		return true;
	}

	void RHI::shutdown()
	{
#if defined( SW_DEBUG )
		if ( _liveShaderManager != nullptr )
			_liveShaderManager->shutdown();
		_liveShaderManager.reset();
#endif
		if ( _device != nullptr )
		{
			_device->shutdown();
			_device.reset();
		}
		// After devices are gone, drop MODULE DLLs (DX11/DX12) so FreeLibrary is safe.
		engine::getRHIBackendRegistry().unloadModules();
	}

	bool RHI::recreateDevice( RHIBackend backend )
	{
		if ( RHIAvailability::isAvailable( backend ) == false )
		{
			SW_LOG_ERROR( "recreateDevice: backend unavailable" );
			return false;
		}

		IWindow*		 pWindow		 = IWindow::getActiveWindow();
		const RHIBackend previousBackend = _committedRHIBackend;

		if ( _device )
		{
			_device->waitIdle();
			_device->shutdown();
			_device.reset();
		}

		const RHICapabilities currentCaps  = RHIAvailability::query( backend );
		const RHICapabilities previousCaps = RHIAvailability::query( previousBackend );
		if ( ( currentCaps._bRequiresWindowRecreate != 0 || previousCaps._bRequiresWindowRecreate != 0 ) && pWindow != nullptr )
		{
			pWindow->recreate();
		}

		gv_rhiBackend = backend;
		_device		  = createDevice( backend );
		if ( _device == nullptr )
			return false;

		if ( pWindow != nullptr )
			_device->setInitWindow( pWindow );
		_device->setPreferredVSync( _bPreferredVSync );

		if ( _device->initialize() == false )
		{
			_device.reset();
			return false;
		}

		_device->setDefaultCommandListMode( gv_rhiCommandListMode );

		SW_LOG_INFO( "Soft-recreated device: %#", getBackendTypeName( backend ) );
		_committedRHIBackend = backend;
		_pendingRHIBackend	 = backend;
		return true;
	}

	void RHI::schedulePendingBackendChange( RHIBackend requested )
	{
		if ( RHIAvailability::isAvailable( requested ) == false )
		{
			SW_LOG_WARNING( "Backend %# unavailable — reverting.", static_cast<int32>( requested ) );
			gv_rhiBackend = _committedRHIBackend;
			return;
		}

		if ( requested == _committedRHIBackend )
			return;

		SW_LOG_INFO( "RHI Backend change queued: %# → %#", getBackendTypeName( _committedRHIBackend ), getBackendTypeName( requested ) );
		_pendingRHIBackend	   = requested;
		_bPendingBackendChange = true;
	}

	RHIBackend RHI::consumePendingBackendChange()
	{
		_bPendingBackendChange = false;
		return _pendingRHIBackend;
	}

	unique_ptr<IRHIDevice> RHI::createDevice( RHIBackend backend )
	{
		return engine::getRHIBackendRegistry().createDevice( backend );
	}

	const utf8* RHI::getBackendTypeName( RHIBackend backend )
	{
		const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( "RHIBackend" ) );

		if ( pInfo != nullptr )
		{
			hashed_string name = pInfo->toString( static_cast<int64>( backend ) );
			if ( name.empty() == false )
				return name.c_str();
		}

		switch ( backend )
		{
			case RHIBackend::DirectX11:
				return "DirectX11";
			case RHIBackend::DirectX12:
				return "DirectX12";
			case RHIBackend::Vulkan:
				return "Vulkan";
			case RHIBackend::OpenGL:
				return "OpenGL";
		}
		return "Unknown";
	}

	RHIBackend RHI::getDefaultPlatformBackend()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return RHIBackend::DirectX12;
#elif defined( SW_PLATFORM_LINUX )
		return RHIBackend::Vulkan;
#else
		return RHIBackend::OpenGL;
#endif
	}

	ShaderTargetFormat RHI::getShaderTargetFormat( RHIBackend backend )
	{
		switch ( backend )
		{
			case RHIBackend::DirectX11:
				return ShaderTargetFormat::DXBC_D3D11;
			case RHIBackend::DirectX12:
				return ShaderTargetFormat::DXIL_D3D12;
			case RHIBackend::Vulkan:
				return ShaderTargetFormat::SPIRV_Vulkan;
			case RHIBackend::OpenGL:
				return ShaderTargetFormat::SPIRV_OpenGL;
		}
		return ShaderTargetFormat::DXIL_D3D12;
	}
} // namespace sw
