#include "pch.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"

#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAssetCache.h"
#include "Engine/Renderer/VFX/VFXGPUSimulation.h"
#include "Engine/Scene/Scene.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_deferredShading=1` 이면 기본 파이프라인을 디퍼드로 고릅니다(기본은 포워드).
     * @details `initialize` 에 파이프라인 경로를 주지 않을 때 디퍼드를 고르는 방법이다.
     *          디퍼드는 레벨이 나뉘어(레벨 0은 Shadow 와 GBuffer) 병렬 기록이 실제로 도는 유일한 경로이기도 하다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_deferredShading, false, "기본 파이프라인을 디퍼드로 (기본 포워드)" );
    /**
     * @brief `-gv_renderPipeline=<파이프라인 XML>` 이면 그 파이프라인으로 그립니다(디퍼드 선택 · 팩 설정 · 기본보다 우선합니다).
     * @details 게임은 데이터로 고른다 — 팩 설정의 `PackEngineSettings::_renderPipeline`(2D 게임은 그림자 맵과 톤맵이 없는
     *          `engine/pipeline/forward2dpipeline.xml`. 톤맵은 흰색을 0.5 로 누르기 때문이다). 이 스위치는 측정 · 검증용으로 그것을 덮는다.
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_renderPipeline, "", "이 파이프라인 XML 로 그린다 (비우면 gv_deferredShading · 팩 설정 · 기본 포워드)" );

    /**
     * @brief `-gv_vertexPool=0` 이면 씬 메시 정점을 한 풀 버퍼에 모으지 않고 메시마다 자기 정점 버퍼로 그립니다.
     * @details 풀을 켠 이미지와 끈 이미지는 같아야 한다. 다르면 간접 인자의 startVertex 나 SV_VertexID 의 API 차이를 잘못 다룬 것이다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_vertexPool, true, "씬 메시 정점 풀 (false=메시마다 정점 버퍼, 진단용)" );

    /**
     * @brief 라이트 컬링 방식입니다. `-gv_lightCulling=None` 이면 라이트 그리드 없이 픽셀마다 목록 전체를 돕니다(비교 기준), `CPU` 면 CPU 가 만든 클러스터 그리드, `GPU` 면 GPU 컴퓨트가 만든 그리드입니다.
     * @details 학습용으로 세 방식을 나란히 보게 둔다. 기본값은 GPU 다. Shipping 은 테스트 변수를 등록하지 않아(명령줄 · 콘솔이 못 바꾼다) 기본값 GPU 로 고정된다
     *          (`FrameRenderer::selectLightGridProducer`). 명령줄은 열거자 이름만 받는다(숫자는 오류).
     */
    SW_TEST_GLOBAL_VARIABLE( LightGridProducer, gv_lightCulling, LightGridProducer::GPU, "라이트 컬링 (None=그리드 없이 목록 전체, CPU=CPU 클러스터 그리드, GPU=GPU 클러스터 그리드)" );

    /**
     * @brief `-gv_viewMode=<이름>` 은 주 출력(게임 창)의 보기 모드입니다(Lit, Unlit, Wireframe, Normals, Depth, Overdraw, LightingOnly, Roughness, Metallic, Mipmaps, LightComplexity, ShadedWireframe, LODLevel, LODFadeCode, MarkerSlots).
     * @details 에디터 씬 뷰 툴바와 같은 값을 가리킨다(`RenderViewMode`). 여기 있는 이유는 **검증**이다.
     *          뷰 모드가 정말로 픽셀을 바꾸는지 `-gv_screenshot` 으로 확인하려면 에디터를 띄우지 않고
     *          모드를 고를 수 있어야 한다. 없으면 이 기능은 사람 눈으로만 확인되는 기능이 된다.
     *          명령줄은 열거자 이름만 받는다(숫자는 오류).
     */
    SW_GLOBAL_VARIABLE( RenderViewMode, gv_viewMode, RenderViewMode::Lit, "주 출력 보기 모드 (Lit / Unlit / Wireframe / Normals / Depth / Overdraw / LightingOnly / Roughness / Metallic / Mipmaps / LightComplexity / ShadedWireframe / LODLevel / LODFadeCode / MarkerSlots)" );

    FrameRenderer::FrameRenderer()
        : _pDevice{ nullptr }
        , _state{}
        , _sceneBuilder{}
        , _pipelineResource{}
        , _psoManager{ _pDevice, _pipelineResource, _state._gpuScene }
        , _autoExposure{ _pDevice, _pipelineResource, _psoManager }
        , _upscale{ _pDevice, _pipelineResource, _psoManager }
        , _fog{ _pDevice, _psoManager }
        , _extensionHost{ _pDevice, _pipelineResource, _psoManager }
        , _viewResources{ _pDevice, _pipelineResource, _autoExposure, _upscale }
        , _pipelinePath{}
        , _directViewScheduler{}
        , _listDirectViewScratch{}
        , _lastIndirectDrawCallCount{ 0 }
        , _capture{ _pDevice, _state._mainView._transientPool, _pipelineResource }
        , _passExecutor{ _pDevice, _pipelineResource, _state, _psoManager, _autoExposure, _upscale, _fog, _extensionHost, _capture }
        , _listScenePreviewRenderer{}
        , _statusMessage{}
        , _directOutputWidth{ 0 }
        , _directOutputHeight{ 0 }
        , _scenePreviewRendererCount{ 0 }
        , _status{ FrameRendererStatus::Uninitialized }
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
        , _bScenePreview{ SW_FALSE }
        , _bLightGridPSOMissingLogged{ SW_FALSE }
        , _viewMode{ static_cast<uint8>( RenderViewMode::Lit ) }
        , _sceneViewMode{ static_cast<uint8>( RenderViewMode::Lit ) }
    {
    }

    FrameRenderer::~FrameRenderer()
    {
        shutdown();
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, string_view pipelineXMLPath )
    {
        return initialize( pDevice, nullptr, pipelineXMLPath );
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                                    string_view pipelineXMLPath )
    {
        _pDevice = pDevice;
        // 인스턴스 애니메이션 시계는 여기서 한 번 돌린다(GameTimer 는 만들면 멈춘 상태다).
        _state._deform._animationTimer.resetTimer();
        _state._deform._animationTimer.startTimer();
        if ( pDevice == nullptr )
        {
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "null IRHIDevice";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        if ( pTaskManager != nullptr )
            bindServices( pTaskManager );
        else if ( engine::areEngineServicesBound() )
            bindServices( &engine::getSubsystem<TaskManager>() );
        // 패스별 GPU 시간을 보고할 프로파일러다. 서비스가 없는 실행(서비스 없이 렌더러만 쓰는 도구)은 계측하지 않는다.
        // 미리보기 씬 렌더러는 계측하지 않는다(타임스탬프 슬롯 · `GPU.<패스>` 구간이 주 렌더러의 것이다).
        const bool bProfile = engine::areEngineServicesBound() && _bScenePreview == SW_FALSE;
        _state._gpuPassTimeline.bindProfiler( bProfile ? &engine::getSubsystem<FrameProfiler>() : nullptr );

        _renderPipelineAssetCache = make_unique<RenderPipelineAssetCache>();
        if ( _renderPipelineAssetCache->initialize() == false )
        {
            _renderPipelineAssetCache.reset();
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "RenderPipelineAssetCache initialize failed";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        if ( FramePrepass::isMeshMorphDiagValid() == false )
        {
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "-gv_meshMorphDebugMode is not a mesh morph diagnostic mode";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        // 2D 정렬 레이어 표는 엔진 필수 데이터다. 서브시스템 Render2DSettings 가 기동 때 읽고(못 읽으면 기동이 멈춘다) 렌더러 서브시스템이 그 뒤에 선다 -
        // 주 렌더러와 미리보기 렌더러는 서비스 표의 같은 표를 읽는다.

        // 커맨드라인이 주 출력의 보기 모드를 정했으면 여기서 받는다(에디터 씬 뷰는 툴바가 따로 정한다).
        // 모드가 아닌 값(`Count` 표식, 에디터 · 콘솔이 숫자로 넣은 범위 밖 값)은 Lit 으로 바꾸지 않고 초기화를 멈춘다 — 검증 실행이 다른 모드를 찍고 통과하면 안 된다.
        if ( isRenderViewModeIndex( static_cast<int32>( gv_viewMode ) ) == false )
        {
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "-gv_viewMode=" + to_string( static_cast<int64>( gv_viewMode ) ) + " is not a view mode (0 .. " +
                             formatToString( "%#)", static_cast<int64>( RenderViewMode::Count ) - 1 );
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }
        if ( gv_viewMode != RenderViewMode::Lit )
            setViewMode( gv_viewMode );

        // 동기 경로(execute)는 렌더러 자신의 빌더가 배치를 만든다. 패킷 경로의 빌더(Engine)에는 Engine 가 같은 값을 준다.
        _sceneBuilder.setMergeBatchesAcrossMaterials( pDevice->supportsNativeBindlessSampling() );

        const EngineDefaultAssets& engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        RenderPipelineAssetCache&  rpm                 = *_renderPipelineAssetCache;
        if ( rpm.findRenderPass( hashed_string( FrameRendererUtil::kDefaultMainPassName ) ) == nullptr )
            rpm.loadRenderPass( engineDefaultAssets._defaultRenderPass.get() );

        _argumentPipelinePath = pipelineXMLPath;
        return selectPipeline();
    }

    bool FrameRenderer::selectPipeline()
    {
        // 인자, `-gv_renderPipeline`, `-gv_deferredShading`, 팩 설정(`PackEngineSettings::_renderPipeline`), EngineDefaultAssets 의 포워드 순서로 고른다.
        // 디퍼드 경로(와 그 위의 조명)를 측정하거나 검증하려면 `-gv_deferredShading=1` 을 준다.
        const EngineDefaultAssets&        engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const DataPath&                   packPipeline        = engine::getSubsystem<PackEngineSettings>()._renderPipeline;
        FrameRendererUtil::PipelineChoice choice{};
        choice._argumentPath               = _argumentPipelinePath;
        choice._commandLinePath            = gv_renderPipeline;
        choice._deferredPath               = engineDefaultAssets._defaultDeferredPipeline.get();
        choice._gamePath                   = packPipeline.isSet() ? string_view( packPipeline.get() ) : string_view{};
        choice._forwardPath                = engineDefaultAssets._defaultForwardPipeline.get();
        choice._bDeferred                  = gv_deferredShading;
        const string_view resolvedPipeline = FrameRendererUtil::computePipelinePath( choice );
        if ( _status == FrameRendererStatus::Ready && resolvedPipeline == _pipelinePath )
            return true; // 고른 결과가 같다(디퍼드를 켰지만 `gv_renderPipeline` 이 이긴다 등)

        if ( loadPipeline( AssetID{ resolvedPipeline } ) == false )
        {
            _status = FrameRendererStatus::Failed;
            if ( _statusMessage.empty() )
                _statusMessage = string( "pipeline load failed: " ) + string( resolvedPipeline );
            SW_LOG_ERROR( "Not ready — %#", _statusMessage );
            return false;
        }

        _status = FrameRendererStatus::Ready;
        _statusMessage.clear();
        SW_LOG_INFO( "Ready with pipeline '%#'", _pipelinePath );
        return true;
    }

    void FrameRenderer::bindServices( TaskManager* pTaskManager )
    {
        _state._pTaskManager = pTaskManager;
    }

    void FrameRenderer::shutdown()
    {
        const bool bAlreadyShutDown = _status == FrameRendererStatus::Uninitialized && _pDevice == nullptr && _pipelinePath.empty();
        if ( bAlreadyShutDown )
            return;

        // 스냅샷이 가진 머티리얼과 인스턴스의 소유권을 **디바이스가 살아 있을 때** 해제한다. 스냅샷은 소유권을 함께
        // 싣고 있으므로(GPUScene.h) 여기서 비우지 않으면 렌더러가 소멸할 때까지 그것들이 살아 있어, 디바이스가 먼저
        // 사라진 뒤 소멸자가 사라진 디바이스에 GPU 자원을 반환하려 한다(해제 후 사용).
        // 미리보기 씬 렌더러도 같은 디바이스의 자원을 든다 — 디바이스가 살아 있을 때 먼저 내린다. 그 렌더러들은 이 렌더러의 PSO 캐시를 빌려 쓰므로
        // 캐시를 놓는 `releasePassResources` 보다 먼저여야 한다.
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            preview._renderer->shutdown();
        }
        _listScenePreviewRenderer.clear();
        _scenePreviewRendererCount.store( 0 );
        _state._graph.releaseCommandLists();
        if ( _pDevice != nullptr )
            _state._gpuScene.releaseGPU( _pDevice );
        _state._gpuScene.clear();
        _sceneBuilder.clear();

        // 추가 뷰는 통째로 해제한다(빌린 렌더 텍스처, 컬링 상수 버퍼, 리스트). 주 시점의 리스트도 이 디바이스의 것이다.
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            releaseExtraView( *pView );
        }
        _state._listExtraView.clear();
        _state._mainView._commandList.reset();
        _directViewScheduler.clear();
        releaseTransientResources();
        releasePassResources();
        _state._graph.clear();
        // 패스 실행의 그래프 실행 상태(이름별 리소스 상태)와 프레임 리스트도 이 디바이스의 것이다.
        _passExecutor.forgetDevice();
        // 패스와 파이프라인 에셋은 위의 패스 자원과 그래프를 해제한 **뒤에** 비운다. 그쪽이 이 캐시의 포인터를 가지고 있다.
        if ( _renderPipelineAssetCache != nullptr )
        {
            _renderPipelineAssetCache->shutdown();
            _renderPipelineAssetCache.reset();
        }
        _state._frameContext._pCmd = nullptr;
        _pDevice                   = nullptr;
        // 다음 디바이스(백엔드 교체)는 새 GPU 컨텍스트를 연다. 같은 주소에 새 디바이스가 생겨도 옛 시계 기준을 쓰지 않도록 버린다.
        _state._gpuPassTimeline.forgetDevice();
        _state._pTaskManager = nullptr;
        _status              = FrameRendererStatus::Uninitialized;
        _statusMessage.clear();
        _bCallbacksBound = SW_FALSE;
        _pipelinePath.clear();
        SW_LOG_INFO( "Shut down." );
    }

    bool FrameRenderer::loadPipeline( const AssetID& assetID )
    {
        const string_view pipelineXMLPath = assetID.view();
        _pipelinePath                     = pipelineXMLPath;
        _bCallbacksBound                  = SW_FALSE;
        _state._graph.clear();
        releaseTransientResources();

        if ( _pipelineResource.loadFromXMLFile( assetID ) == false )
        {
            _statusMessage = string( "failed to load pipeline XML: " ) + string( pipelineXMLPath );
            return false;
        }

        if ( _renderPipelineAssetCache != nullptr )
        {
            RenderPipelineAssetCache& rpm = *_renderPipelineAssetCache;
            rpm.loadPipeline( pipelineXMLPath );
            for ( const string& passRef : _pipelineResource.getDesc()._listRenderPassRef )
            {
                if ( passRef.empty() == false )
                    rpm.loadRenderPass( passRef );
            }
        }

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        if ( listPass.empty() )
        {
            _statusMessage = string( "no graph passes in pipeline: " ) + string( pipelineXMLPath );
            SW_LOG_ERROR( "%#", _statusMessage );
            return false;
        }

#if SW_PROFILE_COMPILED
        // 타임스탬프 슬롯은 앞쪽 패스에만 있다. 넘는 패스는 GPU 시간(과 GPU.Frame 의 끝)이 빠지므로 로드할 때 알린다.
        const uint32 untimedPassCount = FrameRendererUtil::countUntimedGPUPass( listPass.size() );
        if ( untimedPassCount > 0 )
        {
            SW_LOG_WARNING( "Pipeline '%#' has %# passes; GPU timestamps cover the first %#, so %# pass(es) from '%#' on are not timed "
                            "(constant::kMaxGPUTimestampSlot)",
                            _pipelineResource.getDesc()._name, listPass.size(), FrameRendererUtil::kGPUTimedPassCapacity, untimedPassCount,
                            listPass[FrameRendererUtil::kGPUTimedPassCapacity]._name );
        }
#endif

        float4 sceneColorClear;
        if ( tryGetAttachmentClearColor( FrameRendererUtil::Attachment::kSceneColor, sceneColorClear ) )
            _state._clearColor = sceneColorClear;

        // 파이프라인 패스 설정(셰이더, 진입점, 블렌드, 퍼뮤테이션)으로 PSO 를 다시 만든다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        _bCallbacksBound = _passExecutor.bindPassCallbacks() ? SW_TRUE : SW_FALSE;

        SW_LOG_INFO( "Built graph '%#' (%# passes, callbacks bound once)",
                     _pipelineResource.getDesc()._name, listPass.size() );
        return true;
    }

    // ---------------------------------------------------------------------------
    // 공통 헬퍼: commandList 준비
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // 공통 헬퍼: graph 실행과 commandList 제출
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------

    bool FrameRenderer::execute( IRHIDevice* pDevice, Scene& scene )
    {
        if ( isReady() == false || pDevice == nullptr )
            return false;

        // 주 시점은 씬의 게임 카메라다. 없으면 기본 카메라로 대신 그리지 않고 멈춘다 — 카메라가 없다는 씬 데이터의 결함이 화면으로 가려진다.
        CameraComponent* pMainCamera = scene.pickActiveGameCamera() ? scene.getActiveGameCamera() : nullptr;
        if ( pMainCamera == nullptr )
        {
            // 프레임마다 도는 길이라 장면 이름별로 한 번만 알린다.
            SW_LOG_ERROR_ONCE_FOR( scene.getName(), "Scene '%#' has no usable camera - nothing is drawn", scene.getName() );
            return false;
        }
        Scene* const pScene = &scene;

        _pDevice                   = pDevice;
        _state._outputRenderTarget = 0;
        // 주 시점의 출력 설정(사각형, 배율, 끌 기능, 컷)은 패킷 경로와 같은 규칙으로 카메라에서 읽는다.
        _state._mainView._settings           = RenderViewCollector::makeMainSettings( pMainCamera, RenderViewCollector::kDirectPathAutoScale ); // 자동 렌더 배율은 패킷 경로(RenderPacketBuilder)만 쓴다: 직접 경로(테스트 · 초상화 베이크)는 프레임 시계가 없고 출력이 결정적이어야 한다
        _state._mainView._settings._viewMode = getViewMode();                                                                                   // 직접 경로(테스트)는 주 출력 모드를 쓴다. 패킷 경로는 Engine 가 싣는다.
        // 씬 직접 경로(에디터, 테스트)도 패킷 경로와 **같은 라이트 버퍼**를 쓴다. 경로마다 조명이
        // 다르면 에디터에서 본 이미지와 게임 화면이 달라진다.
        collectSceneLights( pScene, _listScratchLight, &_listScratchShadowLightIndex, &_listScratchLightProbe, &_listScratchLightCookie, &_listScratchRealtimeProbe );
        // 주 광원(그림자 배치, 앰비언트)도 패킷 경로(Engine)와 **같은 규칙**으로
        // 씬에서 읽는다. 주의: 이 경로가 주 광원을 채우지 않으면 테스트가 씬에 둔 방향광이 이미지에 반영되지 않아,
        // 조명이 필요한 픽셀 검증(블룸이 1을 넘는 위치)이 불가능해진다.
        DirectionalLightComponent* pKeyLight    = scene.findActiveDirectionalLight();
        DirectionalLightComponent* pShadowLight = scene.findShadowCastingDirectionalLight();
        if ( pKeyLight != nullptr )
        {
            const float3 lightDir            = pKeyLight->getLightDirection();
            const float3 lightColor          = pKeyLight->getColor();
            _state._frameLight._dirIntensity = float4{ lightDir._x, lightDir._y, lightDir._z, pKeyLight->getIntensity() };
            _state._frameLight._colorAmbient = float4{ lightColor._x, lightColor._y, lightColor._z, pKeyLight->getAmbient() };
        }
        else
        {
            _state._frameLight = FrameLightState{};
        }
        // 하늘도 패킷 경로(RenderPacketBuilder)와 같은 규칙으로 씬에서 읽는다.
        SkyGradient sceneSky;
        const bool  bSceneSky = scene.getObjectManager() != nullptr && SkyComponent::findSky( *scene.getObjectManager(), sceneSky );
        applyFrameSky( bSceneSky, sceneSky );
        // 안개도 같다 — 물속이면 물의 안개가 컴포넌트의 값을 덮는다(FogComponent::findFog).
        FogSettings sceneFog;
        const bool  bSceneFog = scene.getObjectManager() != nullptr && FogComponent::findFog( *scene.getObjectManager(), pMainCamera->getCameraPosition(), sceneFog );
        _fog.applyFrameFog( bSceneFog, sceneFog );
        // 출력 크기(인자가 없으면 스왑체인)를 먼저 정한다 — 주 시점의 비율이 그것을 본다.
        ensurePassResources();
        ensureTransientResources( _directOutputWidth, _directOutputHeight );
        // 주 시점 뷰-투영은 하나다. 그림자 맞춤과 그리기가 같은 행렬을 쓰고, 비율은 패킷 경로(`RenderPacketBuilder`)와 같은 규칙(출력 × 사각형)이다.
        // 둘을 따로 측정하면 출력 설정의 비율과 풀의 비율이 어긋나 그림자 볼륨이 그리는 절두체를 덮지 못한다.
        const float4x4 mainCameraViewProj = pMainCamera->getViewProjectionMatrix( RenderViewCollector::computeAspect( _state._mainView._settings, _state._outputWidth, _state._outputHeight ) );
        // 그림자 배치(볼륨 맞춤 · 아틀라스 칸 · 점광 · 스폿 타일)는 패킷 경로(Engine)와 같은 함수로 주 시점 카메라의 뷰-투영에 맞춘다.
        ShadowSetupUtil::populateFrame( pKeyLight != nullptr ? pShadowLight : nullptr, &mainCameraViewProj, getShadowMapResolution(), _listScratchShadowLightIndex, _listScratchLight,
                                        _state._frameShadow );
        applyFrameShadow();
        // 쿠키를 쓰는 2D 빛에 아틀라스 칸을 나눠 원소에 적는다(업로드 전). 칸에 그리는 것은 기록 중 프리패스(drawLight2DCookieAtlas)다.
        _state._cookieAtlas.assignCells( pDevice, _listScratchLightCookie, _listScratchLight );
        _state._listFrameRealtimeProbe = _listScratchRealtimeProbe;
        _state._lightBuffer.populate( _listScratchLight, _state._frameShadow._listTileElement, _listScratchLightProbe, _state._listFrameRealtimeProbe.empty() == false );
        resetPassCbRing();
        _passExecutor.setIdentityWorld( _state._frameContext );
        updatePassConstants( _state._frameContext, mainCameraViewProj );
        _passExecutor.resetViewRecordState();

        const float3 cameraPos                        = pMainCamera->getCameraPosition();
        _state.view( RenderViewType::Main )._position = cameraPos; // 정렬 키(카메라까지의 거리)와 컬링이 같은 값을 본다.
        // 추가 뷰(캡처 카메라, 화면 사각형)는 패킷 경로(Engine)와 같은 수집기와 스케줄러로 고른다. 시각은 렌더러 시계를 쓴다(테스트는 고정할 수 있다).
        _listDirectViewScratch.clear();
        const shared_ptr<const OcclusionTileSnapshot> occlusionSnapshot = _passExecutor.getOcclusionSnapshot();
        if ( scene.getObjectManager() != nullptr )
        {
            RenderViewCollector::collectExtraViews( *scene.getObjectManager(), pMainCamera, _state.view( RenderViewType::Main )._viewProj, cameraPos, _state._outputWidth, _state._outputHeight, RenderViewCollector::kDirectPathAutoScale,
                                                    static_cast<float64>( _state.getAnimationTime() ), RenderViewCollector::getDefaultBudget(), _directViewScheduler,
                                                    occlusionSnapshot.get(), _listDirectViewScratch );
        }
        // 추가 뷰마다 자기 카메라에 맞춘 방향광 그림자 — 패킷 경로(RenderPacketBuilder)와 같은 함수다.
        for ( RenderViewRequest& request : _listDirectViewScratch )
        {
            if ( request._bRender == SW_TRUE )
                request._shadow = ShadowSetupUtil::makeViewShadowFor( pKeyLight != nullptr ? pShadowLight : nullptr, request._viewProj, _state._frameShadow );
        }
        // 패킷 경로와 **같은 경로**다. 빌더가 스냅샷을 만들고 RT 쪽이 받는다. 렌더 스레드 쪽 GPUScene 에는 씬을 읽는 메서드가 없다.
        // 주 카메라의 정렬 축도 패킷 경로(Engine)처럼 설정한다. 직교 카메라는 시선 축을, 원근 카메라는 거리를 쓴다.
        _sceneBuilder.setTransparentSortAxis( engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( pMainCamera->isOrthographic(), pMainCamera->getCameraForward() ) );
        _sceneBuilder.populateFromScene( pScene, cameraPos );
        _sceneBuilder.computeViewTransparentOrders( _listDirectViewScratch );
        prepareExtraViews( _listDirectViewScratch );
        {
            // 스크래치 하나를 돌려 쓴다. 맞바꾸기라서 지난 스냅샷의 저장소가 여기로 돌아온다.
            _sceneBuilder.exportCPUSnapshot( _sceneSnapshotScratch );
            _state._gpuScene.adoptCPUSnapshot( _sceneSnapshotScratch );
        }
        return uploadSceneAndSubmit( pDevice, "execute" );
    }

    bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        const bool bNotExecutable = isReady() == false || pDevice == nullptr || packet._bValid == SW_FALSE;
        if ( bNotExecutable )
            return false;

        // 주 시점이 없는 패킷은 그리지 않는다 — 기본 카메라로 대신 그리면 카메라가 없다는 결함이 화면으로 가려진다. 활성 씬이 없는 프레임은
        // 그릴 3D 내용도 없어 조용히 넘기고, 3D 내용을 실은 채 시점이 빠진 패킷(만든 쪽의 결함)만 한 번 알린다. 씬에 카메라가 없는 것은 게임 스레드가 알린다.
        if ( packet._bHasViewProj == SW_FALSE )
        {
            const bool bCarries3DContent = packet._gpuScene.getInstances().empty() == false || packet._listView.empty() == false;
            if ( bCarries3DContent )
                SW_LOG_ERROR_ONCE( "Render packet carries scene content but no main view (RenderFramePacket::setMainView) - the frame is not drawn" );
            return false;
        }

        // 게임 스레드가 맡긴 텍스처 구간(지형 스플랫 칠하기)을 기록 전에 올린다. 업로드는 뒤이은 드로우보다 먼저 실행된다(IRHIResourceFactory 계약).
        if ( engine::areEngineServicesBound() )
        {
            // 결정적 실행(`AssetManager::isFinishingAssetWorkEachFrame`)이면 아래 프레임당 상한을 모두 푼다 — 상한 안에 무엇이 드느냐가 워커의 도착 순서에 달린다.
            const bool bFinishAssetWork = AssetManager::isFinishingAssetWorkEachFrame();
            // 새 머티리얼의 GPU 초기화(상수 버퍼 · bindless 등록)도 기록 전에 여기서 한다. 올라간(`Ready`) 머티리얼부터 게임 스레드가 그리는 패킷에 싣는다.
            {
                SW_PROFILE_SCOPE( "RT.MaterialInit" );
                engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().initializePending( pDevice, bFinishAssetWork ? UINT32_MAX : MaterialCache::kMaxInitsPerFrame );
            }
            SW_PROFILE_SCOPE( "RT.TextureRegionUpdates" );
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            (void)textures.flushRegionUpdates( pDevice );
            // 워커가 읽어 둔 비동기 텍스처를 올린다 — 디바이스를 쓰는 스레드는 여기다(DX11 즉시 컨텍스트 · GL). 프레임당 상한이 있다.
            // 결정적 실행이면 상한 없이 모두 올리고 업로드 끝남까지 이 자리에서 기다린다.
            if ( bFinishAssetWork )
                (void)textures.finishPendingLoads( pDevice );
            else
                (void)textures.completePendingLoads( pDevice, TextureCache::kMaxUploadsPerFrame );
            // 아래쪽 밉만 올린 텍스처의 위쪽 밉을 프레임 예산 안에서 업로드한다(밉 스트리밍). 옛 자원은 게임 스레드가 해제한다(`Engine`).
            (void)textures.updateStreaming( pDevice, bFinishAssetWork ? UINT32_MAX : TextureCache::kMaxStreamingUpgradesPerFrame );
        }

        _pDevice                   = pDevice;
        _state._outputRenderTarget = packet._outputRenderTarget;
        _state._mainView._settings = packet._mainView;
        // _state._gpuScene 은 FrameRenderer 가 프레임 사이에 계속 소유한다(GPU 버퍼, 핸들, 머티리얼 데이터 버퍼를 보존한다).
        // 패킷에서는 CPU 스냅샷(인스턴스와 배치 목록)만 옮겨 온다. 주의: 통째로 move 하면 직전 프레임에 업로드한
        // GPU 버퍼와 디스크립터를 releaseGPU() 없이 잃어버려 매 프레임 새로 만드는 누수가 된다.
        _state._gpuScene.adoptCPUSnapshot( packet._gpuScene );
        // 캔버스(화면 2D)도 패킷으로만 온다(렌더 스레드는 위젯을 볼 수 없다). 맞바꾸기라 지난 프레임의 저장소가 패킷으로 돌아간다.
        setCanvasFrame( packet._canvas );

        // 주 광원은 씬이 아니라 패킷으로 온다. 렌더 스레드는 씬을 볼 수 없다.
        if ( packet._bHasLight != SW_FALSE )
        {
            _state._frameLight._dirIntensity = packet._lightDirIntensity;
            _state._frameLight._colorAmbient = packet._lightColorAmbient;
        }
        else
        {
            _state._frameLight = FrameLightState{};
        }
        // 하늘도 패킷으로만 온다.
        applyFrameSky( packet._bHasSky != SW_FALSE, packet._sky );
        _fog.applyFrameFog( packet._bHasFog != SW_FALSE, packet._fog );
        // 그림자 배치도 패킷으로만 온다. 맞바꾸기라 지난 프레임의 저장소가 패킷으로 돌아간다.
        std::swap( _state._frameShadow, packet._shadow );
        applyFrameShadow();
        // 씬의 모든 라이트다. 방향광과 점광이 한 버퍼에 섞여 올라가고 포워드와 디퍼드가 같이 읽는다. 점광 · 스폿 그림자 타일은 그 뒤에 붙는다.
        // 비어 있으면 셰이더는 앰비언트만 더한다 — 라이트 컴포넌트가 없는 씬은 어둡다(키 라이트로 대신 비추지 않는다).
        _state._cookieAtlas.assignCells( pDevice, packet._listLightCookie, packet._listLight );
        _state._listFrameRealtimeProbe = packet._listRealtimeProbe;
        _state._lightBuffer.populate( packet._listLight, _state._frameShadow._listTileElement, packet._listLightProbe, _state._listFrameRealtimeProbe.empty() == false );

        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        _passExecutor.setIdentityWorld( _state._frameContext );
        // 시드는 씬 경로와 같은 updatePassConstants 로 채운다. 따로 채우면 라이트, 블룸, 아웃라인 색 상수가
        // 빠지기 쉽다. 뷰-투영은 패킷의 주 시점이다(역행렬과 절두체도 함께 갱신된다).
        updatePassConstants( _state._frameContext, packet._viewProj );
        _state.view( RenderViewType::Main )._position = packet._cameraPos;
        // 추가 뷰의 풀, 텍스처, 컬링 슬롯을 업로드 전에 맞춘다(기록 중에는 만들 수 없다).
        prepareExtraViews( packet._listView );
        // 값 업로드와 바인딩은 드로우 직전에 ShaderParameterBinder 가 한다. 여기서는 시드만 채운다.
        _passExecutor.resetViewRecordState();

        return uploadSceneAndSubmit( pDevice, "executePacket" );
    }

    void FrameRenderer::updateLightBuffer( IRHIDevice* pDevice )
    {
        // 그리드 번호는 이 목록의 순서다 — 주 시점이 0 번, 이번 프레임에 그리는 추가 뷰가 그 뒤다. 뷰-투영은 그 뷰의 g_ViewProj 와 같은 값이다.
        _listScratchGridViewProj.clear();
        _state._mainView._lightGridIndex = invalid_index::kUint32;
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            pView->_lightGridIndex = invalid_index::kUint32;
        }
        const LightGridProducer producer = selectLightGridProducer();
        if ( producer != LightGridProducer::None )
        {
            _state._mainView._lightGridIndex = static_cast<uint32>( _listScratchGridViewProj.size() );
            _listScratchGridViewProj.push_back( _state.view( RenderViewType::Main )._viewProj );
            for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
            {
                if ( pView->_bRenderThisFrame == SW_FALSE )
                    continue;
                pView->_lightGridIndex = static_cast<uint32>( _listScratchGridViewProj.size() );
                _listScratchGridViewProj.push_back( pView->_cullInput._viewProj );
            }
        }
        // GPU 방출기의 파티클 라이트 — 방출기가 요청한 슬롯의 합이 라이트 버퍼의 GPU 구간 크기다(GPU 생산자가 아니면 버퍼가 거절하고 오류를 남긴다).
        // 범위 나누기는 시뮬레이션(prepareFrame)이 같은 함수로 다시 한다.
        const VFXGPULightAssignment lightAssignment = VFXGPUSimulation::assignLightRanges( _state._gpuScene.getParticleBatches(), _state._gpuScene.getVFXMeshEmitters(), _listScratchGPULightRange );
        _state._lightBuffer.update( pDevice, _listScratchGridViewProj, producer, lightAssignment._slotCount );
    }

    LightGridProducer FrameRenderer::selectLightGridProducer()
    {
        const LightGridProducer requested = gv_lightCulling;
        if ( requested == LightGridProducer::None )
            return LightGridProducer::None;
        if ( requested == LightGridProducer::CPU )
            return LightGridProducer::CPU;
        // GPU 생산자는 컴퓨트 PSO 가 있어야 한다. 없으면 그리드 버퍼를 채울 쪽이 없다 — 채우지 않은 버퍼를 읽게 두지 않고 그리드 없이 그린다(그림은 같고 비용만 다르다).
        if ( _psoManager.getEnginePSO( RenderPassType::LightGrid ) != 0 )
            return LightGridProducer::GPU;
        if ( _bLightGridPSOMissingLogged == SW_FALSE )
        {
            _bLightGridPSOMissingLogged = SW_TRUE;
            SW_LOG_ERROR( "The light grid compute PSO (%#) is missing - every pixel walks every light", engine::getSubsystem<EngineDefaultAssets>()._shaderLightGrid.c_str() );
        }
        return LightGridProducer::None;
    }

    void FrameRenderer::setCanvasFrame( CanvasFrameData& inoutFrame )
    {
        std::swap( _state._canvasFrame, inoutFrame );
    }

    bool FrameRenderer::uploadSceneAndSubmit( IRHIDevice* pDevice, const utf8* pCallerName )
    {
        // 모듈 구간(ExtensionRenderPass M3) — 확장 훅부터 기록 · 제출까지 모듈 코드가 도는 동안 레지스트리의 렌더 잠금을 쥔다(등록 해제의 flush 가 이 잠금을 기다린다).
        // 미리보기 씬 렌더러는 모듈 확장을 받지 않는다(확장은 주 렌더러의 파이프라인에 붙는다). 구간이 끝나면 모듈이 넣은 콜백을 모두 놓는다(소멸 코드가 모듈 안에 있다).
        RenderExtensionRegistry* const pExtensionRegistry = _bScenePreview == SW_FALSE ? engine::findSubsystem<RenderExtensionRegistry>() : nullptr;
        if ( pExtensionRegistry == nullptr )
            return uploadSceneAndSubmitFrame( pDevice, pCallerName );
        RenderExtensionRegistry::RenderScope moduleScope( *pExtensionRegistry );
        // 선언이 그래프에 묶인 것과 다르면 다음 `bindPassCallbacks` 가 모듈 노드를 새로 넣는다. 임시 텍스처는 이번 프레임에 그리는 뷰마다 기록 전에 만든다.
        if ( _extensionHost.collectFrame( *pExtensionRegistry, _state._outputWidth, _state._outputHeight ) )
            _bCallbacksBound = SW_FALSE;
        _extensionHost.allocateTextures( _state._mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                _extensionHost.allocateTextures( *pView );
        }
        const bool bOk = uploadSceneAndSubmitFrame( pDevice, pCallerName );
        _extensionHost.releaseFrame();
        return bOk;
    }

    bool FrameRenderer::uploadSceneAndSubmitFrame( IRHIDevice* pDevice, const utf8* pCallerName )
    {
        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다. 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 되었는지는 upload 뒤에 areIndirectCountsGPUFilled() 가 알려 준다.
        _state._gpuScene.setIndirectCountsFilledByGPU( _passExecutor.getPrepass().usesGPUGeneratedCommands() );
        // 모프 풀 오프셋은 배치 테이블에 실려 업로드 시점에 완성되어 있어야 한다.
        _passExecutor.getPrepass().prepareMeshMorphPool();
        bool bVertexPool = gv_vertexPool;
        _state._gpuScene.setVertexPoolEnabled( bVertexPool );
        _state._gpuScene.upload( pDevice );
        // 라이트 버퍼는 뷰가 정해진 뒤에 올린다 — 뷰마다 라이트 그리드가 붙는다(주 시점과 이번 프레임에 그리는 추가 뷰).
        updateLightBuffer( pDevice );
        // 볼륨 안개의 버퍼와 입력은 라이트 그리드(주 시점의 그리드 번호)가 정해진 뒤, 기록 전에 갖춘다.
        _fog.prepareVolumetricFog( pDevice, _state.view( RenderViewType::Main ), _state._frameLight._dirIntensity, _state._frameLight._colorAmbient, _state._lightBuffer, _state._mainView._lightGridIndex );
        // 실시간 라이트 프로브 갱신도 라이트 버퍼(프로브 버퍼의 UAV)가 올라간 뒤, 기록 전에 갖춘다.
        prepareRealtimeProbes( pDevice );
        // 추가 뷰의 투명 순서(스냅샷)를 뷰 자원에 옮긴다. 컬링과 정렬 디스패치, 기록보다 먼저 한다.
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                applyViewTransparentOrder( *pView );
        }

        if ( _bCallbacksBound == SW_FALSE )
            _bCallbacksBound = _passExecutor.bindPassCallbacks() ? SW_TRUE : SW_FALSE;

        // 상수 버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다.
        ensurePassCbCapacityForFrame();

        // 머티리얼 퍼뮤테이션 PSO 도 같은 이유로 여기서 정한다. 기록 중에는 만들 수 없고, 패스들은 병렬로 기록된다.
        // 만드는 일은 빌더 스레드가 한다(`MaterialPSOBuilder`) — 먼저 지난 프레임까지 끝난 결과를 캐시에 합치고, 이번 프레임에 필요한 키를 줄에 넣는다.
        _psoManager.pumpMaterialPSOBuilds();
        const uint32 viewModeMask = collectFrameViewModeMask();
        _psoManager.ensureMaterialPSOs( viewModeMask );
        // 아직 씬에 없는 고정 머티리얼(프리로드)의 PSO 도 같은 자리에서 미리 만든다(급하지 않은 줄). 줄은 하나라 주 렌더러만 꺼낸다 — 미리보기 렌더러가 꺼내 가면 주 렌더러가 만들지 못한다.
        if ( _bScenePreview == SW_FALSE )
            _psoManager.warmPinnedMaterialPSOs( viewModeMask );
        // 빌더 스레드를 쓰지 않는 경로(GL · `-gv_psoAsync=0`)는 방금 넣은 줄을 이 자리에서 만들어 같은 프레임에 그린다.
        _psoManager.pumpMaterialPSOBuilds();

        // 캔버스의 아틀라스 텍스처, 사각형 버퍼, 렌더 텍스처 대상도 기록 전에 갖춘다(Canvas 패스와 대상 그리기는 그리기만 한다).
        _state._canvasRenderer.prepareFrame( *pDevice, _state._canvasFrame, _psoManager.getCache() );
        // 입자 버퍼 · 입자 텍스처 · 입자 PSO 도 같다(투명 패스는 그리기만 한다).
        _psoManager.ensureParticlePSO();
        _state._particleRenderer.prepareFrame( *pDevice, _state._gpuScene.getParticleBatches(), _state._gpuScene.getVFXMeshEmitters(), _passExecutor.getPrepass().makeVFXSimulationInput() );

        if ( _passExecutor.prepareCommandList( pDevice, pCallerName ) == false )
            return false;

        const bool bOk             = _passExecutor.submitGraph( pDevice );
        _lastIndirectDrawCallCount = _passExecutor.getIndirectDrawCallCount();
        // 타임스탬프 슬롯은 디바이스에 하나다 — 미리보기 렌더러가 읽고 켜고 끄면 주 렌더러의 GPU 시간이 흔들린다.
        if ( _bScenePreview == SW_FALSE )
            _state._gpuPassTimeline.readAndReport( pDevice, _pipelineResource.getGraphPass() );
        return bOk;
    }

    void FrameRenderer::prepareRealtimeProbes( IRHIDevice* pDevice )
    {
        const bool bPipelineReady = _psoManager.getEnginePSO( RenderPassType::ProbeUpdate ) != 0;
        _state._probeUpdater.prepare( pDevice, _state._listFrameRealtimeProbe, _state._lightBuffer, _state._frameSkySource, _state._bHasFrameSky == SW_TRUE, bPipelineReady );
    }

    namespace
    {
        constexpr float4 kDefaultOutlineColor{ 0.08f, 0.05f, 0.12f, 0.85f };
    } // namespace

    void FrameRenderer::applyFrameSky( bool bHasSky, const SkyGradient& sky )
    {
        // 셰이더 상수 배치는 SkyGradient 머리 주석이 기준이다(sky.hlsl 의 evaluateSky 가 같은 순서로 읽는다).
        _state._bHasFrameSky   = bHasSky ? SW_TRUE : SW_FALSE;
        _state._frameSkySource = bHasSky ? sky : SkyGradient{};
        if ( bHasSky == false )
        {
            _state._frameSky = FrameSkyState{};
            return;
        }
        _state._frameSky._zenithExponent  = float4{ sky._zenithColor._x, sky._zenithColor._y, sky._zenithColor._z, sky._horizonExponent };
        _state._frameSky._horizon         = float4{ sky._horizonColor._x, sky._horizonColor._y, sky._horizonColor._z, 0.0f };
        _state._frameSky._groundIntensity = float4{ sky._groundColor._x, sky._groundColor._y, sky._groundColor._z, sky._intensity };

        // 대기 산란 — 셰이더가 해 방향 · 색을 키 라이트 상수에서 읽으므로 여기에는 대기의 값만 싣는다. 그러데이션 모드는 w = 0 이라 셰이더가 그 길을 고른다.
        const SkyAtmosphere& atmosphere      = sky._atmosphere;
        const float3         rayleigh        = atmosphere.computeRayleighBeta();
        _state._frameSky._atmosphereRayleigh = float4{ rayleigh._x, rayleigh._y, rayleigh._z, sky._mode == SkyMode::Atmosphere ? 1.0f : 0.0f };
        _state._frameSky._atmosphereMie      = float4{ atmosphere.computeMieBeta(), SkyAtmosphere::kMieScaleHeightKm, atmosphere._mieAnisotropy, atmosphere._sunIntensity };
        _state._frameSky._atmosphereGeometry = float4{ atmosphere._planetRadiusKm, atmosphere._atmosphereHeightKm, atmosphere._observerAltitudeKm, atmosphere._sunDiscAngle };

        // 구름 층 — 바람 방향은 라디안을 단위 벡터로 바꿔 싣는다.
        const SkyClouds& clouds      = sky._clouds;
        _state._frameSky._cloudShape = float4{ MathUtil::saturate( clouds._coverage ), clouds._altitudeKm, clouds._scale, MathUtil::saturate( clouds._opacity ) };
        _state._frameSky._cloudWind  = float4{ MathUtil::cos( clouds._windDirection ), MathUtil::sin( clouds._windDirection ), clouds._windSpeedKmPerSecond, clouds._softness };
        _state._frameSky._cloudColor = float4{ clouds._color._x, clouds._color._y, clouds._color._z, clouds._bEnabled == SW_TRUE ? 1.0f : 0.0f };
    }

    void FrameRenderer::applyFrameShadow()
    {
        // 게임 스레드가 배치를 만들지 않은 프레임(씬 없음)이거나 그림자 품질이 그 사이 바뀌었으면 아틀라스 레이아웃을 지금 크기로 다시 만든다.
        const uint32 atlasResolution = getShadowMapResolution();
        if ( _state._frameShadow._layout.getAtlasResolution() != atlasResolution )
            _state._frameShadow._layout = ShadowAtlasLayout::make( atlasResolution, _state._frameShadow._layout.getCascadeCount(), static_cast<uint32>( _state._frameShadow._listLocalTile.size() ) );
        // 게임 스레드가 그림자 배치를 만들지 않은 프레임(씬 없음)은 그림자 없는 뷰다. 빛이 없는데 그림자 볼륨을 지어내지 않는다.
        if ( _state._frameShadow._mainView._bValid == SW_FALSE )
            _state._frameShadow._mainView = ShadowSetupUtil::makeUnshadowedView( _state._frameShadow._layout );
        _state._mainView._shadow = _state._frameShadow._mainView;
    }

    void FrameRenderer::updatePassConstants( FramePassContext& context, const float4x4& mainViewProj )
    {
        // **프레임당 한 번** 프레임 시드(_state._frameContext)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        //
        // 주 시점 뷰-투영은 호출자가 카메라에서 만들어 넘긴다(씬 직접 경로는 씬의 게임 카메라, 패킷 경로는 패킷의 주 시점).
        // 그림자는 `applyFrameShadow` 가 준비한 주 시점의 것이다(그림자 빛이 없으면 그림자 없는 뷰).
        _passExecutor.applyViewShadowConstants( context, _state._mainView._shadow );
        _state.view( RenderViewType::Shadow ).setViewProjection( _state._mainView._shadow._cullViewProj );

        beginViewHistory( _state._mainView );
        // 지난 뷰-투영과 견주므로 applyViewProjection(지난 것을 이번 것으로 바꾼다) 앞이다.
        // 주 시점의 첨부만 렌더 타깃 목록(에디터 패널)에 싣는다 — 가리키는 곳이 바뀌거나 타깃을 만들고 놓았을 때만 다시 싣는다.
        const FrameViewResources::PassSelectionResult selection = _viewResources.applyViewPassSelection( _state._mainView, mainViewProj );
        if ( selection._bTargetsRepointed == SW_TRUE || selection._bTargetsChanged == SW_TRUE )
            publishRenderTargets();
        if ( selection._bTargetsChanged == SW_TRUE )
            reportRenderTargetMemory();
        _passExecutor.applyViewProjection( context, _state._mainView, mainViewProj );
        context._passValues.setMatrix( passConstantNames()._world, context._world );

        context._passValues.setFloat4( passConstantNames()._keyLightDirIntensity, _state._frameLight._dirIntensity );
        context._passValues.setFloat4( passConstantNames()._keyLightColor, _state._frameLight._colorAmbient );
        context._passValues.setFloat4( passConstantNames()._skyZenith, _state._frameSky._zenithExponent );
        context._passValues.setFloat4( passConstantNames()._skyHorizon, _state._frameSky._horizon );
        context._passValues.setFloat4( passConstantNames()._skyGround, _state._frameSky._groundIntensity );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereRayleigh, _state._frameSky._atmosphereRayleigh );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereMie, _state._frameSky._atmosphereMie );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereGeometry, _state._frameSky._atmosphereGeometry );
        context._passValues.setFloat4( passConstantNames()._skyCloudShape, _state._frameSky._cloudShape );
        context._passValues.setFloat4( passConstantNames()._skyCloudWind, _state._frameSky._cloudWind );
        context._passValues.setFloat4( passConstantNames()._skyCloudColor, _state._frameSky._cloudColor );
        context._passValues.setFloat4( passConstantNames()._fogColorDensity, _fog.getFrameFog()._colorDensity );
        context._passValues.setFloat4( passConstantNames()._fogHeightParams, _fog.getFrameFog()._heightParams );
        context._passValues.setFloat4( passConstantNames()._fogInscatter, _fog.getFrameFog()._inscatter );
        context._passValues.setFloat4( passConstantNames()._fogVolumeParams, _fog.getFrameFog()._volumeParams );
        context._passValues.setFloat4( passConstantNames()._outlineColor, kDefaultOutlineColor );
        _passExecutor.applyViewPassConstants( context, _state._mainView );
    }

    void FrameRenderer::beginViewHistory( FrameViewTarget& view )
    {
        view._bHistoryReset = ( view._bHasHistory == SW_FALSE || view._settings._bCut == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        view._bHasHistory   = SW_TRUE;
        // TAA 기록은 TAA 가 도는 프레임만 쓴다. 지난 프레임이 쓰지 않았으면 기록은 그 앞 화면이라 버린다 — 뷰 상태(후처리 켬)가 바뀐 첫 프레임이다.
        // 지난 프레임에 TAA 가 돌았는지는 그 프레임의 패스 고르기(`applyViewPassSelection`)가 남긴 `_bTAAHistoryWritten` 하나다 — 실행도 같은 결과를 본다.
        // 후처리 뒤의 효과(피사계 심도 · 모션 블러)가 켜지고 꺼지는 것은 TAA 뒤라 기록과 무관하다.
        view._bTAAHistoryReset = ( view._bHistoryReset == SW_TRUE || view._bTAAHistoryWritten == SW_FALSE ) ? SW_TRUE : SW_FALSE;
    }

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != SW_FALSE )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        if ( _state._passCbRing.initialize( _pDevice ) == false )
        {
            // 준비 깃발을 세우지 않아 다음 프레임에 다시 시도한다 — 같은 실패를 프레임마다 남기지 않게 한 번만 알린다.
            SW_LOG_ERROR_ONCE( "Pass constant buffer slots could not be created - passes stay unprepared" );
            return;
        }
        // 직렬 경로 시드는 0번 슬롯을 쓴다(패스별 경로는 acquirePassCb 로 덮어쓴다).
        _state._passCbRing.getSeedSlot( _state._frameContext._passCb, _state._frameContext._passCbIndex );

        // 만들지 못한 상수버퍼는 `isValid()` 가 걸러 그 디스패치만 꺼진다(그리기는 산다). 꺼진 이유는 여기서 한 번 알린다.
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            if ( row._pSlot->create( _pDevice, row._byteSize ) == false )
                SW_LOG_ERROR( "Failed to create the %# constant buffer - its compute dispatch is skipped", row._pUsage );
        }

        // 패스 실행의 상주 자원(assert 링 · NaN 감시 · 컬링 자리표)을 PSO 보다 먼저 만든다 — assert 변형 PSO 는 assert 링 없이는 돌릴 수 없다.
        _passExecutor.ensureResources();

        // 엔진 패스 PSO · 출력 포맷 변형 · 머티리얼 폴백 버퍼(셋업에서만 만든다 — 패스는 병렬로 기록된다).
        _psoManager.createPassPSOs();

        // 씬 메시는 **인다이렉트 드로우 하나로만** 그린다(컬링 · 정렬 · 인스턴스 애니메이션이 모두 그 경로에 붙어 있다).
        // 지원하지 않는 백엔드가 생기면 조용히 안 그리는 대신 여기서 크게 알린다.
        const RHICapabilities caps = _pDevice->getCapabilities();
        if ( caps._bIndirectDraw == SW_FALSE )
            SW_LOG_ERROR( "This backend does not support indirect draws - scene meshes cannot be drawn." );

        _bPassResourcesReady = SW_TRUE;
        SW_LOG_INFO( "Pass PSOs/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     _psoManager.getEnginePSO( RenderPassType::Shadow ), _psoManager.getEnginePSO( RenderPassType::ForwardOpaque ),
                     _psoManager.getEnginePSO( RenderPassType::Transparent ), _psoManager.getEnginePSO( RenderPassType::Lighting ),
                     _psoManager.getEnginePSO( RenderPassType::Bloom ), _psoManager.getEnginePSO( RenderPassType::Outline ), static_cast<uint32>( caps._bIndirectDraw ) );
    }

    void FrameRenderer::collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] )
    {
        // 뷰마다 하나씩. 절두체가 다르므로 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
        uint32 rowIndex{ 0 };
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            outArrRow[rowIndex++] = { &_state._arrView[viewIndex]._cullCb, sizeof( FrameRendererUtil::GPUCullParams ), "cull" };
            outArrRow[rowIndex++] = { &_state._arrView[viewIndex]._sortCb, sizeof( FrameRendererUtil::GPUSortParams ), "instance sort" };
        }
        outArrRow[rowIndex++] = { &_state._deform._instanceAnimationCb, sizeof( FrameRendererUtil::GPUAnimationParams ), "instance animation" };
        outArrRow[rowIndex++] = { &_state._deform._meshMorphCb, sizeof( FrameRendererUtil::GPUMorphParams ), "mesh morph" };
        outArrRow[rowIndex++] = { &_state._deform._meshSkinCb, sizeof( FrameRendererUtil::GPUSkinParams ), "mesh skin" };
        SW_LOG_ASSERT( rowIndex == _s_kComputeConstantBufferCount, "compute constant buffer table has %# rows, expected %#", rowIndex,
                       _s_kComputeConstantBufferCount );
    }

    void FrameRenderer::releasePassResources()
    {
        // PSO · 레이아웃 · 패스 CB 가 여기서 사라진다. 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다. 백엔드 교체 뒤 빈 화면의 원인이다.)
        _state._frameContext.resetBindingCache();
        // 모듈 요소의 메시(GPU 정점 버퍼)도 이 디바이스의 것이다 — 다음 프레임의 셋업이 다시 읽고 올린다.
        _extensionHost.releaseMeshes();

        // 목록은 하나다. 디바이스가 없으면(초기화 실패) 각 release 가 핸들만 잊는다.
        // 병렬 기록용 커맨드 리스트는 이 디바이스의 것이다. 디바이스가 살아 있을 때 놓는다.
        if ( _pDevice != nullptr )
            _state._graph.releaseCommandLists();

        // PSO · 레이아웃 · 머티리얼 폴백 버퍼. 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 놓는다(`FramePSOManager::release`).
        _psoManager.release();

        _state._gpuScene.releaseGPU( _pDevice );
        // 캔버스의 아틀라스 텍스처 · 사각형 버퍼도 이 디바이스의 것이다(거울은 남아 다음 프레임에 다시 올린다).
        _state._canvasRenderer.release( _pDevice );
        // 입자 버퍼와 빌린 입자 텍스처도 같다(다음 프레임의 prepareFrame 이 다시 만들고 빌린다).
        _state._particleRenderer.release( _pDevice );

        _state._passCbRing.release( _pDevice );
        _state._frameContext._passCb      = 0;
        _state._frameContext._passCbIndex = kInvalidDescriptorIndex;
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            row._pSlot->release( _pDevice );
        }
        // 추가 뷰의 컬링 · 정렬 상수버퍼도 이 디바이스의 것이다(다음 프레임의 prepareExtraViews 가 다시 만든다).
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            pView->_cullInput._cullCb.release( _pDevice );
            pView->_cullInput._sortCb.release( _pDevice );
            releaseViewTransparentOrder( *pView );
        }
        // 주 시점 캐스케이드 컬링 상수버퍼도 마찬가지다(prepareCascadeCullInputs 가 다시 만든다).
        for ( RenderView& cascadeInput : _state._arrCascadeCullInput )
        {
            cascadeInput._cullCb.release( _pDevice );
        }
        _state._cascadeCullCount = 0;
        _passExecutor.releaseResources();
        _state._deform._meshMorphPool.release( _pDevice );
        _state._deform._vertexAnimationPool.release( _pDevice );
        _state._lightBuffer.release( _pDevice );
        _fog.release();
        _state._probeUpdater.release( _pDevice );
        _state._cookieAtlas.release( _pDevice );
        _bPassResourcesReady = SW_FALSE;
    }

    void FrameRenderer::resetPassCbRing()
    {
        _state._passCbRing.beginFrame();
        _state._passCbRing.getSeedSlot( _state._frameContext._passCb, _state._frameContext._passCbIndex );
    }

    void FrameRenderer::ensurePassCbCapacityForFrame()
    {
        const uint32 batchCount = static_cast<uint32>( _state._gpuScene.getOpaqueBatches().size() + _state._gpuScene.getTransparentBatches().size() );
        // 그림자 패스는 타일(캐스케이드 · 점광 · 스폿 면)마다 씬을 한 번씩 그린다 — 어림값의 그림자 한 번을 타일 수로 바꾼다.
        const uint32 shadowTileCount = MathUtil::max( _passExecutor.computeShadowTileCountPerView(), 1u );
        // 모듈 프록시의 요소는 요소마다 슬롯을 하나씩 쓴다(`drawExtensionElements`) — 그리는 패스 · 뷰 수를 모르므로 넉넉히 네 배로 어림한다(모자라면 링이 알리고 다음 프레임이 키운다).
        const uint32 moduleElementCount = static_cast<uint32>( _extensionHost.getDrawElements().size() ) * 4u;
        const uint32 estimate           = batchCount * ( _s_kDrawCbPassEstimate - 1u + shadowTileCount ) + PassConstantRing::kInitialSlotCount + moduleElementCount;
        _state._passCbRing.ensureCapacity( _pDevice, MathUtil::max( estimate, _state._passCbRing.getHighWater() + PassConstantRing::kInitialSlotCount ) );
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePSO( RenderPassType passType ) const
    {
        return _psoManager.getEnginePSO( passType );
    }

    bool FrameRenderer::findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _psoManager.findDesc( pso, outDesc );
    }

    void FrameRenderer::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || _pDevice == nullptr )
            return;

        _psoManager.onShaderRecompiled( shaderPath );

        // **PSO 를 실제로 다시 만든다.** 주의: 바인딩 레이아웃만 새로 만들면 PSO 는 바이트코드를 박아 넣은
        // 객체라 화면이 시작 때 컴파일된 셰이더 그대로다.
        //
        // 순서는 loadPipeline 과 같다. 여기 도달하기 전에 ShaderRecompiler 가 ShaderCache 를 비웠고(수동 리로드)
        // (그래야 새 바이트코드를 집는다) Engine 이 렌더 스레드를 재웠으므로(waitIdle) 안전하다.
        // 셰이더 편집은 개발 중 가끔 있는 일이라 이때의 스톨은 감수한다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        _bCallbacksBound = _passExecutor.bindPassCallbacks() ? SW_TRUE : SW_FALSE;

        // 미리보기 씬 렌더러는 주 렌더러의 PSO 캐시를 함께 쓴다. 주 렌더러가 캐시를 다시 채운 **뒤에** 자기 몫(레이아웃을 가리키는 폴백 버퍼 표,
        // 바인딩 캐시, 패스 콜백)을 다시 만들어야 새 PSO 를 집는다.
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            preview._renderer->onShaderRecompiled( shaderPath, result );
        }
    }

    void FrameRenderer::ensureTransientResources( uint32 overrideWidth, uint32 overrideHeight )
    {
        if ( _pDevice == nullptr )
            return;

        uint32 width  = FrameRendererUtil::kDefaultTransientSize;
        uint32 height = FrameRendererUtil::kDefaultTransientSize;
        if ( overrideWidth > 0 && overrideHeight > 0 )
        {
            width  = overrideWidth;
            height = overrideHeight;
        }
        else
        {
            // 창이 아니라 **스왑체인**의 크기다. 렌더러는 OS 창을 모른다(IRHIDevice::resize 참고).
            if ( _pDevice->getBackBufferWidth() > 0 )
                width = _pDevice->getBackBufferWidth();
            if ( _pDevice->getBackBufferHeight() > 0 )
                height = _pDevice->getBackBufferHeight();
        }
        _state._outputWidth  = width;
        _state._outputHeight = height;

        // 주 시점의 풀은 출력 × 사각형 × 해상도 배율이다(분할 화면의 한 칸 · 낮춘 해상도). 비율을 지키려고 둘 다 같은 배율로 줄인다.
        // 정수 업스케일이면 사각형 픽셀 / 배율(내림)이다 — Present 가 그 배로 키운다.
        const RenderViewSettings& settings   = _state._mainView._settings;
        const uint32              poolWidth  = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( width ) * settings._screenRect._z,
                                                                                         settings._resolutionScale, settings._upscaleZoom );
        const uint32              poolHeight = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( height ) * settings._screenRect._w,
                                                                                         settings._resolutionScale, settings._upscaleZoom );
        if ( _viewResources.updateViewTransients( _state._mainView, poolWidth, poolHeight ) )
        {
            publishRenderTargets();
            reportRenderTargetMemory();
        }
        // Present 뷰포트(출력 × 사각형) — FSR 1 로 키우면 중간 RT 가 이 크기다(executePresentPass 의 뷰포트와 같은 식).
        const uint32 viewportWidth  = static_cast<uint32>( static_cast<float32>( width ) * settings._screenRect._z + 0.5f );
        const uint32 viewportHeight = static_cast<uint32>( static_cast<float32>( height ) * settings._screenRect._w + 0.5f );
        _upscale.prepareUpscale( _state._mainView, viewportWidth, viewportHeight );
        _capture.ensurePresent( _state._outputWidth, _state._outputHeight );
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다. 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>(); pRegistry != nullptr )
            pRegistry->clear();

        _viewResources.releaseViewTransients( _state._mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            _viewResources.releaseViewTransients( *pView );
        }
        // 캡처도 트랜지언트와 크기가 같아야 한다. 같이 버리고 다음 ensureTransientResources 가 새 크기로 만든다.
        _capture.releasePresent();
    }

    void FrameRenderer::reportRenderTargetMemory() const
    {
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        // 주 시점의 첨부마다 크기를 남긴다(렌더 타깃 패널 `RenderTargetRegistry` 와 같은 목록). `[Profile] GPU memory by kind` 가 늘면 어느 첨부가 늘었는지 이 줄로 나눈다(측정 기준을 다시 뜰 때 함께 본다).
        // 풀을 다시 만들 때와 꺼져 있던 효과의 타깃을 처음 만들 때만 부른다.
        uint64 totalBytes = 0;
        for ( const auto& [name, attachment] : _state._mainView._transientPool.getAll() )
        {
            totalBytes += static_cast<uint64>( attachment._width ) * attachment._height * getRHIFormatBytesPerPixel( attachment._format );
        }
        const uint64 totalKilobytes = totalBytes / 1024u;
        SW_LOG_INFO( "[RenderTargets] main view %#x%#: %# KB in %# attachments", _state._mainView._transientPool.getWidth(), _state._mainView._transientPool.getHeight(),
                     totalKilobytes, _state._mainView._transientPool.getAll().size() );
        for ( const auto& [name, attachment] : _state._mainView._transientPool.getAll() )
        {
            const uint64 kilobytes = static_cast<uint64>( attachment._width ) * attachment._height * getRHIFormatBytesPerPixel( attachment._format ) / 1024u;
            SW_LOG_INFO( "[RenderTargets]   %#  %#x%#  %# KB", name, attachment._width, attachment._height, kilobytes );
        }
#endif
    }

    uint32 FrameRenderer::getShadowMapResolution()
    {
        return FrameViewResources::getShadowMapResolution();
    }

    bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const
    {
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            if ( attachment._name == attachmentName )
            {
                outClearColor = attachment._clearColor;
                return attachment._bClear;
            }
        }
        return false;
    }

    void FrameRenderer::publishRenderTargets() const
    {
        RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>();
        // 목록은 주 렌더러의 것이다 — 미리보기 씬 렌더러가 공개하면 렌더 타깃 패널이 미리보기 첨부를 보인다.
        if ( pRegistry == nullptr || _bScenePreview == SW_TRUE )
            return;

        // 화면에 나가는 것이 무엇인지 함께 실어 준다. 패널이 열리자마자 **지금 보이는 그림**을
        // 고를 수 있어야 쓸모가 있다(이름순 첫 번째는 AOColor 라 아무 의미가 없다).
        // 꺼진 효과의 출력이면 그것이 가리키는 원본이 화면에 나간다(`TransientAttachmentPool::forward`).
        const string_view presented = _state._mainView._transientPool.resolveName( getPresentedAttachmentName() );

        vector<RenderTargetInfo> listTarget;
        listTarget.reserve( _state._mainView._transientPool.getAll().size() );
        for ( const auto& [name, attachment] : _state._mainView._transientPool.getAll() )
        {
            if ( attachment._texture == 0 )
                continue;
            RenderTargetInfo info{};
            info._name       = name;
            info._bPresented = ( presented.empty() == false && name == presented ) ? SW_TRUE : SW_FALSE;
            info._texture    = attachment._texture;
            info._width      = attachment._width;
            info._height     = attachment._height;
            info._format     = attachment._format;
            info._bDepth     = FrameRendererUtil::isDepthFormat( info._format ) ? SW_TRUE : SW_FALSE;
            listTarget.push_back( std::move( info ) );
        }

        // 이름순으로 정렬해 둔다. 해시맵 순서는 실행마다 달라서, 정렬하지 않으면 목록이 프레임마다
        // 흔들리는 것처럼 보이고 "어디 있었더라" 를 매번 다시 찾게 된다.
        VectorUtil::sort( listTarget, []( const RenderTargetInfo& lhs, const RenderTargetInfo& rhs )
        { return lhs._name < rhs._name; } );

        pRegistry->publish( std::move( listTarget ) );
    }

    string_view FrameRenderer::getPresentedAttachmentName() const
    {
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getDesc()._listPass )
        {
            if ( pass._resolvedType == RenderPassType::Present && pass._listInput.empty() == false )
                return string_view{ pass._listInput.front() };
        }
        return string_view{};
    }

    /**
     * @brief `-gv_shadowCascadeCulling=1`: 주 시점의 방향광 그림자를 캐스케이드마다 따로 컬링합니다(캐스케이드마다 컬링 슬롯 하나) — 같은 바이너리로 전/후를 비교합니다.
     * @details 0(기본)이면 모든 타일을 덮는 합친 상자 하나로 거른다. 캐스케이드가 둘 이상일 때만 뜻이 있다. 이득은 `perf-compare --cases shadowCascadeCulling` 이 정한다.
     *          기동 때 정한다(슬롯 수가 바뀌면 컬링 버퍼를 다시 만든다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_shadowCascadeCulling, false, "방향광 그림자를 캐스케이드마다 따로 컬링한다(0 이면 모든 타일을 덮는 상자 하나 — 전/후 비교용)" );

    void FrameRenderer::prepareExtraViews( const vector<RenderViewRequest>& listRequest )
    {
        if ( _pDevice == nullptr )
            return;
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            pView->_bSeenThisFrame = SW_FALSE;
        }

        const uint32 requestCount = MathUtil::min( static_cast<uint32>( listRequest.size() ), kMaxExtraRenderView );
        for ( uint32 requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            prepareExtraView( listRequest[requestIndex] );
        }

        // 이번 요청에 없는 뷰(카메라가 사라졌다)는 놓는다. 남은 뷰의 컬링 칸은 목록 순서로 고정 칸 뒤에 붙는다 — 뷰마다 둘(시점, 그림자).
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bSeenThisFrame == SW_FALSE )
                releaseExtraView( *pView );
        }
        VectorUtil::removeIf( _state._listExtraView, []( const unique_ptr<FrameViewTarget>& pView )
        { return pView->_bSeenThisFrame == SW_FALSE; } );
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _state._listExtraView.size() ); ++viewIndex )
        {
            _state._listExtraView[viewIndex]->_cullSlot       = kFirstExtraCullView + viewIndex * kCullSlotsPerExtraView;
            _state._listExtraView[viewIndex]->_shadowCullSlot = kFirstExtraCullView + viewIndex * kCullSlotsPerExtraView + 1u;
        }
        // 캐스케이드 컬링 슬롯은 추가 뷰 슬롯 뒤에 붙는다. 슬롯 수가 바뀌면(캐스케이드 수 · 스위치) 새 칸의 버퍼는 다음 업로드가 만든다.
        _state._cascadeCullSlotBase = kFirstExtraCullView + static_cast<uint32>( _state._listExtraView.size() ) * kCullSlotsPerExtraView;
        _state._cascadeCullCount    = prepareCascadeCullInputs();
        _state._gpuScene.setCullViewCount( _pDevice, _state._cascadeCullSlotBase + _state._cascadeCullCount );
    }

    uint32 FrameRenderer::prepareCascadeCullInputs()
    {
        // 캐스케이드 하나뿐이면 합친 칸이 곧 그 캐스케이드라 나눌 이득이 없다. 그림자 없는 뷰(`makeUnshadowedView`)도 캐스케이드 1 이다.
        const ViewShadowData& shadow       = _state._mainView._shadow;
        const bool            bHasCascades = shadow._bValid == SW_TRUE && shadow._cascadeCount >= 2;
        const bool            bEnabled     = gv_shadowCascadeCulling && _pDevice != nullptr && bHasCascades;
        if ( bEnabled == false )
            return 0;
        for ( uint32 cascadeIndex = 0; cascadeIndex < shadow._cascadeCount; ++cascadeIndex )
        {
            RenderView& input = _state._arrCascadeCullInput[cascadeIndex];
            if ( input._cullCb.isValid() || input._cullCb.create( _pDevice, sizeof( FrameRendererUtil::GPUCullParams ) ) )
                continue;
            // 만들지 못하면 이 프레임은 합친 칸으로 돌아간다(같은 결과, 컬링만 덜 좁다). 칸마다 부분적으로 섞어 쓰지 않는다.
            SW_LOG_ERROR( "Failed to create the cull constant buffer of shadow cascade %# - the combined shadow cull slot is used this frame", cascadeIndex );
            return 0;
        }
        return shadow._cascadeCount;
    }

    FrameViewTarget& FrameRenderer::getOrCreateExtraView( uint64 viewID )
    {
        for ( unique_ptr<FrameViewTarget>& pExisting : _state._listExtraView )
        {
            if ( pExisting->_viewID == viewID )
                return *pExisting;
        }
        _state._listExtraView.push_back( make_unique<FrameViewTarget>() );
        FrameViewTarget& view = *_state._listExtraView.back();
        view._viewID          = viewID;
        return view;
    }

    void FrameRenderer::prepareExtraView( const RenderViewRequest& request )
    {
        FrameViewTarget& view  = getOrCreateExtraView( request._viewID );
        view._bSeenThisFrame   = SW_TRUE;
        view._settings         = request._settings;
        view._outputKind       = request._outputKind;
        view._bRenderThisFrame = request._bRender;
        view._outputWidth      = request._outputWidth;
        view._outputHeight     = request._outputHeight;
        view._hostTarget       = request._outputKind == RenderViewOutputKind::HostTarget ? request._hostTarget : RHITextureHandle{ 0 };
        if ( view._outputKind == RenderViewOutputKind::HostTarget && view._hostTarget == 0 )
            view._bRenderThisFrame = SW_FALSE;

        // 렌더 텍스처는 캐시에서 빌린다 — 머티리얼이 같은 경로로 읽는다. 크기는 텍스처의 실제 크기다(먼저 만든 쪽이 정했다).
        const bool bRenderTexture       = view._outputKind == RenderViewOutputKind::RenderTexture;
        const bool bBorrowRenderTexture = bRenderTexture && ( view._outputPath == request._renderTexture ) == false && engine::areEngineServicesBound();
        if ( bBorrowRenderTexture )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            if ( view._pOutputTexture != nullptr )
                textures.release( view._outputPath.view(), _pDevice );
            textures.declareRenderTarget( request._renderTexture.view(), request._outputWidth, request._outputHeight );
            // 렌더 타깃은 읽을 파일이 없어 기다림이 이 스레드의 GPU 생성뿐이라 동기로 둔다(`CanvasTextureLeases::prepareTargets` 와 같다).
            view._pOutputTexture = textures.acquire( request._renderTexture.view(), _pDevice );
            view._outputPath     = request._renderTexture;
        }
        if ( bRenderTexture )
        {
            if ( view._pOutputTexture == nullptr || view._pOutputTexture->isRHIValid() == false )
            {
                view._bRenderThisFrame = SW_FALSE;
                return;
            }
            view._outputWidth  = view._pOutputTexture->getWidth();
            view._outputHeight = view._pOutputTexture->getHeight();
        }

        // 컬링 · 정렬 상수버퍼는 뷰마다 자기 것이다(나눠 쓰면 뒤 업로드가 앞 디스패치를 덮어쓴다). 그림자 패스의 컬링도 뷰마다 자기 슬롯 · 상수버퍼다
        // (정렬은 그림자에 필요 없다 — 깊이만 쓴다).
        const bool bCullCb                     = view._cullInput._cullCb.isValid() || view._cullInput._cullCb.create( _pDevice, sizeof( FrameRendererUtil::GPUCullParams ) );
        const bool bSortCb                     = view._cullInput._sortCb.isValid() || view._cullInput._sortCb.create( _pDevice, sizeof( FrameRendererUtil::GPUSortParams ) );
        const bool bShadowCullCb               = view._shadowCullInput._cullCb.isValid() || view._shadowCullInput._cullCb.create( _pDevice, sizeof( FrameRendererUtil::GPUCullParams ) );
        const bool bConstantBufferCreateFailed = bCullCb == false || bSortCb == false || bShadowCullCb == false;
        if ( bConstantBufferCreateFailed )
        {
            SW_LOG_ERROR( "Failed to create the cull / sort constant buffers of extra view %# - it is not drawn", view._viewID );
            view._bRenderThisFrame = SW_FALSE;
            return;
        }
        view._cullInput.setViewProjection( request._viewProj );
        view._cullInput._position = request._position;
        if ( view._bRenderThisFrame == SW_FALSE )
            return;
        // 방향광 그림자는 이 뷰의 카메라에 맞춘 것이다 — 그리는 뷰는 게임 스레드가 늘 채운다(그림자 빛이 없으면 그림자 없는 뷰, `ShadowSetupUtil::makeViewShadowFor`).
        // 비었으면 주 시점 것을 빌리지 않는다(다른 곳을 보는 뷰는 그림자가 빠진다) — 이 뷰를 그리지 않는다.
        if ( request._shadow._bValid == SW_FALSE )
        {
            SW_LOG_ERROR( "Extra view %# has no shadow data of its own - it is not drawn", view._viewID );
            view._bRenderThisFrame = SW_FALSE;
            return;
        }
        view._shadow = request._shadow;
        view._shadowCullInput.setViewProjection( view._shadow._cullViewProj );
        view._shadowCullInput._position = request._position;
        // 정수 업스케일(화면 사각형 뷰)이면 사각형 픽셀 / 배율(내림)이다. 렌더 텍스처 · 호스트 타깃은 자기 크기에 그대로 그린다.
        const float32 scale  = MathUtil::clamp( view._settings._resolutionScale, 0.1f, 2.0f );
        const uint32  zoom   = view._outputKind == RenderViewOutputKind::ScreenRect ? view._settings._upscaleZoom : 0u;
        const uint32  width  = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( view._outputWidth ), scale, zoom );
        const uint32  height = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( view._outputHeight ), scale, zoom );
        (void)_viewResources.updateViewTransients( view, width, height );
        // 기록을 버릴지는 컬링(가림 판정)이 기록 전에 보므로 그리기(renderExtraView)가 아니라 여기서 정한다.
        beginViewHistory( view );
        // 이 뷰가 기록할 풀스크린 패스를 고르고 도는 패스의 타깃을 만든다 — 텍스처는 기록 중에 만들 수 없어 셋업에서 한다. 지난 뷰-투영은 그리기(applyViewProjection)가 바꾼다.
        (void)_viewResources.applyViewPassSelection( view, request._viewProj ); // 추가 뷰의 첨부는 렌더 타깃 목록에 싣지 않는다
        _upscale.prepareUpscale( view, view._outputWidth, view._outputHeight );
    }

    void FrameRenderer::releaseExtraView( FrameViewTarget& view )
    {
        _viewResources.releaseViewTransients( view );
        view._cullInput._cullCb.release( _pDevice );
        view._cullInput._sortCb.release( _pDevice );
        view._shadowCullInput._cullCb.release( _pDevice );
        releaseViewTransparentOrder( view );
        view._commandList.reset();
        if ( view._pOutputTexture != nullptr && engine::areEngineServicesBound() )
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().release( view._outputPath.view(), _pDevice );
        view._pOutputTexture = nullptr;
        view._outputPath     = hashed_string{};
    }

    void FrameRenderer::applyViewTransparentOrder( FrameViewTarget& view )
    {
        view._bHasTransparentRank = SW_FALSE;
        view._bUsesViewSlotStream = SW_FALSE;
        view._listTransparentBatchOrder.clear();
        view._listParticleSlot.clear();
        view._bHasParticleOrder               = SW_FALSE;
        const GPUViewTransparentOrder* pOrder = _state._gpuScene.findViewTransparentOrder( view._viewID );
        // 배치 순서와 입자 자리는 한 쌍이다 — 입자 자리(`_drawPosition`)는 그 순서 안의 번호라, 뷰의 순서에 주 시점의 자리를 섞으면 입자가 엉뚱한 배치 사이에 끼인다.
        // 그래서 둘 다 이 뷰의 것을 쓰거나 둘 다 주 시점의 것을 쓴다. 빌더는 입자마다 자리를 싣는다(`computeViewTransparentOrders`) — 수가 다르면 계약 위반이다.
        const bool bParticleSlotsComplete = pOrder != nullptr && pOrder->_listParticleSlot.size() == _state._gpuScene.getParticleBatches().size();
        SW_ASSERT( pOrder == nullptr || bParticleSlotsComplete );
        const bool bUsable = bParticleSlotsComplete && pOrder->_pListRank != nullptr && pOrder->_pListTailSlot != nullptr &&
                             pOrder->_listBatchOrder.size() == _state._gpuScene.getTransparentBatches().size() &&
                             pOrder->_tailBase + pOrder->_pListRank->size() == _state._gpuScene.getInstances().size();
        // 메시 투명이 없으면 입자 자리만 이 뷰의 것이다(배치 순서가 비어 짝이 될 것이 없다).
        const bool bParticleOrderOnly  = bParticleSlotsComplete && pOrder->_listBatchOrder.empty() && _state._gpuScene.getTransparentBatches().empty();
        const bool bParticleSlotsMatch = bUsable || bParticleOrderOnly;
        if ( bParticleSlotsMatch )
        {
            view._listParticleSlot  = pOrder->_listParticleSlot;
            view._bHasParticleOrder = SW_TRUE;
        }
        if ( bUsable == false )
            return;
        view._listTransparentBatchOrder = pOrder->_listBatchOrder;
        view._transparentTailBase       = pOrder->_tailBase;
        const uint32 tailCount          = static_cast<uint32>( pOrder->_pListRank->size() );
        if ( _state._gpuScene.areIndirectCountsGPUFilled() )
        {
            // GPU 정렬이 순번으로 되돌린다(instance_sort.hlsl). 표는 뷰마다 자기 것 — 나눠 쓰면 뒤 업로드가 앞 디스패치를 덮는다(정렬 CB 와 같은 함정).
            if ( view._transparentRank.ensureCapacity( _pDevice, sizeof( uint32 ), tailCount, RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
                return;
            view._transparentRank.upload( _pDevice, pOrder->_pListRank->data(), tailCount * static_cast<uint32>( sizeof( uint32 ) ) );
            view._bHasTransparentRank = SW_TRUE;
            return;
        }
        // 컬링이 없는 백엔드는 인스턴스 슬롯 스트림이 곧 그리는 순서다. 앞(불투명)은 항등, 꼬리는 배치 안을 뷰 순서로 다시 놓는다.
        // 내용이 지난 프레임과 같으면 버퍼를 그대로 쓴다(뷰 카메라 · 투명 물체가 서 있으면 순서가 같다).
        const uint32 instanceCount = pOrder->_tailBase + tailCount;
        const bool   bSameContent  = view._instanceSlotStream != 0 && view._listInstanceSlot.size() == instanceCount &&
                                  Memory::compare( view._listInstanceSlot.data() + pOrder->_tailBase, pOrder->_pListTailSlot->data(), tailCount * sizeof( uint32 ) ) == 0;
        if ( bSameContent == false )
        {
            view._listInstanceSlot.resize( instanceCount );
            for ( uint32 slot = 0; slot < pOrder->_tailBase; ++slot )
            {
                view._listInstanceSlot[slot] = slot;
            }
            Memory::copy( view._listInstanceSlot.data() + pOrder->_tailBase, pOrder->_pListTailSlot->data(), tailCount * sizeof( uint32 ) );
            // 지난 프레임 기록이 이 버퍼를 읽었을 수 있다 — 반환은 디바이스가 미룬다(releaseHandle).
            if ( view._instanceSlotStream != 0 )
                _pDevice->releaseHandle( RHIHandleKind::Buffer, view._instanceSlotStream );
            view._instanceSlotStream = _pDevice->getResourceFactory()->createVertexBuffer( view._listInstanceSlot.data(), instanceCount * static_cast<uint32>( sizeof( uint32 ) ) );
            if ( view._instanceSlotStream == 0 )
            {
                view._listInstanceSlot.clear();
                return;
            }
        }
        view._bUsesViewSlotStream = SW_TRUE;
    }

    void FrameRenderer::releaseViewTransparentOrder( FrameViewTarget& view )
    {
        view._transparentRank.release( _pDevice );
        if ( view._instanceSlotStream != 0 && _pDevice != nullptr )
            _pDevice->releaseHandle( RHIHandleKind::Buffer, view._instanceSlotStream );
        view._instanceSlotStream = 0;
        view._listInstanceSlot.clear();
        view._listTransparentBatchOrder.clear();
        view._listParticleSlot.clear();
        view._bHasParticleOrder   = SW_FALSE;
        view._bHasTransparentRank = SW_FALSE;
        view._bUsesViewSlotStream = SW_FALSE;
    }

    uint32 FrameRenderer::collectFrameViewModeMask() const
    {
        // 이번 프레임에 만들 변형은 (패스, 퍼뮤테이션, 뷰 모드)로 정해진다. 보기 모드는 뷰마다의 설정이라 이번 프레임에 그리는 뷰들의 모드를 모은다.
        // 뷰 설정은 프레임 시작에 정해지고 기록 중에 바뀌지 않으므로, 드로우가 고르는 PSO 는 여기서 준비한 집합 안에 있다.
        // 겹침 모드(ShadedWireframe)는 배치를 Lit 으로 한 번 더 그리므로 Lit 변형도 모은다.
        auto viewModeBits = []( RenderViewMode viewMode ) -> uint32
        {
            const uint32 bit = 1u << static_cast<uint32>( getRenderViewModePipelineMode( viewMode ) );
            return getRenderViewModeInfo( viewMode )._bOverlay ? ( bit | ( 1u << static_cast<uint32>( RenderViewMode::Lit ) ) ) : bit;
        };
        uint32 viewModeMask = viewModeBits( _state._mainView._settings._viewMode );
        for ( const unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                viewModeMask |= viewModeBits( pView->_settings._viewMode );
        }
        return viewModeMask;
    }

    bool FrameRenderer::supportsViewMode( RenderViewMode viewMode ) const
    {
        // 와이어프레임 채우기가 없는 디바이스(Vulkan fillModeNonSolid 없음)는 와이어프레임 모드를 받지 않는다 — 솔리드 그림이 와이어프레임으로 통과하지 않게.
        const bool bNeedsWireframe = getRenderViewModeInfo( viewMode )._bWireframe;
        const bool bDrawable       = bNeedsWireframe == false || _pDevice == nullptr || _pDevice->getCapabilities()._bWireframeFill != SW_FALSE;
        if ( bDrawable )
            return true;
        SW_LOG_ERROR( "View mode %# needs wireframe fill, which this device does not support - the mode is not applied", getRenderViewModeInfo( viewMode )._pName );
        return false;
    }

    void FrameRenderer::setViewMode( RenderViewMode viewMode )
    {
        // 범위 밖 값을 Lit 으로 바꾸지 않는다 — 들어오는 번호는 부르는 쪽이 거른다(isRenderViewModeIndex).
        if ( viewMode >= RenderViewMode::Count )
        {
            SW_LOG_ERROR( "setViewMode: %# is not a view mode - the main output keeps its mode", static_cast<uint32>( viewMode ) );
            SW_ASSERT( viewMode < RenderViewMode::Count );
            return;
        }
        if ( supportsViewMode( viewMode ) == false )
            return;
        const uint8 previous = _viewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;

        // 바뀔 때만 남긴다. 뷰 모드는 화면 전체를 바꾸는 상태다. 로그가 없으면 "왜 와이어프레임인가" 를 화면만 보고 되짚어야 한다.
        SW_LOG_INFO( "Main output view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getViewMode() const
    {
        // setViewMode 가 범위 밖 값을 받지 않으므로 그대로 바꾼다.
        return static_cast<RenderViewMode>( _viewMode.load( std::memory_order_relaxed ) );
    }

    void FrameRenderer::setSceneViewMode( RenderViewMode viewMode )
    {
        if ( viewMode >= RenderViewMode::Count )
        {
            SW_LOG_ERROR( "setSceneViewMode: %# is not a view mode - the scene view keeps its mode", static_cast<uint32>( viewMode ) );
            SW_ASSERT( viewMode < RenderViewMode::Count );
            return;
        }
        if ( supportsViewMode( viewMode ) == false )
            return;
        const uint8 previous = _sceneViewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;
        SW_LOG_INFO( "Scene view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getSceneViewMode() const
    {
        return static_cast<RenderViewMode>( _sceneViewMode.load( std::memory_order_relaxed ) );
    }

    bool FrameRenderer::executeScenePreviewPacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        const bool bCanRender      = isReady() && pDevice != nullptr && packet._bValid == SW_TRUE;
        const bool bIsPreviewFrame = packet._sceneKey != 0 && packet._outputRenderTarget != 0;
        if ( bCanRender == false || bIsPreviewFrame == false )
            return false;

        FrameRenderer* pRenderer = nullptr;
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            if ( preview._sceneKey == packet._sceneKey )
                pRenderer = preview._renderer.get();
        }
        if ( pRenderer == nullptr )
        {
            // 주 렌더러와 같은 파이프라인과 작업 관리자로 만든다. 계측과 공개를 끄는 표시와 PSO 캐시는 initialize 앞에 둔다(파이프라인 로드가 공개를 부른다).
            ScenePreviewRenderer created;
            created._renderer                 = make_unique<FrameRenderer>();
            created._renderer->_bScenePreview = SW_TRUE;
            created._renderer->_psoManager.shareCacheOf( _psoManager );
            created._sceneKey = packet._sceneKey;
            if ( created._renderer->initialize( pDevice, _state._pTaskManager, _pipelinePath ) == false )
            {
                SW_LOG_ERROR( "Scene preview renderer for scene key %# could not start: %#", packet._sceneKey, created._renderer->getStatusMessage() );
                return false;
            }
            SW_LOG_INFO( "Scene preview renderer started for scene key %#", packet._sceneKey );
            pRenderer = created._renderer.get();
            _listScenePreviewRenderer.push_back( std::move( created ) );
            _scenePreviewRendererCount.store( static_cast<uint32>( _listScenePreviewRenderer.size() ) );
        }
        // 보기 모드는 패킷의 뷰 설정이 정한다(`HostSceneView::_settings`).
        return pRenderer->executePacket( pDevice, packet );
    }

    void FrameRenderer::retireScenePreviews( const RenderFramePacket& mainPacket )
    {
        for ( size_t index = _listScenePreviewRenderer.size(); index > 0; --index )
        {
            ScenePreviewRenderer& preview = _listScenePreviewRenderer[index - 1];
            bool                  bAlive  = false;
            for ( uint32 previewIndex = 0; previewIndex < mainPacket._scenePreviewCount; ++previewIndex )
            {
                bAlive = bAlive || mainPacket._listScenePreview[previewIndex]->_sceneKey == preview._sceneKey;
            }
            if ( bAlive )
                continue;
            // 디바이스의 해제는 GPU 완료 뒤로 미뤄진다. 이미 낸 프레임이 쓰는 자원을 지금 파괴하지 않는다(파일 머리 주석).
            preview._renderer->shutdown();
            _listScenePreviewRenderer.erase( _listScenePreviewRenderer.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        _scenePreviewRendererCount.store( static_cast<uint32>( _listScenePreviewRenderer.size() ) );
    }
} // namespace sw
