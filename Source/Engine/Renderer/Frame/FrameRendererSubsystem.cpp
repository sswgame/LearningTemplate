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
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    namespace
    {
        // 렌더러를 세우고 고르는 변수 — 이 파일(게임 스레드)만 읽고 렌더러에는 사본(`RenderPipelineRequest` · 보기 모드)을 넘긴다. 익명 이름공간이라 렌더 쪽 코드는 이름을 볼 수 없다.

        /**
         * @brief `-gv_deferredShading=1` 이면 기본 파이프라인을 디퍼드로 고릅니다(기본은 포워드).
         * @details `initialize` 에 파이프라인 경로를 주지 않을 때 디퍼드를 고르는 방법이다. 디퍼드는 레벨이 나뉘어(레벨 0은 Shadow 와 GBuffer) 병렬 기록이 실제로 도는 유일한 경로이기도 하다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_deferredShading, false, "기본 파이프라인을 디퍼드로 (기본 포워드)" );
        /**
         * @brief `-gv_renderPipeline=<파이프라인 XML>` 이면 그 파이프라인으로 그립니다(디퍼드 선택 · 팩 설정 · 기본보다 우선합니다).
         * @details 게임은 데이터로 고른다 — 팩 설정의 `PackEngineSettings::_renderPipeline`(2D 게임은 그림자 맵과 톤맵이 없는
         *          `engine/pipeline/forward2dpipeline.xml`. 톤맵은 흰색을 0.5 로 누르기 때문이다). 이 스위치는 측정 · 검증용으로 그것을 덮는다.
         */
        SW_GLOBAL_VARIABLE( sw::GlobalString, gv_renderPipeline, "", "이 파이프라인 XML 로 그린다 (비우면 gv_deferredShading · 팩 설정 · 기본 포워드)" );
        /**
         * @brief `-gv_viewMode=<이름>` 은 주 출력(게임 창)의 보기 모드입니다. 에디터 씬 뷰 툴바와 같은 값을 가리킨다(`RenderViewMode`).
         * @details 여기 있는 이유는 **검증**이다 — 뷰 모드가 정말로 픽셀을 바꾸는지 `-gv_screenshot` 으로 확인하려면 에디터를 띄우지 않고 모드를 고를 수 있어야 한다.
         *          명령줄은 열거자 이름만 받는다(숫자는 오류). 렌더러 기동 때 한 번 읽는다.
         */
        SW_GLOBAL_VARIABLE( RenderViewMode, gv_viewMode, RenderViewMode::Lit, "주 출력 보기 모드 (Lit / Unlit / Wireframe / Normals / Depth / Overdraw / LightingOnly / Roughness / Metallic / Mipmaps / LightComplexity / ShadedWireframe / LODLevel / LODFadeCode / MarkerSlots / ShadingPath)" );

        struct FrameRendererSubsystemInternal
        {
            /** @brief 파이프라인 변수의 지금 값입니다(게임 스레드). 렌더 스레드의 고르기는 이 사본을 받는다. */
            static RenderPipelineRequest makePipelineRequest()
            {
                RenderPipelineRequest request;
                request._commandLinePath = gv_renderPipeline.get();
                request._bDeferred       = gv_deferredShading ? SW_TRUE : SW_FALSE;
                return request;
            }
        };
    } // namespace

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
        host._pRenderDebug     = engine::findSubsystem<RenderDebugRegistry>();
        host._pPinnedMaterials = &engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        _frameRenderer->bindHost( host );
        // 커맨드라인이 주 출력의 보기 모드를 정했으면 여기서 받는다(에디터 씬 뷰는 툴바가 따로 정한다).
        // 모드가 아닌 값(`Count` 표식, 에디터 · 콘솔이 숫자로 넣은 범위 밖 값)은 Lit 으로 바꾸지 않고 기동을 멈춘다 — 검증 실행이 다른 모드를 찍고 통과하면 안 된다.
        const RenderViewMode viewMode = gv_viewMode;
        if ( isRenderViewModeIndex( static_cast<int32>( viewMode ) ) == false )
        {
            SW_LOG_ERROR_ID( "FrameRenderer.Initialize", "initialize: -gv_viewMode=%# is not a view mode (0 .. %#)", static_cast<int64>( viewMode ), static_cast<int64>( RenderViewMode::Count ) - 1 );
            return SubsystemInitResult::Failed;
        }
        if ( _frameRenderer->initialize( &device, &engine::getSubsystem<TaskManager>(), RenderFrameSettings::capture(), FrameRendererSubsystemInternal::makePipelineRequest() ) == false )
        {
            SW_LOG_ERROR_ID( "FrameRenderer.FailedInitializeFrameRenderer", "Failed to initialize FrameRenderer!" );
            return SubsystemInitResult::Failed;
        }
        if ( viewMode != RenderViewMode::Lit )
            _frameRenderer->setViewMode( viewMode );
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
        const string                change         = pVariable->_name + "=" + pVariable->getValueAsString();
        const RenderPipelineRequest request        = FrameRendererSubsystemInternal::makePipelineRequest(); // 렌더 스레드는 변수를 읽지 않는다 — 여기(게임 스레드)서 뜬 사본을 넘긴다
        RenderThreadSubsystem*      pRenderThreads = engine::findSubsystem<RenderThreadSubsystem>();
        RenderThread* const         pRenderThread  = pRenderThreads != nullptr ? pRenderThreads->findRenderThread() : nullptr;
        if ( pRenderThread == nullptr )
        {
            applyPipelineChange( change, request ); // 패킷을 실행하는 쪽이 없다 — 렌더러를 만지는 스레드가 이 스레드 하나다
            return;
        }
        pRenderThread->enqueueCommand( SW_DELEGATE_LAMBDA( RenderCommandDelegate, [this, change, request]( IRHIDevice& )
        { applyPipelineChange( change, request ); } ) );
    }

    void FrameRendererSubsystem::applyPipelineChange( const string& change, const RenderPipelineRequest& request )
    {
        // 화면 밖 렌더러는 옛 파이프라인으로 지었고 PSO 캐시를 함께 쓴다 — 내리고 다음 그리기가 새 파이프라인으로 다시 짓는다.
        _offscreenRenderers->shutdown();
        if ( _frameRenderer->selectPipeline( request ) == false )
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
        (void)scope.exposeMember( frameRendererSubsystem, frameRendererSubsystem.getFrameRenderer() );      // 새 범위 — 겹치지 않는다
        (void)scope.exposeMember( frameRendererSubsystem, frameRendererSubsystem.getOffscreenRenderers() ); // 화면 밖 씬 렌더러(진단 감시 값이 수를 읽는다)
    } );

    // 위 공장이 건 렌더러 둘의 서비스 칸. 비어 있을 수 있다 — 렌더러를 두지 않는 호스트(헤드리스 · 테스트 하네스)는 만들지 않는다.
    SW_ENGINE_SERVICE( FrameRenderer, HostOnly );
    SW_ENGINE_SERVICE( OffscreenSceneRenderers, HostOnly );
} // namespace sw
