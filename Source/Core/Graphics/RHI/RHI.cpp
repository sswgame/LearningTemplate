#include "RHI.h"
#include "RHIBackendRegistry.h"
#include "RHICapabilities.h"
#include "Core/Graphics/Shader/LiveShaderManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Window/IWindow.h"
#include "Core/Utility/Log/Logger.h"

/**
 * @file RHI.cpp
 * @brief RHI 팩토리 클래스 구현
 */

namespace sw
{
	RHICapabilities::RHICapabilities() noexcept
		: _bBindless{ 0 }
		, _bCompute{ 1 }
		, _bOffscreenRT{ 0 }
		, _bImGuiHooks{ 0 }
		, _bEditorSupported{ 0 }
		, _bComputeRootConstants{ 0 }
		, _reserved{ 0 }
	{
	}

	RHIPipelineStateDesc::RHIPipelineStateDesc() noexcept
		: _bEnableDepthTest{ 0 }
		, _bEnableBlend{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	RHIRenderPassDesc::RHIRenderPassDesc() noexcept
		: _bHasDepthStencil{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	RHITextureDesc::RHITextureDesc() noexcept
		: _bIsRenderTarget{ 0 }
		, _bIsDepthStencil{ 0 }
		, _bIsShaderResource{ 1 }
		, _bIsUnorderedAccess{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	namespace
	{
		void registerRHIBackendEnum( TypeRegistry& registry )
		{
			EnumInfo info{};
			info._name				 = hashed_string( "RHIBackend" );
			info._fullyQualifiedName = hashed_string( "sw::RHIBackend" );
			info._moduleName		 = hashed_string( "Core" );
			info._bIsBitFlag		 = 0;

			const auto add = [&]( const utf8* name, RHIBackend value )
			{
				const int64			v = static_cast<int64>( value );
				const hashed_string key( name );
				info._mapNameToValue[key] = v;
				// Prefer primary names for value→name (skip aliases).
				if ( info._mapValueToName.find( v ) == info._mapValueToName.end() )
					info._mapValueToName[v] = key;
			};

			add( "DirectX11", RHIBackend::DirectX11 );
			add( "DirectX12", RHIBackend::DirectX12 );
			add( "Vulkan", RHIBackend::Vulkan );
			add( "OpenGL", RHIBackend::OpenGL );
			registry.registerEnum( info );
		}

		EnumRegistrar s_rhiBackendEnumRegistrar{ &registerRHIBackendEnum };
	} // namespace

	SW_GLOBAL_VARIABLE_ENUM( gv_RHIBackend, RHIBackend, RHIBackend::DirectX12, "Current RHI Backend" );

	bool RHI::initialize()
	{
		SW_LOG_INFO( "RHI::initialize called" );

		try
		{
			// Priority: explicit CLI > current GVM value > OS default > first available
			RHIBackend initialBackend = gv_RHIBackend;
			bool	   bCliOverride	  = false;

			CommandLineManager& commandLineManager = core::getCommandLineManager();
			bool				bFlag			   = false;
			if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
			{
				initialBackend = RHIBackend::DirectX11;
				bCliOverride   = true;
			}
			else if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
			{
				initialBackend = RHIBackend::DirectX12;
				bCliOverride   = true;
			}
			else if ( commandLineManager.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
			{
				initialBackend = RHIBackend::Vulkan;
				bCliOverride   = true;
			}
			else if ( commandLineManager.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
			{
				initialBackend = RHIBackend::OpenGL;
				bCliOverride   = true;
			}

			if ( bCliOverride == false )
			{
				if ( RHIAvailability::isAvailable( initialBackend ) == false )
					initialBackend = getDefaultPlatformBackend();
			}

			if ( RHIAvailability::isAvailable( initialBackend ) == false )
			{
				SW_LOG_ERROR( "Requested RHI backend is unavailable on this platform." );
				return false;
			}

			gv_RHIBackend = initialBackend;

			SW_LOG_INFO( "Initializing RHI with backend: %s", getBackendTypeName( initialBackend ) );

			_device = createDevice( initialBackend );
			if ( _device == nullptr )
			{
				SW_LOG_ERROR( "Failed to create RHI Device!" );
				return false;
			}

			if ( IWindow* window = IWindow::getActiveWindow() )
				_device->setInitWindow( window );

			if ( _device->initialize() == false )
			{
				SW_LOG_ERROR( "Failed to initialize RHI Device!" );
				return false;
			}
		}
		catch ( const std::exception& e )
		{
			SW_LOG_ERROR( "Exception during RHI initialization: %s", e.what() );
			return false;
		}
		catch ( ... )
		{
			SW_LOG_ERROR( "Unknown exception during RHI initialization" );
			return false;
		}

#if defined( SW_DEBUG )
		_liveShaderManager = std::make_unique<LiveShaderManager>();
		if ( _liveShaderManager->initialize( "Shaders" ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize LiveShaderManager!" );
			return false;
		}
#endif

		SW_LOG_INFO( "RHI initialized successfully." );
		return true;
	}

	void RHI::shutdown()
	{
#if defined( SW_DEBUG )
		if ( _liveShaderManager )
			_liveShaderManager->shutdown();
#endif
		_liveShaderManager.reset();

		if ( _device )
		{
			_device->shutdown();
			_device.reset();
		}

		// After devices are gone, drop MODULE DLLs (DX11/DX12) so FreeLibrary is safe.
		RHIBackendRegistry::get().unloadModules();
	}

	bool RHI::recreateDevice( RHIBackend backend )
	{
		if ( RHIAvailability::isAvailable( backend ) == false )
		{
			SW_LOG_ERROR( "[RHI] recreateDevice: backend unavailable" );
			return false;
		}

		IWindow* window = IWindow::getActiveWindow();
		if ( _device )
		{
			_device->waitIdle();
			_device->shutdown();
			_device.reset();
		}

		gv_RHIBackend = backend;
		_device		  = createDevice( backend );
		if ( _device == nullptr )
			return false;

		if ( window )
			_device->setInitWindow( window );

		if ( _device->initialize() == false )
		{
			_device.reset();
			return false;
		}

		SW_LOG_INFO( "[RHI] Soft-recreated device: %s", getBackendTypeName( backend ) );
		return true;
	}

	std::unique_ptr<IRHIDevice> RHI::createDevice( RHIBackend backend )
	{
		return RHIBackendRegistry::get().create( backend );
	}

	const utf8* RHI::getBackendTypeName( RHIBackend backend )
	{
		const EnumInfo* info = core::getTypeRegistry().findEnum( hashed_string( "RHIBackend" ) );

		if ( info != nullptr )
		{
			hashed_string name = info->toString( static_cast<int64>( backend ) );
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
