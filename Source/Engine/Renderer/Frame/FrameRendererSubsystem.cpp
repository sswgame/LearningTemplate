#include "pch.h"

#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    // 실행 중에 바꾸면 파이프라인을 다시 고르는 변수입니다(`FrameRenderer::selectPipeline` 의 입력, 정의는 FrameRenderer.cpp).
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_renderPipeline );
    SW_EXTERN_GLOBAL_VARIABLE( bool, gv_deferredShading );

    FrameRendererSubsystem::FrameRendererSubsystem( unique_ptr<FrameRenderer> frameRenderer, const RHISubsystem& rhiSubsystem )
        : _frameRenderer{ std::move( frameRenderer ) }
        , _packetBuilder{ nullptr }
        , _rhiSubsystem{ rhiSubsystem }
    {
        SW_MEMORY_SCOPE( RenderCPU );
        _packetBuilder = make_unique<RenderPacketBuilder>();
    }

    FrameRendererSubsystem::~FrameRendererSubsystem()
    {
        _packetBuilder->destroy();
        _packetBuilder.reset();
        _frameRenderer.reset();
    }

    SubsystemInitResult FrameRendererSubsystem::initializeSubsystem()
    {
        IRHIDevice& device = _rhiSubsystem.findRHI()->getDevice();
        _packetBuilder->initialize( device, &engine::getSubsystem<TaskManager>() );
        if ( _frameRenderer->initialize( &device, &engine::getSubsystem<TaskManager>() ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
            return SubsystemInitResult::Failed;
        }
        GlobalVariableManager&    globalVariables       = engine::getSubsystem<GlobalVariableManager>();
        GlobalVariableInfo* const arrPipelineVariable[] = { globalVariables.findVariableOf( gv_renderPipeline ), globalVariables.findVariableOf( gv_deferredShading ) };
        for ( GlobalVariableInfo* const pVariable : arrPipelineVariable )
        {
            if ( pVariable == nullptr )
            {
                SW_LOG_ERROR( "A pipeline variable (gv_renderPipeline · gv_deferredShading) is not registered" );
                return SubsystemInitResult::Failed;
            }
            pVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &FrameRendererSubsystem::onPipelineVariableChanged, this );
        }
        return SubsystemInitResult::Succeeded;
    }

    void FrameRendererSubsystem::onPipelineVariableChanged( GlobalVariableInfo* pVariable )
    {
        // 렌더 스레드가 지금 파이프라인으로 그리는 중이면 기다린다 — 셰이더 리로드(`LiveShaderSubsystem`)와 같은 자리 바꿈이다(프레임 경로가 아니다).
        if ( RenderThreadSubsystem* const pRenderThreads = engine::findSubsystem<RenderThreadSubsystem>() )
        {
            if ( RenderThread* const pRenderThread = pRenderThreads->findRenderThread() )
                pRenderThread->waitIdle( RHIWaitIdleReason::PipelineSwitch );
        }
        if ( _frameRenderer->selectPipeline() == false )
            SW_LOG_ERROR( "%# changed to '%#' but the pipeline could not be selected - %#", pVariable->_name.c_str(), pVariable->getValueAsString().c_str(),
                          _frameRenderer->getStatusMessage().c_str() );
        else
            SW_LOG_INFO( "%# changed - drawing with %#", pVariable->_name.c_str(), _frameRenderer->getPipelinePath().c_str() );
    }

    void FrameRendererSubsystem::shutdownSubsystem()
    {
        if ( GlobalVariableManager* const pGlobalVariables = engine::findSubsystem<GlobalVariableManager>() )
        {
            for ( GlobalVariableInfo* const pVariable : { pGlobalVariables->findVariableOf( gv_renderPipeline ), pGlobalVariables->findVariableOf( gv_deferredShading ) } )
            {
                if ( pVariable != nullptr )
                    pVariable->_onValueChanged = {};
            }
        }
        _packetBuilder->shutdown();
        _frameRenderer->shutdown();
    }

    span<const utf8* const> FrameRendererSubsystem::getSubsystemDependencies() const
    {
        // 렌더 파이프라인은 팩 설정(`<Engine _renderPipeline>`)이 고를 수 있다. 2D 정렬 레이어 표는 엔진 파일이고 기동에서 Render2DSettings 가 읽는다.
        static constexpr const utf8* kArrDependency[] = { PackEngineSettingsSubsystem::kSubsystemName, Render2DSettingsSubsystem::kSubsystemName, RHISubsystem::kSubsystemName,
                                                          TaskManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask FrameRendererSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw
