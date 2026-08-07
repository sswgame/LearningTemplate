#include "RHI.h"
#include "Core/Graphics/Shader/LiveShaderManager.h"
#include "DX11/D3D11RHIDevice.h"
#include "DX12/D3D12RHIDevice.h"
#include "Vulkan/VulkanRHIDevice.h"
#include "GL/OpenGLRHIDevice.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Window/IWindow.h"

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
			RHIBackend initialBackend = RHIBackend::DirectX12;

			CommandLineManager& commandLineManager = getCommandLineManager();
			bool				bFlag			   = false;
			if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
				initialBackend = RHIBackend::DirectX11;
			else if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
				initialBackend = RHIBackend::DirectX12;
			else if ( commandLineManager.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
				initialBackend = RHIBackend::Vulkan;
			else if ( commandLineManager.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
				initialBackend = RHIBackend::OpenGL;

			g_RHIBackend = initialBackend;

			SW_LOG_INFO( "Initializing RHI with backend: %s", getBackendTypeName( initialBackend ) );

			SW_LOG_INFO( "Creating RHI device..." );
			_device = createDevice( initialBackend );
			if ( _device == nullptr )
			{
				SW_LOG_ERROR( "Failed to create RHI Device!" );
				return false;
			}

			SW_LOG_INFO( "RHI Device created successfully." );

			if ( IWindow* window = IWindow::getActiveWindow() )
			{
				SW_LOG_INFO( "Setting init window..." );
				_device->setInitWindow( window );
			}

			SW_LOG_INFO( "Initializing RHI device..." );
			if ( _device->initialize() == false )
			{
				SW_LOG_ERROR( "Failed to initialize RHI Device!" );
				return false;
			}

			SW_LOG_INFO( "RHI Device initialized successfully." );
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

	/**
	 * @brief 요청된 백엔드 타입에 맞는 RHI 디바이스 객체 동적 생성
	 */
	std::unique_ptr<IRHIDevice> RHI::createDevice( RHIBackend backend )
	{
		switch ( backend )
		{
			case RHIBackend::DirectX11:
#if defined( SW_PLATFORM_WINDOWS )
				SW_LOG_INFO( "Creating Direct3D 11 RHI Backend..." );
				return std::make_unique<D3D11RHIDevice>();
#else
				SW_LOG_ERROR( "Direct3D 11 RHI Backend is not supported on non-Windows platforms!" );
				return nullptr;
#endif

			case RHIBackend::DirectX12:
#if defined( SW_PLATFORM_WINDOWS )
				SW_LOG_INFO( "Creating Direct3D 12 RHI Backend..." );
				return std::make_unique<D3D12RHIDevice>();
#else
				SW_LOG_ERROR( "Direct3D 12 RHI Backend is not supported on non-Windows platforms!" );
				return nullptr;
#endif

			case RHIBackend::Vulkan:
				SW_LOG_INFO( "Creating Vulkan RHI Backend..." );
				return std::make_unique<VulkanRHIDevice>();

			case RHIBackend::OpenGL:
				SW_LOG_INFO( "Creating OpenGL RHI Backend..." );
				return std::make_unique<OpenGLRHIDevice>();
		}
		SW_LOG_ERROR( "Unknown RHI Backend Type requested!" );
		return nullptr;
	}

	/**
	 * @brief 백엔드 열거형의 이쁜 이름 반환 (Reflection 활용)
	 */
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

	/**
	 * @brief OS 플랫폼별 기본 최적 백엔드 선택
	 */
	RHIBackend RHI::getDefaultPlatformBackend()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return RHIBackend::DirectX12;
#elif defined( SW_PLATFORM_MACOS )
		return RHIBackend::Metal;
#else
		return RHIBackend::OpenGL;
#endif
	}

	/**
	 * @brief 각 RHI 백엔드 전용 셰이더 컴파일 목적 타깃 포맷 반환
	 */
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
			default:
				return ShaderTargetFormat::DXBC_D3D11;
		}
	}
}
