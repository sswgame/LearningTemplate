#include "pch.h"

#include "Engine/Graphics/RenderPass/FrameRenderer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"
#include "Engine/Graphics/RenderPass/FrameRendererInternal.h"
#include "Engine/Graphics/RenderPass/RenderFramePacket.h"
#include "Engine/Graphics/RenderPass/RenderPassManager.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
	FrameRenderer::FrameRenderer()
		: _pDevice{ nullptr }
		, _pCmdOwnerDevice{ nullptr }
		, _frameCmd{ nullptr }
		, _pCmd{ nullptr }
		, _pScene{ nullptr }
		, _pTaskManager{ nullptr }
		, _gpuScene{}
		, _pipelineResource{}
		, _graph{}
		, _pipelinePath{}
		, _arrClearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
		, _mapTransients{}
		, _mapTransientSrvs{}
		, _listClearedThisFrame{}
		, _pBoundMaterial{ nullptr }
		, _passConstants{}
		, _passCb{ 0 }
		, _passCbIndex{ kInvalidDescriptorIndex }
		, _gpuCullCb{ 0 }
		, _gpuCullCbIndex{ kInvalidDescriptorIndex }
		, _mapEnginePsos{}
		, _mapMaterialPassPsos{}
		, _transientWidth{ 0 }
		, _transientHeight{ 0 }
		, _outputRenderTarget{ 0 }
		, _taaHistory{ 0 }
		, _status{ FrameRendererStatus::Uninitialized }
		, _statusMessage{}
		, _bCallbacksBound{ 0 }
		, _bPassResourcesReady{ 0 }
		, _bSceneTransformsFlushed{ 0 }
		, _bHasExecutedDepthPrepass{ 0 }
		, _reservedFlags{ 0 }
		, _bUseGpuDriven{ false }
		, _graphContext{}
	{
	}

	FrameRenderer::~FrameRenderer()
	{
		shutdown();
	}

	bool FrameRenderer::initialize( IRHIDevice* pDevice, string_view pipelineXmlPath )
	{
		return initialize( pDevice, nullptr, pipelineXmlPath );
	}

	bool FrameRenderer::initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
									string_view pipelineXmlPath )
	{
		_pDevice = pDevice;
		if ( pDevice == nullptr )
		{
			_status		   = FrameRendererStatus::Failed;
			_statusMessage = "null IRHIDevice";
			SW_LOG_ERROR( "[FrameRenderer] initialize: %#", _statusMessage );
			return false;
		}

		if ( pTaskManager != nullptr )
			bindServices( pTaskManager );
		else if ( engine::areEngineServicesBound() )
			bindServices( &engine::getTaskManager() );

		const EngineData&  engineData = engine::getEngineData();
		RenderPassManager& rpm		  = pDevice->getRenderPassManager();
		if ( rpm.findRenderPass( hashed_string( kDefaultMainPassName ) ) == nullptr )
			rpm.loadRenderPass( engineData._defaultRenderPass );

		const string_view resolvedPipeline =
			pipelineXmlPath.empty() ? string_view( engineData._defaultForwardPipeline ) : pipelineXmlPath;

		if ( loadPipeline( resolvedPipeline ) == false )
		{
			_status = FrameRendererStatus::Failed;
			if ( _statusMessage.empty() )
				_statusMessage = string( "pipeline load failed: " ) + string( resolvedPipeline );
			SW_LOG_ERROR( "[FrameRenderer] Not ready — %#", _statusMessage );
			return false;
		}

		_status = FrameRendererStatus::Ready;
		_statusMessage.clear();
		SW_LOG_INFO( "[FrameRenderer] Ready with pipeline '%#'", _pipelinePath );
		return true;
	}

	void FrameRenderer::bindServices( TaskManager* pTaskManager )
	{
		_pTaskManager = pTaskManager;
	}

	void FrameRenderer::shutdown()
	{
		if ( _status == FrameRendererStatus::Uninitialized && _pDevice == nullptr && _pipelinePath.empty() )
			return;

		releaseTransientResources();
		releasePassResources();
		_graph.clear();
		_frameCmd.reset();
		_pCmdOwnerDevice = nullptr;
		_pCmd			 = nullptr;
		_pDevice		 = nullptr;
		_pBoundMaterial	 = nullptr;
		_pTaskManager	 = nullptr;
		_status			 = FrameRendererStatus::Uninitialized;
		_statusMessage.clear();
		_bCallbacksBound = 0;
		_pipelinePath.clear();
		SW_LOG_INFO( "[FrameRenderer] Shut down." );
	}

	bool FrameRenderer::loadPipeline( string_view pipelineXmlPath )
	{
		_pipelinePath	 = pipelineXmlPath;
		_bCallbacksBound = 0;
		_graph.clear();
		releaseTransientResources();

		if ( _pipelineResource.loadFromXmlFile( pipelineXmlPath ) == false )
		{
			_statusMessage = string( "failed to load pipeline XML: " ) + string( pipelineXmlPath );
			return false;
		}

		if ( _pDevice != nullptr )
		{
			RenderPassManager& rpm = _pDevice->getRenderPassManager();
			rpm.loadPipeline( pipelineXmlPath );
			for ( const string& passRef : _pipelineResource.getDesc()._listRenderPassRefs )
			{
				if ( passRef.empty() == false )
					rpm.loadRenderPass( passRef );
			}
		}

		const vector<RenderGraphPassDesc>& passes = _pipelineResource.getGraphPasses();
		if ( passes.empty() )
		{
			_statusMessage = string( "no graph passes in pipeline: " ) + string( pipelineXmlPath );
			SW_LOG_ERROR( "[FrameRenderer] %#", _statusMessage );
			return false;
		}

		float32 sceneColorClear[4];
		if ( tryGetAttachmentClearColor( Attachment::kSceneColor, sceneColorClear ) )
			Memory::copy( _arrClearColor, sceneColorClear, sizeof( _arrClearColor ) );

		// Rebuild PSOs from pipeline pass recipes (shader / entry / blend / permutations).
		releasePassResources();
		ensurePassResources();
		ensureTransientResources();
		bindPassCallbacks();

		SW_LOG_INFO( "[FrameRenderer] Built graph '%#' (%# passes, callbacks bound once)",
					 _pipelineResource.getDesc()._name, passes.size() );
		return true;
	}

	// ---------------------------------------------------------------------------
	// 공통 헬퍼: commandList 준비
	// ---------------------------------------------------------------------------

	bool FrameRenderer::prepareCommandList( IRHIDevice* pDevice, [[maybe_unused]] const utf8* pCallerName )
	{
		if ( _frameCmd &&
			 ( _pCmdOwnerDevice != pDevice ||
			   ( _frameCmd->asDeferred() != nullptr &&
				 _frameCmd->asDeferred()->getMode() != pDevice->getDefaultCommandListMode() ) ) )
			_frameCmd.reset();

		if ( _frameCmd == nullptr )
			_frameCmd = pDevice->createCommandList();

		_pCmdOwnerDevice = pDevice;
		_pCmd			 = _frameCmd.get();

		if ( _pCmd == nullptr )
		{
			SW_LOG_ERROR( "[FrameRenderer] %#: createCommandList returned null", pCallerName );
			return false;
		}

		RHIDeferredCommandList* pDeferred = _pCmd->asDeferred();
		if ( pDeferred != nullptr )
			pDeferred->setContext( pDevice->getCommandContextForMode( pDevice->getDefaultCommandListMode() ) );

		return true;
	}

	// ---------------------------------------------------------------------------
	// 공통 헬퍼: graph 실행 및 commandList 제출
	// ---------------------------------------------------------------------------

	bool FrameRenderer::submitGraph( IRHIDevice* pDevice )
	{
		_pCmd->beginCommandList();

		if ( _bUseGpuDriven != 0 && _gpuScene.isUploaded() )
		{
			const RHIPipelineStateHandle cullPso = getEnginePso( "GpuCull" );
			if ( cullPso != 0 && _gpuCullCb != 0 &&
				 _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex &&
				 _gpuScene.getIndirectArgsUav() != kInvalidDescriptorIndex )
			{
				struct GpuCullParams
				{
					float32 _planes[6][4]{};
					uint32	_instanceCount{ 0 };
					uint32	_batchCount{ 0 };
					uint32	_pad[2]{};
				} cullParams{};
				cullParams._instanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
				cullParams._batchCount	  = _gpuScene.getIndirectCommandCount();
				_pDevice->getResource()->updateConstantBuffer( _gpuCullCb, &cullParams, sizeof( cullParams ) );

				_pCmd->setComputePipelineState( cullPso );
				_pCmd->bindComputeUAV( _gpuScene.getInstanceSrv(), 0 );
				_pCmd->bindComputeUAV( _gpuScene.getIndirectArgsUav(), 1 );
				_pCmd->setComputeRootConstants( 0, sizeof( cullParams ) / 4, &cullParams );
				const uint32 groups = ( cullParams._batchCount + 63u ) / 64u;
				if ( groups > 0 )
					_pCmd->dispatchCompute( groups, 1, 1 );
				_pCmd->transitionBuffer( _gpuScene.getIndirectArgsBuffer(), RHIBufferState::IndirectArgument );
			}
		}

		const bool bOk = _graph.execute( _graphContext );
		_pCmd->endCommandList();
		pDevice->executeCommandList( _pCmd );
		// _frameCmd 는 다음 프레임 prepareCommandList 에서 재사용. _pCmd 만 비움.
		_pCmd = nullptr;
		return bOk;
	}

	// ---------------------------------------------------------------------------

	bool FrameRenderer::execute( IRHIDevice* pDevice, Material* pMaterial, Scene* pScene )
	{
		if ( isReady() == false || pDevice == nullptr )
			return false;

		_pDevice			= pDevice;
		_pScene				= pScene;
		_pBoundMaterial		= pMaterial;
		_outputRenderTarget = 0;
		ensurePassResources();
		ensureTransientResources();
		setIdentityWorld();
		updatePassConstants();
		_listClearedThisFrame.clear();
		_bSceneTransformsFlushed  = 0;
		_bHasExecutedDepthPrepass = 0;

		float32 camPos[3];
		Memory::copy( camPos, kDefaultCameraPos, sizeof( camPos ) );
		if ( pScene != nullptr )
		{
			pScene->ensureDefaultCameras();
			CameraComponent* pCam = pScene->getActiveRenderCamera( false );
			if ( pCam != nullptr )
				pCam->getCameraPosition( camPos );
		}
		_gpuScene.buildFromScene( pScene, camPos, _pTaskManager );
		if ( _bUseGpuDriven != 0 )
			_gpuScene.upload( pDevice );

		if ( _bCallbacksBound == 0 )
			bindPassCallbacks();

		if ( prepareCommandList( pDevice, "execute" ) == false )
		{
			_pScene = nullptr;
			return false;
		}

		const bool bOk = submitGraph( pDevice );
		_gpuScene.advanceMaterialRetireFrame();
		_pScene = nullptr;
		return bOk;
	}

	bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
	{
		if ( isReady() == false || pDevice == nullptr || packet._bValid == 0 )
			return false;

		_pDevice			= pDevice;
		_pScene				= nullptr;
		_pBoundMaterial		= packet._pSceneMaterial;
		_outputRenderTarget = packet._gameRenderTarget;
		_gpuScene			= std::move( packet._gpuScene );
		ensurePassResources();
		ensureTransientResources();
		setIdentityWorld();
		buildLightViewProj( _passConstants._lightViewProj );
		if ( packet._bHasViewProj != 0 )
			Memory::copy( _passConstants._viewProj, packet._viewProj, sizeof( _passConstants._viewProj ) );
		else
			buildViewProj( _passConstants._viewProj );
		_passConstants._outlineParams[1] = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
		_passConstants._outlineParams[2] = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;
		_passConstants._flags			 = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u;
		if ( _pDevice != nullptr && _passCb != 0 )
			_pDevice->getResource()->updateConstantBuffer( _passCb, &_passConstants, sizeof( PassConstants ) );
		// Skip updatePassConstants() — view already applied from packet.
		_listClearedThisFrame.clear();
		_bSceneTransformsFlushed  = 0;
		_bHasExecutedDepthPrepass = 0;

		if ( _bUseGpuDriven != 0 )
			_gpuScene.upload( pDevice );

		if ( _bCallbacksBound == 0 )
			bindPassCallbacks();

		if ( prepareCommandList( pDevice, "executePacket" ) == false )
		{
			packet._gpuScene = std::move( _gpuScene );
			return false;
		}

		const bool bOk = submitGraph( pDevice );
		_gpuScene.advanceMaterialRetireFrame();
		packet._gpuScene = std::move( _gpuScene );
		return bOk;
	}
} // namespace sw
