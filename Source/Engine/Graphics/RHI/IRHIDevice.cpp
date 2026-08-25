#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/RenderPassManager.h"
#include "Engine/Window/IWindow.h"

#include "Core/CommandLine/CommandLineManager.h"

namespace sw
{
	IRHIDevice::~IRHIDevice() = default;

	IRHIDevice::IRHIDevice()
		: _pInitWindow{ nullptr }
		, _renderPassManager{ nullptr }
		, _defaultCommandListMode{ RHICommandListMode::Deferred }
		, _bPreferredVSync{ false }
	{
	}

	unique_ptr<IRHICommandList> IRHIDevice::createCommandList()
	{
		return createCommandList( _defaultCommandListMode );
	}

	bool IRHIDevice::executeOffscreenPipelineSmoke( RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
													uint32 width, uint32 height )
	{
		if ( pso == 0 || width == 0 || height == 0 )
			return false;
		IRHIResource* pResource = getResource();
		if ( pResource == nullptr )
		{
			SW_LOG_WARNING( "[RHI] executeOffscreenPipelineSmoke: missing resource" );
			return false;
		}
		if ( getCapabilities()._bOffscreenRT == 0 )
		{
			SW_LOG_WARNING( "[RHI] executeOffscreenPipelineSmoke: caps._bOffscreenRT=0" );
			return false;
		}

		RHITextureDesc desc{};
		desc._width				= width;
		desc._height			= height;
		desc._format			= RHIFormat::R8G8B8A8_UNORM;
		desc._bIsRenderTarget	= 1;
		desc._bIsShaderResource = 1;
		desc._arrClearColor[0]	= 0.05f;
		desc._arrClearColor[1]	= 0.05f;
		desc._arrClearColor[2]	= 0.08f;
		desc._arrClearColor[3]	= 1.0f;

		const RHITextureHandle rt = pResource->createTexture2D( desc );
		if ( rt == 0 )
		{
			SW_LOG_WARNING( "[RHI] executeOffscreenPipelineSmoke: createTexture2D failed" );
			return false;
		}

		bool bOk{ true };

		// Present 없이 beginRenderPass → PSO → fullscreen draw (모든 백엔드).
		unique_ptr<IRHICommandList> cmd = createCommandList( RHICommandListMode::Immediate );
		if ( cmd == nullptr )
		{
			SW_LOG_WARNING( "[RHI] executeOffscreenPipelineSmoke: createCommandList failed" );
			bOk = false;
		}
		else
		{
			RHIRenderPassBeginInfo beginInfo{};
			beginInfo._colorTarget = rt;
			beginInfo._bBindColor  = 1;
			beginInfo._width	   = width;
			beginInfo._height	   = height;
			beginInfo._loadOp	   = RHIRenderPassLoadOp::Clear;
			Memory::copy( beginInfo._arrClearColor, desc._arrClearColor, sizeof( beginInfo._arrClearColor ) );

			RHIViewport viewport{};
			viewport._width	 = static_cast<float32>( width );
			viewport._height = static_cast<float32>( height );

			cmd->beginCommandList();
			cmd->setViewport( viewport );
			cmd->beginRenderPass( beginInfo );
			cmd->setPipelineState( pso );
			cmd->draw( 3, 0, materialCb );
			cmd->endRenderPass();
			cmd->endCommandList();
			executeCommandList( cmd.get() );
			waitIdle();
		}

		pResource->destroyTexture( rt );
		return bOk;
	}

	bool IRHIDevice::initialize()
	{
		if ( _pInitWindow == nullptr )
			return false;

		_renderPassManager = make_unique<RenderPassManager>();
		if ( _renderPassManager->initialize() == false )
			return false;

		constexpr uint32 kBackBufferCount = 3;

		RHISwapChainDesc swapChainDesc{};
		swapChainDesc._pWindowHandle  = _pInitWindow->getNativeHandle();
		swapChainDesc._pWindowDisplay = _pInitWindow->getNativeDisplay();
		swapChainDesc._width		  = _pInitWindow->getWidth();
		swapChainDesc._height		  = _pInitWindow->getHeight();
		swapChainDesc._bufferCount	  = kBackBufferCount;
		swapChainDesc._bVSync		  = _bPreferredVSync;
		if ( engine::areEngineServicesBound() )
		{
			bool bCliVSync{ false };
			if ( engine::getCommandLineManager().getArgument( CommandLineArgument::VSYNC, bCliVSync ) )
				swapChainDesc._bVSync = bCliVSync;
		}

		return initializeInternal( swapChainDesc );
	}

	void IRHIDevice::shutdown()
	{
		if ( _renderPassManager )
		{
			_renderPassManager->shutdown();
			_renderPassManager.reset();
		}
		shutdownInternal();
	}

	RenderPassManager& IRHIDevice::getRenderPassManager() const
	{
		return *_renderPassManager;
	}
} // namespace sw
