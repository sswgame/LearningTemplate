/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML 을 로드해 RenderGraph 를 만들고, 프레임을 셋업한 뒤 패스 실행(`FramePassExecutor`)에 넘겨 Shadow, Forward, Deferred, Post 패스를 그립니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"
#include "Engine/Renderer/Frame/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostProcessMaterials.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/FrameResourceRegistry.h"
#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"
#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Frame/View/FrameViewResources.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/Light2DCookieAtlas.h"
#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUScene.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"

namespace sw
{
    struct FogSettings;
    struct RenderFramePacket;
    struct RenderPassTypeInfo;
    struct SkyGradient;

    class CameraComponent;
    class IRHICommandList;
    class IRHIDevice;
    class Material;
    class Mesh;
    class RenderExtensionRegistry;
    class FrameProfiler;
    class MaterialCache;
    class MaterialInstance;
    class RenderTargetRegistry;
    class Scene;
    class ShaderBindingLayout;
    class TaskArgs;
    class TaskManager;
    class Texture2D;

    /** @brief FrameRenderer 의 초기화와 파이프라인 상태를 나타냅니다. */
    enum class FrameRendererStatus : uint8
    {
        Uninitialized = 0, ///< initialize 를 아직 부르지 않았다.
        Ready,             ///< 파이프라인을 로드했고 그래프가 준비되었다.
        Failed             ///< initialize 나 loadPipeline 이 실패했다(getStatusMessage 참고).
    };

} // namespace sw

namespace sw
{
    /**
     * @struct FrameRendererHost
     * @brief 주 렌더러만 하는 일의 상대입니다 — 호스트(`FrameRendererSubsystem`)가 엔진 서비스로 채워 `FrameRenderer::bindHost` 로 넘긴다.
     * @details 화면 밖 씬 렌더러(`OffscreenSceneRenderers`) · 도구 · 테스트의 렌더러는 비어 있다. 예전에는 렌더러가 스스로 전역에서 찾고 역할 표시(`_bScenePreview`)로
     *          다섯 곳을 갈랐다 — 표시를 빠뜨린 자리(트랜지언트를 놓을 때 렌더 타깃 목록 비우기)는 화면 밖 렌더러가 내려갈 때 주 렌더러의 목록을 지웠다.
     */
    struct FrameRendererHost
    {
        FrameProfiler*           _pProfiler{ nullptr };        ///< 패스별 GPU 시간 보고(타임스탬프 슬롯은 디바이스에 하나 — 렌더러 하나만 계측한다)
        RenderExtensionRegistry* _pExtensions{ nullptr };      ///< 모듈 렌더 확장(주 파이프라인에 붙는다)
        RenderTargetRegistry*    _pRenderTargets{ nullptr };   ///< 렌더 타깃 패널의 목록(주 시점의 첨부)
        MaterialCache*           _pPinnedMaterials{ nullptr }; ///< 고정 머티리얼의 PSO 미리 만들기 줄(줄은 하나 — 렌더러 하나만 꺼낸다)
    };

    /**
     * @class FrameRenderer
     * @brief 프레임 경로를 담당합니다. RenderPipeline XML 에서 RenderGraph 를 만들고, GPUScene 과 MeshComponent 를 IRHIDevice 로 그립니다.
     * @details **뷰는 여럿일 수 있다.** 주 시점 하나와 추가 뷰를 같은 그래프로 한 번씩 그린다.
     *          추가 뷰(`RenderViewRequest`)에는 CCTV, 백미러, 미니맵 같은 렌더 텍스처 뷰와 분할 화면, PiP 같은 화면 사각형 뷰가 있다.
     *          뷰마다 트랜지언트 풀, 컬링 슬롯, TAA 히스토리, 커맨드 리스트를 따로 가진다(`FrameViewTarget`).
     *          컬링 슬롯은 간접 인자, 가시 목록, 컬링 상수 버퍼로 이루어진다.
     *          실행 순서는 프레임 그래프(모든 뷰의 컬링), 렌더 텍스처 뷰, 주 시점, 화면 사각형 뷰다.
     *          주 시점이 렌더 텍스처를 읽으므로 렌더 텍스처 뷰가 먼저 돌고, 화면 사각형 뷰는 주 시점 위에 겹친다.
     *
     *          **얇은 지휘자.** 렌더러는 프레임 순서(스냅샷 받기 · 라이트 · 그림자 · 하늘 · 뷰 준비 · 업로드 · PSO 준비 · 제출)를 정하고 프레임 데이터(`FrameRenderState`)를 소유한다.
     *          혼자 서는 일은 소유한 협력 객체가 한다 — 기록과 제출(`FramePassExecutor`), PSO 만들기 · 찾기(`FramePSOManager`), 후처리 효과 셋(`FrameAutoExposure` · `FrameUpscale` · `FrameFog`),
     *          모듈 확장 받아들이기(`FrameExtensionHost`), 뷰 GPU 자원 수명(`FrameViewResources`), 캡처(`FrameCapture`).
     *          협력 객체는 지금 그리는 뷰를 인자로 받고 렌더러의 `_pDevice` 변수를 참조한다. 언리얼 `FSceneRenderer` 의 셋업(InitViews)과 패스 기록(RDG 패스 실행)을 나눈 것과 같다.
     */
    class SW_API FrameRenderer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명: 디바이스, 파이프라인 XML, 핫패스 서비스
        // ------------------------------------------------------------------------------
        /** @brief 빈 렌더러로 만듭니다. initialize 를 부르기 전에는 쓸 수 없습니다. */
        FrameRenderer();
        /** @brief GPU 자원을 해제합니다. */
        ~FrameRenderer();

        /** @brief 복사를 금지합니다. */
        FrameRenderer( const FrameRenderer& ) = delete;
        /** @brief 대입을 금지합니다. */
        FrameRenderer& operator=( const FrameRenderer& ) = delete;

        /** @brief 디바이스와 파이프라인 XML 로 초기화합니다. */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice, string_view pipelineXMLPath = {} );
        /** @brief 디바이스, TaskManager, 파이프라인 XML 로 초기화합니다. */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                                       string_view pipelineXMLPath = {} );
        /** @brief 주 렌더러만 하는 일의 상대를 겁니다(`FrameRendererHost`). `initialize` 앞에 부릅니다(파이프라인 로드가 렌더 타깃을 공개한다). */
        void bindHost( const FrameRendererHost& host ) { _host = host; }
        /**
         * @brief 화면 밖 씬(미리보기 · 캡처) 하나를 그리는 렌더러로 초기화합니다(`OffscreenSceneRenderers` 가 만든다).
         * @details @p mainRenderer 의 TaskManager 를 쓰고, @p pipelinePath 가 비면 그 파이프라인과 PSO 저장소를 함께 쓴다(같은 파이프라인이면 PSO 가 같다).
         *          호스트(`FrameRendererHost`)는 걸지 않는다 — 프로파일러 보고 · 렌더 타깃 목록 · 모듈 확장 · 고정 PSO 미리 만들기는 주 렌더러의 일이다.
         */
        [[nodiscard]] bool initializeOffscreen( IRHIDevice* pDevice, FrameRenderer& mainRenderer, string_view pipelinePath );
        /** @brief 핫패스에서 쓰는 TaskManager 를 연결합니다. */
        void bindServices( TaskManager* pTaskManager );
        /** @brief GPU 자원을 해제하고 종료합니다. */
        void shutdown();

        // ------------------------------------------------------------------------------
        // 2) 파이프라인 · 실행: XML 로드, execute / executePacket
        // ------------------------------------------------------------------------------
        /** @brief RenderPipeline XML 로 그래프를 다시 만듭니다(동기 로드). 패스 콜백은 한 번만 바인딩합니다. */
        [[nodiscard]] bool loadPipeline( const AssetID& assetID );
        /**
         * @brief 파이프라인을 고르는 순서(초기화 인자 > `gv_renderPipeline` > `gv_deferredShading` > 팩 설정 > 기본 포워드)로 다시 골라 불러옵니다.
         * @details 초기화가 부르고, 실행 중에는 두 변수가 바뀔 때 렌더 스레드를 재운 뒤 부른다(`FrameRendererSubsystem`) — 언리얼 `r.*` 렌더 설정을 실행 중에 바꾸는 것과 같다.
         */
        [[nodiscard]] bool selectPipeline();
        /**
         * @brief 렌더 스레드 경로입니다. 미리 만들어 둔 packet 의 GPUScene 을 쓰며 Scene 에는 접근하지 않습니다.
         * @details 주 시점이 없는 패킷(`RenderFramePacket::_bHasViewProj` 가 0 — 활성 씬이 없거나 씬에 카메라가 없다)은 그리지 않고 false 입니다.
         *          3D 내용을 실은 채 주 시점이 없으면 오류로 한 번 알립니다. 주 시점은 `RenderFramePacket::setMainView` 로 싣습니다.
         */
        [[nodiscard]] bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );
        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        /** @brief Ready 상태면 true 입니다. */
        bool isReady() const { return _status == FrameRendererStatus::Ready; }

        /** @brief 마지막 프레임에서 씬이 낸 간접 드로우 호출 수입니다(모든 패스 합). 배치 수보다 작으면 묶인 호출이 있다는 뜻입니다. */
        uint32 getLastIndirectDrawCallCount() const { return _lastIndirectDrawCallCount; }
        /** @brief 마지막 프레임에서 머티리얼 PSO 가 아직 만드는 중이라 그리지 못한 배치 묶음 수입니다. 캡처는 0 인 프레임만 읽는다(`SceneCapture`). */
        uint32 getLastPendingPSOBatchCount() const { return _lastPendingPSOBatchCount; }
        /** @brief 마지막 프레임의 볼륨 안개 상태(켜졌거나 왜 꺼졌는지)입니다. 렌더 스레드가 주 렌더러의 값을 올린다(`FrameFog::publishMainVolumeState`). */
        FogVolumeState getFogVolumeState() const { return _fog.getVolumeState(); }
        /**
         * @brief 풀스크린 패스가 이 역할의 입력을 바인딩하지 **않게** 합니다. 언리얼의 r.AmbientOcclusion.Levels=0 같은 쇼 플래그입니다.
         * @details 셰이더는 그 인덱스를 kInvalidIndex 로 읽고 폴백한다(AO 는 1). 같은 프레임 안에서 "이 입력이 실제로 이미지를 바꾸는가" 를 비교할 수 있다.
         *          입력을 매 프레임 계산하기만 하고 화면에는 쓰지 않는 결함을 픽셀로 잡는다. 바인딩은 패스 실행(`FramePassExecutor`)이 한다.
         */
        void setInputRoleEnabled( RenderPassInputRole role, bool bEnabled ) { _passExecutor.setInputRoleEnabled( role, bEnabled ); }
        /** @brief 초기화와 파이프라인 상태를 반환합니다. */
        FrameRendererStatus getStatus() const { return _status; }
        /** @brief Failed 일 때의 원인 메시지입니다(그 밖에는 비어 있습니다). */
        const string& getStatusMessage() const { return _statusMessage; }
        /** @brief 지금 그리는 파이프라인 XML 입니다(불러오기 전이면 빈 글). */
        const string& getPipelinePath() const { return _pipelinePath; }
        /** @brief 렌더 그래프를 반환합니다. */
        const RenderGraph& getGraph() const { return _state._graph; }
        /** @brief GPUScene 을 반환합니다. */
        const GPUScene& getGPUScene() const { return _state._gpuScene; }
        GPUScene&       getGPUScene() { return _state._gpuScene; }
        /** @brief 셰이더 핫 리로드를 알립니다(렌더 스레드, 프레임 기록 전 — `RenderThread::applyRecompiledShaders`). 그 셰이더들의 바인딩 레이아웃을 무효화하고 패스 PSO 를 한 번 다시 만듭니다. */
        void onShaderRecompiled( const vector<string>& listShaderPath );
        /**
         * @brief 이 렌더러의 캡처 · 되읽기입니다. Present 결과 캡처(`-gv_screenshot`, 테스트의 최종 화면 비교), 트랜지언트 첨부와 임의 텍스처를 CPU 로 읽거나 파일로 씁니다.
         * @details 읽기는 GPU 를 기다리므로 프레임 경로에서 부르지 않는다.
         */
        FrameCapture&       getCapture() { return _capture; }
        const FrameCapture& getCapture() const { return _capture; }
        /**
         * @brief 다음 프레임부터 그릴 캔버스(화면 2D)를 받습니다. 렌더러가 가진 것과 맞바꾸므로 버퍼 용량은 서로 넘어갑니다.
         * @details 패킷 경로에서는 `executePacket` 이 패킷의 `_canvas` 로 같은 일을 한다. 씬 직접 경로(에디터, 테스트)는 이 함수로 넣으며, 바꾸기 전까지 매 프레임
         *          그 목록을 그린다. 아틀라스 업로드는 한 번만 반영하고 반영한 뒤에는 비운다.
         */
        void setCanvasFrame( CanvasFrameData& inoutFrame );
        /** @brief 패킷의 글리프 아틀라스 업로드를 캔버스 거울에 옮깁니다. 그리지 않을 패킷도 받은 즉시 부릅니다(`CanvasRenderer::acceptAtlasUploads`). */
        void acceptCanvasAtlasUploads( CanvasFrameData& inoutFrame ) { _state._canvasRenderer.acceptAtlasUploads( inoutFrame ); }
        /** @brief 캔버스(화면 2D · 렌더 텍스처 대상) 렌더러입니다. 다시 그린 대상 수 같은 진단 값을 읽을 때 씁니다. */
        const CanvasRenderer& getCanvasRenderer() const { return _state._canvasRenderer; }

        /**
         * @brief 화면에 나간 첨부의 이름입니다. Present 패스가 입력으로 받는 첨부이며, 없으면 빈 문자열입니다.
         * @details 저장하려는 것은 항상 "지금 보이는 이미지" 이므로 파이프라인에 물어본다.
         *          주의: `"SceneColor"` 같은 리터럴을 직접 쓰면, 그 이름이 없는 파이프라인(디퍼드)에서는 읽기 실패 로그만 남고 파일이 만들어지지 않는다.
         */
        string_view getPresentedAttachmentName() const;
        /**
         * @brief 그림자 맵 한 변의 텍셀 수(`gv_shadowMapResolution`)입니다. 품질 단계마다의 값은 설정 스키마(`engine.settings.xml` 의 `graphics.shadowQuality`)가 정합니다.
         * @details 그림자 맵 크기는 화면 크기를 따르지 않는다. 정사각 볼륨을 1280×720 에 담으면 텍셀이 한 축으로 1.8배 늘어나고,
         *          출력이 작은 에디터 게임 뷰에서는 그림자 맵도 같이 작아지기 때문이다. 게임 스레드(그림자 행렬의 텍셀 스냅)와 렌더 스레드(첨부 할당)가 같은 값을 쓴다.
         */
        static uint32 getShadowMapResolution();
        /**
         * @brief 지금 살아 있는 트랜지언트 목록을 엔진 레지스트리에 공개합니다. 에디터 패널이 이 목록을 읽습니다.
         * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어지므로 그때 한 번만 부르면 된다(풀을 다시 만들 때, 꺼진 효과의 출력이 가리키는 곳이 바뀔 때).
         *          매 프레임 부를 필요는 없다.
         */
        void publishRenderTargets() const;
        /** @brief 주 시점 첨부마다의 크기(KB)를 Info 로그(`[RenderTargets]`)로 남깁니다. GPU 메모리가 늘었을 때 어느 첨부인지 나눈다. 풀을 다시 만들거나 첨부를 더 만들 때만 부릅니다. */
        void reportRenderTargetMemory() const;

        /**
         * @brief 주 출력(게임 창, 에디터 게임 뷰)의 보기 모드를 정합니다. `-gv_viewMode` 와 테스트가 씁니다.
         * @details 보기 모드는 뷰마다 갖는 설정(`RenderViewSettings::_viewMode`)이다. 이 값은 주 출력 뷰의 설정에 들어가고,
         *          에디터 씬 뷰는 `setSceneViewMode` 의 값을 쓴다. 다음 프레임의 `FramePSOManager::ensureMaterialPSOs` 가 그 프레임 뷰들의 모드로
         *          PSO 변형을 만들고, 드로우는 지금 그리는 뷰의 모드로 변형을 고른다. 모드를 바꾼 프레임에는 셰이더 컴파일이 한 번 끼어든다.
         */
        void setViewMode( RenderViewMode viewMode );
        /** @brief 이 디바이스가 그 보기 모드를 그릴 수 있는지 봅니다(와이어프레임 채우기 능력). 못 그리면 오류를 남기고 false 입니다. 디바이스가 없으면 true 입니다. */
        [[nodiscard]] bool supportsViewMode( RenderViewMode viewMode ) const;
        /** @brief 주 출력의 보기 모드입니다. */
        RenderViewMode getViewMode() const;
        /**
         * @brief 에디터 씬 뷰의 보기 모드를 정합니다(씬 뷰 툴바). 게임 뷰에는 적용되지 않습니다.
         * @details 씬 뷰가 호스트 타깃을 쓰는 추가 뷰이든, 게임 뷰가 가려져 주 출력이 되었든 이 값은 씬 뷰의 설정에 들어간다(`Engine::update`).
         */
        void setSceneViewMode( RenderViewMode viewMode );
        /** @brief 에디터 씬 뷰의 보기 모드입니다. */
        RenderViewMode getSceneViewMode() const;
        /** @brief 이 렌더러가 그리는 디바이스입니다. 초기화 전이면 nullptr 이며, 백엔드 이름 같은 정보를 읽을 때 씁니다. */
        IRHIDevice* getDevice() const { return _pDevice; }
        /**
         * @brief 주 시점의 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다. 어느 스레드에서 불러도 됩니다.
         * @details `gv_occlusionCulling` 이 꺼졌거나 사본이 아직 없거나 기록을 버린 프레임(처음 · 컷) 뒤면 nullptr — 부르는 쪽은 가림을 보지 않는다.
         *          `RenderViewCollector` 가 모니터의 "지난 프레임에 그려졌나" 판정에 쓴다(`OcclusionTileSnapshot::isSphereOccluded`).
         */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _passExecutor.getOcclusionSnapshot(); }

        /** @brief 패스 타입에 해당하는 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle getEnginePSO( RenderPassType passType ) const;
        /**
         * @brief 이 패스가 이 배치를 그리는지 반환합니다. 패스가 머티리얼 define 으로 배치를 거르면(메시 외곽선) 퍼뮤테이션에 그 define 이 있어야 합니다.
         * @details 판정은 `FrameRendererUtil::drawsMaterialInPass` 하나로 한다. 머티리얼 PSO 변형과 셰이더 쿠커도 같은 판정을 쓴다. 드로우 루프(`FramePassExecutor`)와 같은 함수다.
         */
        bool drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch, const GPUMeshBatchBinding& binding ) const
        {
            return _passExecutor.drawsBatchInPass( passType, batch, binding );
        }
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터(셰이더 경로, define, 렌더 상태)를 반환합니다. 모르는 PSO 면 false 입니다.
         * @details 어떤 퍼뮤테이션이 실제로 적용되었는지 밖에서 확인할 수 있는 유일한 방법이다.
         *          픽셀로는 드러나지 않는 차이(알파 경로가 컴파일되었는가 등)를 테스트가 여기서 확인한다.
         */
        [[nodiscard]] bool findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 주 시점의 TAA 히스토리 텍스처입니다. 파이프라인에 TAA 패스가 없거나 아직 만들지 않았으면 0 입니다(진단과 테스트용). */
        RHITextureHandle getTAAHistory() const { return _state._mainView._taaHistory; }
        /** @brief 주 시점의 첨부 풀입니다. 어느 타깃이 있고 몇 번 만들었는지 보는 진단 · 테스트용입니다(고치지 않는다). */
        const TransientAttachmentPool& getMainViewPool() const { return _state._mainView._transientPool; }
        /** @brief 마지막 프레임에 실제로 그린 추가 뷰 수입니다. 갱신 주기나 예산 때문에 쉰 뷰는 빠집니다(진단과 테스트용). */
        uint32 getLastRenderedExtraViewCount() const { return _passExecutor.getLastRenderedExtraViewCount(); }
        /** @brief 가지고 있는 추가 뷰 수입니다. 쉬는 뷰도 포함합니다. */
        uint32 getExtraViewCount() const { return static_cast<uint32>( _state._listExtraView.size() ); }

    private:
        /** @brief 컴퓨트 디스패치 상수 버퍼 한 항목입니다. 생성(`ensurePassResources`)과 해제(`releasePassResources`)가 같은 테이블을 돕니다. */
        struct ComputeConstantBufferRow
        {
            RHIConstantBuffer* _pSlot{ nullptr };
            uint32             _byteSize{ 0 };
            const utf8*        _pUsage{ nullptr };
        };
        /// @brief 컴퓨트 상수 버퍼 수입니다. 고정 뷰마다 컬링과 정렬이 하나씩 있고, 인스턴스 애니메이션, 메시 모프, 메시 스킨이 하나씩 더 있습니다. 추가 뷰의 것은 그 뷰가 가집니다.
        static constexpr uint32 _s_kComputeConstantBufferCount = static_cast<uint32>( RenderViewType::Count ) * 2 + 3;
        /// @brief 배치 하나가 한 프레임에 몇 개의 지오메트리 패스(그림자, 프리패스, 불투명, 반투명)에서 그려지는지의 어림값입니다.
        static constexpr uint32 _s_kDrawCbPassEstimate = 4;
        // ------------------------------------------------------------------------------
        // 4) 셋업: 패스 자원, 트랜지언트, 추가 뷰, 프레임 상수(기록 전)
        // ------------------------------------------------------------------------------
        /** @brief 패스용 상주 GPU 자원을 확보합니다. 패스 CB 링, 컴퓨트 CB 테이블, 엔진 PSO, Present 변형, 머티리얼 폴백이 해당합니다. */
        void ensurePassResources();
        /**
         * @brief 패스용 상주 GPU 자원을 해제합니다. 디바이스가 없으면 각 자원이 핸들만 잊습니다.
         * @details 트랜지언트 크기를 따르는 자원(첨부, TAA 히스토리, Present 캡처)은 여기서 해제하지 않는다. 그것은 `releaseTransientResources` 의 몫이며,
         *          셰이더 핫 리로드처럼 패스 자원만 다시 만드는 경로에서는 이 자원이 다시 만들어지지 않기 때문이다.
         */
        void releasePassResources();
        /** @brief 컴퓨트 상수 버퍼 테이블을 채웁니다. 새 컴퓨트 상수 버퍼는 여기에 한 줄만 추가하면 됩니다. 생성과 해제를 따로 적지 않습니다. */
        void collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] );
        /** @brief 프레임을 시작할 때마다 패스 상수 슬롯 커서를 되감고 시드를 0번 슬롯에 맞춥니다. */
        void resetPassCbRing();
        /** @brief 주 시점의 트랜지언트 텍스처를 확보합니다. 크기는 출력 크기 × 사각형 × 해상도 배율이며, 출력 크기는 덮어쓴 크기가 있으면 그 값, 없으면 백버퍼입니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /** @brief 추가 뷰 하나를 통째로 해제합니다. 트랜지언트, 컬링 상수 버퍼, 리스트, 빌린 렌더 텍스처가 해당합니다. */
        void releaseExtraView( FrameViewTarget& view );
        /**
         * @brief 이번 프레임의 추가 뷰 요청을 뷰 상태에 반영합니다. 새 뷰를 만들고, 사라진 뷰를 해제하고, 출력 텍스처, 풀 크기, 컬링 입력을 맞추고,
         *        GPUScene 의 컬링 슬롯 수를 정합니다. **업로드 전에**(셋업 단계) 불러야 합니다. 버퍼와 텍스처 생성은 기록 중에 할 수 없습니다.
         */
        void prepareExtraViews( const vector<RenderViewRequest>& listRequest );
        /**
         * @brief 주 시점의 캐스케이드별 컬링 슬롯 수를 정하고 상수 버퍼를 맞춥니다(`-gv_shadowCascadeCulling=1` 이고 캐스케이드가 둘 이상일 때만 켭니다).
         * @return 슬롯 수(0 이면 쓰지 않음)
         */
        uint32 prepareCascadeCullInputs();
        /** @brief 이 카메라 id 의 추가 뷰입니다. 없으면 새로 만들어 목록 끝에 붙입니다. */
        FrameViewTarget& getOrCreateExtraView( uint64 viewID );
        /** @brief 요청 하나로 추가 뷰를 준비합니다(설정 · 출력 · 렌더 텍스처 빌리기 · 컬링 상수버퍼 · 트랜지언트). 준비하지 못하면 이번 프레임에 그리지 않습니다. */
        void prepareExtraView( const RenderViewRequest& request );
        /**
         * @brief 스냅샷에 든 이 뷰의 투명 순서를 뷰 자원(순번 테이블, 슬롯 스트림, 배치 순서)에 옮깁니다. 없으면 주 순서로 그립니다.
         * @details GPU 정렬 백엔드는 순번 테이블을 정렬 디스패치에 쓰고, 컬링이 없는 백엔드는 배치 안을 뷰 순서로 다시 배치한 슬롯 스트림을 드로우에 쓴다. 업로드 뒤, 기록 전에 부른다.
         */
        void applyViewTransparentOrder( FrameViewTarget& view );
        /** @brief 뷰 하나의 투명 순서 자원(순번 테이블, 슬롯 스트림)을 해제합니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseViewTransparentOrder( FrameViewTarget& view );
        /**
         * @brief 뷰의 이번 프레임 시간 누적 기록을 시작합니다 — 기록을 버릴지(`_bHistoryReset`: 이 풀로 처음 그리거나 컷)를 정하고 그렸다고 표시합니다.
         * @details 셋업에서 뷰마다 프레임에 한 번 부른다(주 시점은 `updatePassConstants`, 추가 뷰는 `prepareExtraView`). 컬링(가림)이 기록 전에 돌므로 그리기 때(`applyViewProjection`)가 아니라 여기서 정한다.
         */
        void beginViewHistory( FrameViewTarget& view );
        /** @brief 트랜지언트 텍스처를 해제합니다. */
        void releaseTransientResources();

        /**
         * @brief 패스 상수 값(PassConstantValues)을 채웁니다. 업로드와 바인딩은 ShaderParameterBinder 가 합니다.
         * @param mainViewProj 주 시점 카메라의 뷰-투영입니다. 카메라가 없는 프레임은 여기까지 오지 않습니다(기본 카메라로 대신 그리지 않는다).
         */
        void updatePassConstants( FramePassContext& context, const float4x4& mainViewProj );

        /** @brief 이번 프레임의 하늘(`_state._frameSky` · `_state._bHasFrameSky`)을 정합니다. 씬 직접 경로와 패킷 경로가 같은 규칙으로 부릅니다. */
        void applyFrameSky( bool bHasSky, const SkyGradient& sky );
        /**
         * @brief 실시간 라이트 프로브 갱신의 버퍼와 입력을 갖춥니다(`_state._listFrameRealtimeProbe`). **기록 전에** 부릅니다 - 라이트 버퍼가 올라간 뒤(프로브 버퍼의 UAV가 정해진 뒤)다.
         * @details 못 켜는 이유(컴퓨트 PSO · 프로브 버퍼 UAV · 버퍼 할당)는 갱신기가 한 번만 오류로 남기고, 실시간 볼륨은 검게 남는다(베이크로 대신하지 않는다).
         */
        void prepareRealtimeProbes( IRHIDevice* pDevice );
        /**
         * @brief 이번 프레임의 그림자 배치(`_state._frameShadow`)를 마무리합니다. 주 시점 그림자가 비어 있으면(게임 스레드가 배치를 만들지 않았다) 그림자 없는 뷰로 두고, 주 시점 뷰에 옮깁니다.
         * @details 라이트 버퍼 업로드보다 먼저 부른다 — 점광 · 스폿 타일 원소가 라이트 버퍼 꼬리에 붙는다.
         */
        void applyFrameShadow();

        // ------------------------------------------------------------------------------
        // 5) 업로드와 제출 — 기록은 패스 실행(`FramePassExecutor`)이 한다
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 뒷부분입니다. 스냅샷을 GPU 로 올리고, 기록 전에 준비해야 하는 것(콜백, 상수 버퍼 슬롯, 머티리얼 PSO)을 갖춘 뒤 그래프를 제출합니다.
         * @details 두 진입점이 이 단계를 각자 가지면 한쪽만 빠지기 쉽다. 예를 들어 시드를 채우는 순서가 갈리면 한쪽만 상수가 빠진다. 그래서 한 함수로 둔다.
         * @param pCallerName 오류 로그에 찍을 호출자 이름
         */
        [[nodiscard]] bool uploadSceneAndSubmit( IRHIDevice* pDevice, const utf8* pCallerName );
        /** @brief `uploadSceneAndSubmit` 의 몸체(모듈 구간 안)입니다. */
        [[nodiscard]] bool uploadSceneAndSubmitFrame( IRHIDevice* pDevice, const utf8* pCallerName );
        /**
         * @brief 라이트 버퍼를 올리고 뷰마다 라이트 그리드를 준비합니다. 업로드 전에, 추가 뷰가 정해진 뒤에 부릅니다(생산자는 `selectLightGridProducer`).
         * @details 라이트와 그림자 타일은 앞서 `GPULightBuffer::populate` 로 담겨 있어야 한다. 뷰의 그리드 번호는 `FrameViewTarget::_lightGridIndex` 에 남는다.
         *          GPU 생산자면 그리드 채우기는 커맨드 리스트가 열린 뒤 `dispatchLightGrid` 가 한다.
         */
        void updateLightBuffer( IRHIDevice* pDevice );
        /**
         * @brief (셋업) 이번 프레임 데칼의 텍스처를 풀어(`_postProcessTextures` — 읽는 중이면 그 데칼은 빠진다) 그리기 목록(`_listFrameDecalDraw`)을 만듭니다.
         * @details 데칼이 있는데 파이프라인에 Decal 패스가 없으면(포워드) 한 번 경고한다 — 덮을 G버퍼가 없다.
         */
        void prepareFrameDecals();
        /**
         * @brief 이번 프레임의 라이트 그리드 생산자입니다. 진단 정책(`RenderDiagnosticsPolicy::_lightGridProducer`, 기본 GPU)이 고르고, Shipping 은 GPU 로 고정입니다.
         * @details GPU 를 골랐는데 컴퓨트 PSO 가 없으면 오류를 한 번 남기고 그리드 없이 그린다 — 채우지 않은 그리드 버퍼를 셰이더가 읽게 두지 않는다.
         */
        LightGridProducer selectLightGridProducer();

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        [[nodiscard]] bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;

        /**
         * @brief 이번 프레임 배치 수에 맞춰 상수 버퍼 슬롯을 **기록 시작 전에** 늘려 둡니다. 드로우마다 슬롯을 하나씩 쓰기 때문입니다.
         * @details 기록 중에는 버퍼 생성과 bindless 등록을 할 수 없다. 지난 프레임의 최대 사용량(하이워터)도 최솟값으로 본다.
         */
        void ensurePassCbCapacityForFrame();
        /** @brief 이번 프레임에 그리는 뷰들의 보기 모드를 비트 집합으로 모읍니다(주 시점은 늘 들어갑니다). */
        uint32 collectFrameViewModeMask() const;

    private:
        IRHIDevice* _pDevice;
        /** @brief 셋업이 채우고 패스 기록이 읽는 프레임 데이터(GPUScene · 그래프 · 뷰 · 라이트 · 그림자 · 하늘 · 패스 상수 링 · 캔버스 · 입자)입니다. 협력 객체가 참조하므로 그들보다 앞에 선언한다. */
        FrameRenderState    _state;
        RenderPipelineAsset _pipelineResource;
        /**
         * @brief 이 렌더러의 PSO(엔진 패스 · 출력 · 머티리얼 변형 · 입자 · 폴백 버퍼)를 만들고 찾는 관리자입니다. 미리보기 씬 렌더러는 주 렌더러의 저장소를 빌린다(`shareCacheOf`).
         * @details `_pipelineResource` · `_gpuScene` 을 참조로 읽으므로 그 둘보다 뒤에 선언한다. 언리얼이 PSO 조립을 메시 패스 프로세서 · PSO 캐시로 렌더러 밖에 두는 것과 같다.
         */
        FramePSOManager _psoManager;
        /**
         * @brief 후처리 효과 셋(자동 노출 · FSR 1 업스케일 · 안개와 볼륨 안개)의 셋업과 기록입니다. 지금 그리는 뷰는 인자로 넘긴다.
         * @details `_psoManager` 를 참조로 읽으므로 그 뒤에 선언한다. 언리얼의 효과별 `AddXxxPass` 처럼 장면 렌더러 밖에 둔다.
         */
        FrameAutoExposure         _autoExposure;
        FrameUpscale              _upscale;
        FrameFog                  _fog;
        FramePostProcessMaterials _postProcessMaterials; ///< 후처리 머티리얼의 원소 버퍼 · PSO(뷰마다 셋업에서 `prepareView`)
        /**
         * @brief 모듈 렌더 확장의 프레임 받아들이기(확장 훅 · 프록시 요소 · 패스 선언 검사 · 프로그램 PSO · 임시 텍스처 · 요소 메시)입니다. 그래프에 묶고 기록하는 일은 패스 실행(`_passExecutor`)이 한다.
         * @details `_psoManager` 를 참조로 쓰므로 그 뒤에 선언한다. 언리얼 `FSceneViewExtensions` 가 확장을 모으고 장면 렌더러가 훅 자리마다 부르는 나눔과 같다.
         */
        FrameExtensionHost _extensionHost;
        /**
         * @brief 뷰(`FrameViewTarget`)의 GPU 자원 수명 — 트랜지언트 풀, TAA · 깊이 히스토리, 가림 타일, 프레임마다의 패스 고르기입니다.
         * @details `_autoExposure` · `_upscale` 을 참조로 쓰므로(뷰의 노출 기록 · 업스케일 중간 RT 를 함께 놓는다) 그 뒤에 선언한다.
         */
        FrameViewResources _viewResources;
        string             _pipelinePath;
        string             _argumentPipelinePath; ///< `initialize` 가 받은 파이프라인(비면 변수 · 팩 설정이 고른다) — `selectPipeline` 이 다시 읽는다

        /// @brief 마지막 프레임의 값입니다(getLastIndirectDrawCallCount).
        uint32           _lastIndirectDrawCallCount;
        uint32           _lastPendingPSOBatchCount; ///< 마지막 프레임의 값입니다(getLastPendingPSOBatchCount).
        vector<float4x4> _listScratchGridViewProj;  ///< 라이트 그리드를 만들 뷰의 뷰-투영(프레임마다 다시 쓴다)
        /// @brief GPU 방출기의 파티클 라이트 슬롯 범위입니다. 라이트 버퍼의 GPU 구간 크기를 정하는 데만 쓴다(범위는 시뮬레이션이 같은 함수로 다시 얻는다).
        vector<VFXGPULightRange> _listScratchGPULightRange;
        /// @brief 컬링이 없는 백엔드에서 추가 뷰의 꼬리 슬롯을 만드는 임시 배열입니다(`applyViewTransparentOrder`, 뷰마다 다시 쓴다).
        vector<uint32> _listScratchTailSlot;

        /// @brief 캡처 · 되읽기입니다. Present 결과 텍스처(`-gv_screenshot`, 크기는 주 출력)를 소유하고 `_pDevice` · 주 시점 풀 · 파이프라인을 빌려 씁니다(그래서 그 셋보다 뒤에 선언한다).
        FrameCapture _capture;
        /**
         * @brief 패스 실행 — 프레임 그래프, 그래프 콜백과 패스별 기록, 드로우, 뷰별 커맨드 리스트 제출입니다. 셋업이 끝난 뒤 `submitGraph` 로 넘긴다.
         * @details 프레임 데이터(`_state`)와 협력 객체(`_psoManager` · `_autoExposure` · `_upscale` · `_fog` · `_extensionHost` · `_capture`)를 참조로 쓰므로 그들보다 뒤에 선언한다.
         *          언리얼 `FSceneRenderer::Render` 가 뷰마다 RDG 패스를 더하고 실행하는 자리다.
         */
        FramePassExecutor _passExecutor;
        FrameRendererHost _host; ///< 주 렌더러만 하는 일의 상대(`bindHost`). 화면 밖 렌더러는 비어 있다
        string            _statusMessage;

        // 아래는 8바이트보다 작은 필드다. 사이에 끼면 패딩이 생기므로 큰 것부터 두고 작은 것을 끝에 모은다.
        FrameRendererStatus _status;
        uint8               _bCallbacksBound            : 1;
        uint8               _bPassResourcesReady        : 1;
        uint8               _bLightGridPSOMissingLogged : 1; ///< 라이트 그리드 컴퓨트 PSO 가 없다는 것을 한 번만 알리는 래치다(셋업 단계에서만 쓴다)
        /**
         * @brief 주 출력의 보기 모드(`RenderViewMode`)입니다.
         * @details 게임 스레드(패킷)와 직접 경로가 프레임 시작에 읽어 뷰 설정에 싣고, 드로우는 뷰 설정만 본다. 쓰는 쪽이 다른 스레드이므로 atomic 이다.
         */
        atomic<uint8> _viewMode;
        /** @brief 에디터 씬 뷰의 보기 모드(`RenderViewMode`)입니다. UI 스레드가 쓰고, 게임 스레드가 패킷을 만들 때 읽습니다. */
        atomic<uint8> _sceneViewMode;
    };

    SW_SERVICE_SLOT( FrameRenderer, Optional );
} // namespace sw
