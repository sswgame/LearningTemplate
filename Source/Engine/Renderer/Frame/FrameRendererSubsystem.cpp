#include "pch.h"

#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    // 실행 중에 바꾸면 파이프라인을 다시 고르는 변수입니다(`FrameRenderer::selectPipeline` 의 입력, 정의는 FrameRenderer.cpp).
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_renderPipeline );
    SW_EXTERN_GLOBAL_VARIABLE( bool, gv_deferredShading );

    FrameRendererSubsystem::FrameRendererSubsystem( unique_ptr<FrameRenderer> frameRenderer, const RHISubsystem& rhiSubsystem )
        : _frameRenderer{ std::move( frameRenderer ) }
        , _offscreenRenderers{ nullptr }
        , _packetBuilder{ nullptr }
        , _rhiSubsystem{ rhiSubsystem }
    {
        SW_MEMORY_SCOPE( RenderCPU );
        _packetBuilder      = make_unique<RenderPacketBuilder>();
        _offscreenRenderers = make_unique<OffscreenSceneRenderers>();
    }

    FrameRendererSubsystem::~FrameRendererSubsystem()
    {
        _packetBuilder->destroy();
        _packetBuilder.reset();
        _offscreenRenderers.reset();
        _frameRenderer.reset();
    }

    SubsystemInitResult FrameRendererSubsystem::initializeSubsystem()
    {
        IRHIDevice& device = _rhiSubsystem.findRHI()->getDevice();
        _packetBuilder->initialize( device, &engine::getSubsystem<TaskManager>() );
        // 주 렌더러만 하는 일의 상대 — 화면 밖 씬 렌더러는 걸지 않는다(`FrameRendererHost`). initialize 앞이다(파이프라인 로드가 렌더 타깃을 공개한다).
        FrameRendererHost host{};
        host._pProfiler        = &engine::getSubsystem<FrameProfiler>();
        host._pExtensions      = engine::findSubsystem<RenderExtensionRegistry>();
        host._pRenderTargets   = engine::findSubsystem<RenderTargetRegistry>();
        host._pPinnedMaterials = &engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        _frameRenderer->bindHost( host );
        if ( _frameRenderer->initialize( &device, &engine::getSubsystem<TaskManager>() ) == false )
        {
            SW_LOG_ERROR_ID( "FrameRenderer.FailedInitializeFrameRenderer", "Failed to initialize FrameRenderer!" );
            return SubsystemInitResult::Failed;
        }
        GlobalVariableManager&    globalVariables       = engine::getSubsystem<GlobalVariableManager>();
        GlobalVariableInfo* const arrPipelineVariable[] = { globalVariables.findVariableOf( gv_renderPipeline ), globalVariables.findVariableOf( gv_deferredShading ) };
        for ( GlobalVariableInfo* const pVariable : arrPipelineVariable )
        {
            if ( pVariable == nullptr )
            {
                SW_LOG_ERROR_ID( "FrameRenderer.PipelineVariableGvRenderPipeline", "A pipeline variable (gv_renderPipeline · gv_deferredShading) is not registered" );
                return SubsystemInitResult::Failed;
            }
            pVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &FrameRendererSubsystem::onPipelineVariableChanged, this );
        }
        return SubsystemInitResult::Succeeded;
    }

    void FrameRendererSubsystem::onPipelineVariableChanged( GlobalVariableInfo* pVariable )
    {
        // 렌더 쪽 객체(렌더러 · PSO 캐시)는 렌더 스레드가 다음 프레임 기록 전에 고친다 — 렌더 스레드와 GPU 를 세우지 않는다(셰이더 리로드 `LiveShaderSubsystem` 과 같은 길).
        // 옛 패스 자원은 디바이스가 그것을 쓴 프레임의 GPU 완료 뒤로 미뤄 놓는다(백엔드 해제 큐).
        const string           change         = pVariable->_name + "=" + pVariable->getValueAsString();
        RenderThreadSubsystem* pRenderThreads = engine::findSubsystem<RenderThreadSubsystem>();
        RenderThread* const    pRenderThread  = pRenderThreads != nullptr ? pRenderThreads->findRenderThread() : nullptr;
        if ( pRenderThread == nullptr )
        {
            applyPipelineChange( change ); // 패킷을 실행하는 쪽이 없다 — 렌더러를 만지는 스레드가 이 스레드 하나다
            return;
        }
        pRenderThread->enqueueCommand( SW_DELEGATE_LAMBDA( RenderCommandDelegate, [this, change]( IRHIDevice& )
        { applyPipelineChange( change ); } ) );
    }

    void FrameRendererSubsystem::applyPipelineChange( const string& change )
    {
        // 화면 밖 렌더러는 옛 파이프라인으로 지었고 PSO 캐시를 함께 쓴다 — 내리고 다음 그리기가 새 파이프라인으로 다시 짓는다.
        _offscreenRenderers->shutdown();
        if ( _frameRenderer->selectPipeline() == false )
            SW_LOG_ERROR_ID( "FrameRenderer.ChangedButPipelineCould", "%# but the pipeline could not be selected - %#", change.c_str(), _frameRenderer->getStatusMessage().c_str() );
        else
            SW_LOG_INFO_ID( "FrameRenderer.ChangedDrawing", "%# - drawing with %#", change.c_str(), _frameRenderer->getPipelinePath().c_str() );
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
        _offscreenRenderers->shutdown(); // 주 렌더러의 PSO 캐시를 빌려 쓴다 — 먼저 내린다
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

namespace sw
{
    SW_ENGINE_SUBSYSTEM( FrameRendererSubsystem, Startup, ( RHISubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        unique_ptr<FrameRenderer> frameRenderer;
        {
            SW_MEMORY_SCOPE( RenderCPU );
            frameRenderer = make_unique<FrameRenderer>();
        }
        FrameRendererSubsystem& frameRendererSubsystem = scope.emplaceSubsystem<FrameRendererSubsystem>( std::move( frameRenderer ), scope.get<RHISubsystem>() );
        build._pStartupParts->_pFrameRendererSubsystem = &frameRendererSubsystem;
        (void)scope.bind( &frameRendererSubsystem.getFrameRenderer() );      // 새 범위 — 겹치지 않는다
        (void)scope.bind( &frameRendererSubsystem.getOffscreenRenderers() ); // 화면 밖 씬 렌더러(진단 감시 값이 수를 읽는다)
    } );

    // 위 공장이 건 렌더러 둘의 서비스 칸. 비어 있을 수 있다 — 렌더러를 두지 않는 호스트(헤드리스 · 테스트 하네스)는 만들지 않는다.
    SW_ENGINE_SERVICE( FrameRenderer, HostOnly );
    SW_ENGINE_SERVICE( OffscreenSceneRenderers, HostOnly );
} // namespace sw
