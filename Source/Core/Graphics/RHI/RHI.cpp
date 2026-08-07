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
	SW_GLOBAL_VARIABLE_ENUM( g_RHIBackend, RHIBackend, RHIBackend::DirectX12, "Current RHI Backend" );

	bool RHI::initialize()
	{
		SW_LOG_INFO( "RHI::initialize called" );

		try
		{
			// Priority: explicit CLI > current GVM value > OS default > first available
			RHIBackend initialBackend = g_RHIBackend;
			bool	   bCliOverride	  = false;

			CommandLineManager& commandLineManager = getCommandLineManager();
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

			g_RHIBackend = initialBackend;

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

		g_RHIBackend = backend;
		_device		 = createDevice( backend );
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
		const EnumInfo* info = getTypeRegistry().findEnum( hashed_string( "RHIBackend" ) );

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
}
