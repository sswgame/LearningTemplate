/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML 을 로드해 RenderGraph 를 만들고, Shadow, Forward, Deferred, Post 패스를 실행합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Debug/GPUAssert.h"
#include "Engine/Graphics/Debug/GPUNanCheck.h"
#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FrameResourceRegistry.h"
#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"
#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
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

#include <optional>

namespace sw
{
    struct FogSettings;
    struct RenderFramePacket;
    struct RenderPassTypeInfo;
    struct ShaderCompileResult;
    struct SkyGradient;

    class CameraComponent;
    class IRHICommandList;
    class IRHIDevice;
    class Material;
    class Mesh;
    class RenderExtensionRegistry;
    class MaterialInstance;
    class RenderPipelineAssetCache;
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

    /**
     * @brief 진단 · 테스트가 코드에서 전역 변수(`gv_morphDiag` · `gv_drawMerge` · `gv_vertexPool`)와 렌더러 시계를 덮어쓰는 값 하나입니다.
     * @details `std::nullopt`(시각은 음수)가 "덮어쓰지 않음" 이라 전역 변수(시각은 렌더러 시계)를 따른다. 언리얼 뷰 패밀리의 `FEngineShowFlags` 처럼 값 하나로 주고받는다(기억에 의존한 비교).
     *          배포본의 렌더러는 이 값을 갖지 않고(`FrameRenderer::setDebugOverrides` 가 없다) 늘 전역 변수를 따른다.
     */
    struct FrameDebugOverrides
    {
        /**
         * @brief GPU 메시 모프 진단 모드입니다. `-gv_morphDiag` 와 같은 `MeshMorphDiagMode` 입니다(Normal / ForceOn / SkipDispatch / IndexTag).
         * @details 모드 2는 컴퓨트 없이 레스트 버퍼를 정점 셰이더에 물리며, 정답은 레스트 포즈와 같은 이미지다. 그래서 이 모드 하나로
         *          "정점 셰이더의 풀 읽기가 네 백엔드에서 같은가" 를 픽셀로 단언할 수 있다. OpenGL 드라이버가 early-return 형태의
         *          `MorphPool::computeElement` 를 잘못 컴파일해 원소를 하나 어긋나게 읽으면 이 단언에 걸린다(binding.hlsli 주석 참고).
         */
        std::optional<MeshMorphDiagMode> _meshMorphDiag{};
        /// @brief true 면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶고, false 면 배치마다 드로우를 부릅니다(`gv_drawMerge`). 멀티 드로우가 없는 백엔드(DX11)는 늘 배치마다입니다.
        std::optional<bool> _drawMerge{};
        /// @brief true 면 씬 메시 정점을 한 풀 버퍼에 모으고, false 면 메시마다 자기 정점 버퍼로 그립니다(`gv_vertexPool`, 진단과 A/B 비교용).
        std::optional<bool> _vertexPool{};
        /**
         * @brief 시간으로 구동하는 컴퓨트(인스턴스 애니메이션, 메시 모프)가 읽는 절대 시간(초)을 고정합니다.
         * @details 모프 변위는 sin(시간)이라 벽시계로 캡처하면 시각에 따라 이미지가 달라지고, 부하가 걸린 실행에서 픽셀 단언이 흔들린다.
         */
        float32 _animationTime{ -1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FrameRenderer
     * @brief 프레임 경로를 담당합니다. RenderPipeline XML 에서 RenderGraph 를 만들고, GPUScene 과 MeshComponent 를 IRHIDevice 로 그립니다.
     * @details **뷰는 여럿일 수 있다.** 주 시점 하나와 추가 뷰를 같은 그래프로 한 번씩 그린다.
     *          추가 뷰(`RenderViewRequest`)에는 CCTV, 백미러, 미니맵 같은 렌더 텍스처 뷰와 분할 화면, PiP 같은 화면 사각형 뷰가 있다.
     *          뷰마다 트랜지언트 풀, 컬링 슬롯, TAA 히스토리, 커맨드 리스트를 따로 가진다(`ViewTarget`).
     *          컬링 슬롯은 간접 인자, 가시 목록, 컬링 상수 버퍼로 이루어진다.
     *          실행 순서는 컴퓨트 프리패스(모든 뷰의 컬링), 렌더 텍스처 뷰, 주 시점, 화면 사각형 뷰다.
     *          주 시점이 렌더 텍스처를 읽으므로 렌더 텍스처 뷰가 먼저 돌고, 화면 사각형 뷰는 주 시점 위에 겹친다.
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
         * @brief 컴파일된 그래프를 실행합니다. 자기 빌더로 scene 의 스냅샷을 만들고, 패킷 경로와 같은 경로로 GPU 에 올립니다.
         * @details 주 시점은 scene 의 게임 카메라(`Scene::pickActiveGameCamera`)다. 쓸 카메라가 없으면 오류로 알리고 그리지 않은 채 false 입니다.
         */
        [[nodiscard]] bool execute( IRHIDevice* pDevice, Scene& scene );
        /**
         * @brief 렌더 스레드 경로입니다. 미리 만들어 둔 packet 의 GPUScene 을 쓰며 Scene 에는 접근하지 않습니다.
         * @details 주 시점이 없는 패킷(`RenderFramePacket::_bHasViewProj` 가 0 — 활성 씬이 없거나 씬에 카메라가 없다)은 그리지 않고 false 입니다.
         *          3D 내용을 실은 채 주 시점이 없으면 오류로 한 번 알립니다. 주 시점은 `RenderFramePacket::setMainView` 로 싣습니다.
         */
        [[nodiscard]] bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );
        /**
         * @brief 미리보기 씬 패킷(`RenderFramePacket::_listScenePreview` 의 하나)을 그 씬 키의 렌더러로 그립니다. 렌더 스레드에서 주 패킷 뒤에 부릅니다.
         * @details **씬마다 렌더러 하나**를 둔다 — GPUScene(인스턴스 · 배치 · 컬링 산출물) · 트랜지언트 · 패스 상수가 씬별이라 메인 씬과 섞이지 않는다.
         *          언리얼이 미리보기 월드마다 `FScene` 을, 유니티 `PreviewRenderUtility` 가 숨은 씬을 따로 그리는 것과 같은 자리다.
         *          미리보기 렌더러는 프로파일러 · 렌더 타깃 목록 공개 · Present 캡처를 하지 않는다(주 렌더러의 것을 덮지 않게).
         *          출력은 패킷의 `_outputRenderTarget` 이고, 그 타깃의 상태 전환(렌더 타깃 → 셰이더 읽기)은 부르는 쪽(`RenderThread`)이 한다.
         */
        [[nodiscard]] bool executeScenePreviewPacket( IRHIDevice* pDevice, RenderFramePacket& packet );
        /** @brief @p mainPacket 의 미리보기 목록에 없는 씬의 렌더러를 내립니다(창을 닫았거나 가렸다). 미리보기 패킷을 다 그린 뒤 부릅니다. */
        void retireScenePreviews( const RenderFramePacket& mainPacket );
        /**
         * @brief 지금 살아 있는 미리보기 씬 렌더러 수입니다(진단, 테스트, 자동화 감시 값 `Editor.ScenePreviewRenderers`).
         * @details 렌더 스레드가 렌더러를 세우고 내릴 때 공개하는 값이라 어느 스레드에서 읽어도 된다. 창을 닫으면 다음 렌더 프레임에 줄어든다(`retireScenePreviews`).
         */
        uint32 getScenePreviewRendererCount() const { return _scenePreviewRendererCount.load(); }

        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        /** @brief Ready 상태면 true 입니다. */
        bool isReady() const { return _status == FrameRendererStatus::Ready; }

#if !defined( SW_SHIPPING )
        /** @brief 진단 · 테스트 덮어쓰기를 통째로 바꿉니다. 다음 프레임부터 적용됩니다. 배포본에는 없습니다(늘 전역 변수를 따른다). */
        void setDebugOverrides( const FrameDebugOverrides& overrides ) { _debugOverrides = overrides; }
        /** @brief 지금의 진단 · 테스트 덮어쓰기입니다. */
        const FrameDebugOverrides& getDebugOverrides() const { return _debugOverrides; }
#endif
        /** @brief 마지막 프레임에서 씬이 낸 간접 드로우 호출 수입니다(모든 패스 합). 배치 수보다 작으면 묶인 호출이 있다는 뜻입니다. */
        uint32 getLastIndirectDrawCallCount() const { return _lastIndirectDrawCallCount; }
        /**
         * @brief 풀스크린 패스가 이 역할의 입력을 바인딩하지 **않게** 합니다. 언리얼의 r.AmbientOcclusion.Levels=0 같은 쇼 플래그입니다.
         * @details 셰이더는 그 인덱스를 kInvalidIndex 로 읽고 폴백한다(AO 는 1). 같은 프레임 안에서 "이 입력이 실제로 이미지를 바꾸는가" 를 비교할 수 있다.
         *          입력을 매 프레임 계산하기만 하고 화면에는 쓰지 않는 결함을 픽셀로 잡는다.
         */
        void setInputRoleEnabled( RenderPassInputRole role, bool bEnabled );
        /** @brief 초기화와 파이프라인 상태를 반환합니다. */
        FrameRendererStatus getStatus() const { return _status; }
        /** @brief Failed 일 때의 원인 메시지입니다(그 밖에는 비어 있습니다). */
        const string& getStatusMessage() const { return _statusMessage; }
        /** @brief 렌더 그래프를 반환합니다. */
        const RenderGraph& getGraph() const { return _graph; }
        /** @brief GPUScene 을 반환합니다. */
        const GPUScene& getGPUScene() const { return _gpuScene; }
        GPUScene&       getGPUScene() { return _gpuScene; }
        /** @brief 셰이더 핫 리로드를 알립니다. 그 셰이더의 바인딩 레이아웃을 무효화하고 패스 PSO 를 모두 다시 만듭니다. */
        void onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result );
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
        /** @brief 캔버스(화면 2D · 렌더 텍스처 대상) 렌더러입니다. 다시 그린 대상 수 같은 진단 값을 읽을 때 씁니다. */
        const CanvasRenderer& getCanvasRenderer() const { return _canvasRenderer; }

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
         *          에디터 씬 뷰는 `setSceneViewMode` 의 값을 쓴다. 다음 프레임의 `ensureMaterialPSOS` 가 그 프레임 뷰들의 모드로
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
         *          `RenderViewCollector` 가 모니터의 "지난 프레임에 그려졌나" 판정에 쓴다(`OcclusionTileQuery::isSphereOccluded`).
         */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _occlusionMailbox.getLatest(); }

        /** @brief 패스 타입에 해당하는 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle getEnginePSO( RenderPassType passType ) const;
        /**
         * @brief 이 배치를 그릴 PSO 입니다. 머티리얼 퍼뮤테이션, 뷰 모드, 컬 반전을 적용한 변형이 있으면 그것을, 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 드로우 경로가 배치마다 부르는 읽기 전용 조회다. 캐시는 `ensureMaterialPSOS` 가 기록 전에 채운다.
         *          거울 변환을 건 배치(`GPUMeshBatch::_bReverseCulling`)는 패스의 컬 모드를 뒤집은 변형으로 그린다.
         *          변형이 `Ready` 가 아니면(만드는 중 · 실패 · 없음) 0 이고, 이유는 @p pOutState 가 답한다.
         * @param viewMode 이 드로우의 보기 모드입니다. 보통 지금 그리는 뷰의 설정이고, 겹침 모드(ShadedWireframe)의 첫 드로우는 Lit 입니다.
         * @param pOutState 널이 아니면 변형 항목의 준비 상태를 받습니다(변형이 필요 없는 배치는 `Ready`). 드로우 경로가 `Building` 을 `RT.PSO.pendingBatches` 로 셀 때 씁니다.
         */
        RHIPipelineStateHandle psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch, RenderViewMode viewMode,
                                            RenderPSOCache::MaterialPSOState* pOutState = nullptr ) const;
        /**
         * @brief 이 패스가 이 배치를 그리는지 반환합니다. 패스가 머티리얼 define 으로 배치를 거르면(메시 외곽선) 퍼뮤테이션에 그 define 이 있어야 합니다.
         * @details 판정은 `FrameRendererUtil::drawsMaterialInPass` 하나로 한다. 머티리얼 PSO 변형과 셰이더 쿠커도 같은 판정을 쓴다.
         */
        bool drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch ) const;
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터(셰이더 경로, define, 렌더 상태)를 반환합니다. 모르는 PSO 면 false 입니다.
         * @details 어떤 퍼뮤테이션이 실제로 적용되었는지 밖에서 확인할 수 있는 유일한 방법이다.
         *          픽셀로는 드러나지 않는 차이(알파 경로가 컴파일되었는가 등)를 테스트가 여기서 확인한다.
         */
        [[nodiscard]] bool findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 주 시점의 TAA 히스토리 텍스처입니다. 파이프라인에 TAA 패스가 없거나 아직 만들지 않았으면 0 입니다(진단과 테스트용). */
        RHITextureHandle getTAAHistory() const { return _mainView._taaHistory; }
        /** @brief 주 시점의 첨부 풀입니다. 어느 타깃이 있고 몇 번 만들었는지 보는 진단 · 테스트용입니다(고치지 않는다). */
        const TransientAttachmentPool& getMainViewPool() const { return _mainView._transientPool; }
        /**
         * @brief 씬 직접 경로(`execute( scene )`)의 출력 크기를 정합니다. 0 이면 백버퍼 크기를 씁니다. 초상화 베이크처럼 창과 다른 크기로 그릴 때 씁니다.
         * @details 패킷 경로는 패킷의 뷰포트 크기를 따르므로 이 값을 보지 않는다.
         */
        void setOutputSizeOverride( uint32 width, uint32 height )
        {
            _directOutputWidth  = width;
            _directOutputHeight = height;
        }
        /** @brief 마지막 프레임에 실제로 그린 추가 뷰 수입니다. 갱신 주기나 예산 때문에 쉰 뷰는 빠집니다(진단과 테스트용). */
        uint32 getLastRenderedExtraViewCount() const { return _lastRenderedExtraViewCount; }
        /** @brief 가지고 있는 추가 뷰 수입니다. 쉬는 뷰도 포함합니다. */
        uint32 getExtraViewCount() const { return static_cast<uint32>( _listExtraView.size() ); }

    private:
        friend class ExtensionPassContext;

        /**
         * @brief 이번 프레임에 모듈 확장이 선언해 검사를 통과한 패스 하나입니다(선언 + 실행 콜백 + 만들어 둔 PSO). 프레임 끝에 비웁니다.
         */
        struct ExtensionFramePass
        {
            ExtensionPassParams    _params{};
            ExtensionPassExecuteFn _execute{};
            hashed_string          _graphName{};   ///< 그래프 노드 이름("Module.<이름>")
            hashed_string          _colorTarget{}; ///< 첫 쓰기 첨부(그리는 컬러 타깃)
            hashed_string          _depthTarget{}; ///< 깊이 첨부(없으면 빈 이름)
            RHIPipelineStateHandle _pso{ 0 };
        };

        /**
         * @brief 패스 하나를 기록하는 동안 쓰는 로컬 상태입니다.
         * @details 병렬 기록에서는 패스마다 하나씩 존재한다. 주의: 이 값들을 FrameRenderer 멤버로 두고
         *          저장하고 복원하면 "한 번에 한 패스만 돈다" 는 전제가 생기고, 병렬 기록에서 패스끼리 값을 덮어쓴다.
         *          상수 버퍼도 패스마다 따로 있어야 한다. 하나를 공유하면 기록은 지연되고
         *          버퍼 쓰기는 즉시 일어나므로, replay 시점에는 마지막으로 쓴 값만 남는다.
         */
        struct FramePassContext
        {
            IRHICommandList* _pCmd{ nullptr };
            /** @brief 엔진 PassCB 값입니다(이름 기반). ShaderParameterBinder 가 리플렉션 오프셋에 기록합니다. */
            PassConstantValues _passValues{};
            /** @brief `g_World` 입니다. 인스턴스 버퍼가 없는 드로우(풀스크린, 픽스처)의 월드 행렬이며, 씬 메시는 인스턴스 버퍼에서 읽습니다. */
            float4x4           _world{};
            RHIBufferHandle    _passCb{ 0 };
            RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
            /** @brief 패스 스코프 이름에서 리소스를 찾는 레지스트리입니다. 패스를 시작할 때마다 reset 합니다.
             *         병렬 기록에서는 패스마다 독립이어야 하므로 FrameRenderer 의 공유 멤버가 아니라 여기에 둡니다. */
            FrameResourceRegistry _resourceRegistry{};
            /// @brief 엔진 상수 버퍼를 마지막으로 올린 시점의 버퍼, 값 버전, 레지스트리 버전입니다.
            ///        셋이 그대로면 그 드로우는 버퍼를 다시 올릴 필요가 없습니다.
            RHIBufferHandle _lastCbBuffer{ 0 };
            uint32          _lastCbValuesVersion{ 0 };
            uint32          _lastCbRegistryVersion{ 0 };
            /**
             * @brief 마지막으로 리소스를 **실제로 바인딩한** PSO 입니다.
             * @details 슬롯 상태는 PSO 단위다. `setPipelineState` 는 이전 PSO 가 바인딩한 t/u 슬롯이 다음 PSO 로
             *          새지 않게 슬롯 상태를 통째로 비운다. 그래서 값과 레지스트리가 그대로여도 PSO 가 바뀌었으면
             *          다시 바인딩해야 한다. 이를 빼먹으면 배치가 퍼뮤테이션 PSO 로 바뀐 순간 t5(g_SwMaterials)
             *          가 **바인딩되지 않은 채** 드로우가 나가고, Vulkan 은 초기화되지 않은 디스크립터를 읽어
             *          디바이스를 잃는다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
             */
            RHIPipelineStateHandle _lastBindPSO{ 0 };
            /**
             * @brief 이 패스가 어느 컬링 슬롯의 결과를 쓸지 정합니다. 0은 주 시점, 1은 그림자이고, `kFirstExtraCullView` 부터는 추가 뷰입니다.
             * @details 그림자 패스만 그림자 슬롯을 쓰고, 나머지는 지금 그리는 뷰의 슬롯을 쓴다. 컬링 결과는 절두체에 종속되므로
             *          뷰를 잘못 고르면 그림자 캐스터가 사라지거나 화면 밖 물체를 그린다.
             */
            uint32 _cullViewIndex{ 0 };
            /// @brief 지금 기록하는 패스의 종류입니다. `executePass` 가 정하며, 드로우 루프가 이 패스에서 그리지 않는 머티리얼의 배치를 거를 때 봅니다.
            RenderPassType _passType{ RenderPassType::Invalid };
            /// @brief 지금 기록하는 패스의 파이프라인 이름입니다(진단 로그용 — bindless 인덱스 검사가 어느 패스인지 적는다). 패스 밖이면 비어 있습니다.
            hashed_string _passName;
            /// @brief 이 드로우 그룹의 루트 상수 값(머티리얼 원소 수)입니다. 배치마다 다른 값(인스턴스 시작, 모프 풀, 정점 풀)은
            ///        배치 테이블(g_SwBatches)과 인스턴스 슬롯 스트림이 제공하므로 그룹 안에서 루트 상수를 다시 바인딩하지 않습니다.
            ///        PassCB 에 넣으면 한 패스의 드로우들이 서로를 덮어씁니다(binding.hlsli "드로우별 루트 상수" 절 참고).
            uint32 _drawMaterialCount{ 0 };
            /// @brief bindForDraw 가 마지막으로 조회한 PSO 와 그 레이아웃입니다. 같은 PSO 로 연속해서 드로우할 때
            ///        layoutForPSO() 의 뮤텍스와 해시맵 조회를 건너뛰는 패스 로컬 캐시입니다(1항목).
            RHIPipelineStateHandle     _lastLayoutPSO{ 0 };
            const ShaderBindingLayout* _pLastLayout{ nullptr };

            /**
             * @brief "마지막으로 바인딩한 것" 캐시를 모두 지웁니다. PSO 와 버퍼 핸들이 무효가 되는 시점(디바이스 교체)에 부릅니다.
             * @details 핸들 값은 **디바이스 안에서만** 식별자 역할을 한다. 새 디바이스의 PSO 가 옛 디바이스의 PSO 와 같은 값을
             *          받을 수 있고(둘 다 첫 PSO 가 같은 번호), 그러면 `_lastLayoutPSO == pso` 가 참이 되어 이미 파괴된
             *          레이아웃(`_pLastLayout`)을 쓰고 리소스 재바인딩도 건너뛴다. 결과적으로 백엔드를 교체한 뒤
             *          아무것도 그려지지 않는다. 파괴할 때 캐시도 함께 지워야 "같은 값은 같은 대상" 이 성립한다.
             */
            void resetBindingCache()
            {
                _lastCbBuffer          = 0;
                _lastCbValuesVersion   = 0;
                _lastCbRegistryVersion = 0;
                _lastBindPSO           = 0;
                _lastLayoutPSO         = 0;
                _pLastLayout           = nullptr;
            }
        };

        /**
         * @brief 뷰 하나가 프레임을 넘어 유지하는 상태입니다. 트랜지언트 풀, 추가 뷰의 컬링 입력, TAA 히스토리, 직렬 커맨드 리스트, 출력 텍스처가 들어 있습니다.
         * @details 주 시점은 `_mainView` 이고(컬링 입력은 `_arrView[Main]`), 추가 뷰는 카메라 id(`_viewID`)로 찾는다. 패스 코드는 지금 그리는 뷰
         *          (`_pActiveView`)의 것만 본다. 뷰가 바뀌면 트랜지언트 이름(SceneColor 등)이 다른 텍스처를 가리키기 때문이다.
         */
        struct ViewTarget
        {
            TransientAttachmentPool     _transientPool;
            RenderView                  _cullInput;   ///< 추가 뷰의 컬링 입력(행렬, 절두체, 자기 컬링 상수 버퍼)
            unique_ptr<IRHICommandList> _commandList; ///< 이 뷰를 직렬로 기록하는 리스트(주 시점은 병렬 경로가 아닐 때만 쓴다)
            RenderViewSettings          _settings;
            hashed_string               _outputPath;                ///< 렌더 텍스처 경로
            Texture2D*                  _pOutputTexture{ nullptr }; ///< 빌려 쓰는 렌더 텍스처(`TextureCache`)
            RHITextureHandle            _hostTarget{ 0 };           ///< 호스트 타깃 뷰의 출력(호스트가 소유한다)
            uint64                      _viewID{ 0 };
            RHITextureHandle            _taaHistory{ 0 }; ///< TAA resolve 히스토리(지난 TAAColor 의 복사본)
            RHIDescriptorIndex          _taaHistorySRV{ kInvalidDescriptorIndex };
            /**
             * @brief 깊이 히스토리 — 지난 프레임에 이 뷰가 남긴 장면 깊이의 R32 실수 사본(RT · RW 텍스처)입니다. GPU 입자의 깊이 충돌이 읽는다(`recordDepthHistories`).
             * @details 깊이 텍스처는 RW 텍스처가 될 수 없고 에뮬 백엔드의 컴퓨트는 샘플 텍스처를 못 읽어서, 프레임 처음에 풀스크린으로 옮겨 둔다.
             *          장면 깊이 첨부는 프레임 사이에 그대로 남아 있으므로 옮기는 때는 다음 프레임의 컴퓨트 프리패스다(복사본을 프레임 끝에 따로 만들지 않는다).
             */
            RHITextureHandle   _depthHistory{ 0 };
            RHIDescriptorIndex _depthHistoryUAV{ kInvalidDescriptorIndex };
            float4x4           _depthHistoryViewProj{};           ///< 장면 깊이를 그린 프레임의 뷰-투영(그 프레임 끝에 적는다)
            float3             _depthHistoryEye{};                ///< 그 프레임의 눈 위치
            uint8              _bLastFrameDepthValid{ SW_FALSE }; ///< 장면 깊이 첨부에 이 뷰가 그린 깊이가 남아 있다(풀을 새로 만들면 내린다)
            /**
             * @brief 가림 타일 — 깊이 히스토리를 `FrameRendererUtil::kOcclusionTileSize` px 타일마다 최대 깊이 하나로 줄인 구조 버퍼(float, SRV + UAV)입니다. 컬링의 가림 판정(t2)이 읽는다.
             * @details 깊이 히스토리와 수명이 같다(`ensureOcclusionTiles`). `_bOcclusionTilesThisFrame` 은 이번 프레임에 타일을 지었는지다(`recordOcclusionTiles`).
             */
            RHIStructuredBufferSlot _occlusionTiles;
            uint32                  _occlusionColumns{ 0 };
            uint32                  _occlusionRows{ 0 };
            uint8                   _bOcclusionTilesThisFrame{ SW_FALSE };
            RHIStructuredBufferSlot _transparentRank;           ///< 이 뷰의 투명 순번 테이블(정렬 디스패치 t2). GPU 정렬 백엔드에서 쓴다.
            vector<uint32>          _listTransparentBatchOrder; ///< 이번 프레임 이 뷰의 투명 배치 순서(비면 주 순서를 쓴다)
            /// @brief 이번 프레임 이 뷰의 입자 그리기 순서와 자리(`_listTransparentBatchOrder` 안의 자리)입니다. `_bHasParticleOrder` 가 0 이면 주 시점의 것을 쓴다.
            vector<GPUTransparentParticleSlot> _listParticleSlot;
            uint8                              _bHasParticleOrder{ SW_FALSE }; ///< `_listParticleSlot` 이 이 뷰의 배치 순서와 짝인지
            vector<uint32>                     _listInstanceSlot;              ///< 지금 `_instanceSlotStream` 에 들어 있는 내용(같으면 다시 만들지 않는다)
            RHIBufferHandle                    _instanceSlotStream{ 0 };       ///< 이 뷰의 인스턴스 슬롯 스트림(꼬리만 뷰 순서). 컬링이 없는 백엔드(DX11)에서 쓴다.
            uint32                             _transparentTailBase{ 0 };      ///< 순번 테이블 0번의 인스턴스 번호
            /**
             * @brief 이 뷰의 방향광 그림자(셰이더 상수 · 캐스케이드 타일 · 컬링 절두체)입니다. 주 시점은 프레임의 것, 추가 뷰는 자기 카메라에 맞춘 것이다.
             * @details 추가 뷰가 주 시점 볼륨을 쓰면 다른 곳을 보는 CCTV · 분할 화면은 그림자가 빠진다(언리얼의 뷰별 그림자 뷰).
             */
            ViewShadowData _shadow;
            /// @brief 추가 뷰의 그림자 패스 컬링 입력(행렬 · 절두체 · 자기 컬링 상수 버퍼)입니다. 주 시점은 `_arrView[Shadow]` 를 씁니다.
            RenderView _shadowCullInput;
            uint32     _cullSlot{ 0 };
            uint32     _shadowCullSlot{ static_cast<uint32>( RenderViewType::Shadow ) }; ///< 그림자 패스가 쓰는 컬링 슬롯(주 시점은 1, 추가 뷰는 자기 슬롯)
            uint32     _outputWidth{ 0 };                                                ///< 출력 크기(렌더 텍스처, 화면 사각형)
            uint32     _outputHeight{ 0 };
            uint32     _shadowMapResolution{ 0 };                 ///< 풀이 가진 그림자 맵 한 변(0이면 아직 없음). 품질이 바뀌면 풀을 다시 만든다.
            uint32     _lightGridIndex{ invalid_index::kUint32 }; ///< 이번 프레임 이 뷰의 라이트 그리드 번호(`GPULightBuffer::getGridBase`). 없으면 셰이더는 목록 전체를 돈다
            /// @brief 지난번 이 뷰를 그린 뷰-투영입니다(모션 블러의 `g_PrevViewProj`). 기록을 버리는 프레임(`_bHistoryReset`)은 이번 것으로 둔다(속도 0).
            float4x4 _prevViewProj{};
            /**
             * @brief 자동 노출의 적응 EV 기록 둘(1x1 R32 실수 RT)과 그 SRV 입니다. 프레임마다 번갈아 한쪽을 읽고 다른 쪽(`_autoExposureWrite`)에 씁니다.
             * @details 풀과 수명이 같다 — `updateViewTransients` 가 만들고 `releaseViewTransients` 가 놓는다. 새로 만든 기록은 값이 없으므로 그 프레임은 기록을 버린다.
             */
            RHITextureHandle   _arrAutoExposure[2]{};
            RHIDescriptorIndex _arrAutoExposureSRV[2]{ kInvalidDescriptorIndex, kInvalidDescriptorIndex };
            /// @brief 이번 프레임의 측정 상수(최소 EV, 최대 EV, 밝아질 때 · 어두워질 때 목표로 다가가는 비율)입니다. 셋업(`prepareAutoExposure`)이 정하고 톤맵 패스는 읽기만 합니다.
            float4  _autoExposureParams{};
            float32 _autoExposureClock{ 0.0f }; ///< 지난번 이 뷰의 노출을 측정한 렌더러 시각(초, `getAnimationTime`)
            /**
             * @brief FSR 1 의 중간 RT(EASU 출력 = Present 뷰포트 크기, RGBA8)와 그 SRV 입니다. 이 뷰가 FSR 로 키울 때만 있다(`prepareUpscale`).
             * @details 크기는 Present 뷰포트(화면 사각형 · 자기 출력)다 — 풀(배율 크기)과 다르고 출력이 바뀌면 다시 만든다.
             */
            RHITextureHandle     _upscaleIntermediate{ 0 };
            RHIDescriptorIndex   _upscaleIntermediateSRV{ kInvalidDescriptorIndex };
            uint32               _upscaleWidth{ 0 };
            uint32               _upscaleHeight{ 0 };
            RenderViewOutputKind _outputKind{ RenderViewOutputKind::ScreenRect };
            uint8                _bRenderThisFrame{ SW_FALSE };
            uint8                _bSeenThisFrame{ SW_FALSE };
            uint8                _bHasTransparentRank{ SW_FALSE }; ///< 이번 프레임에 `_transparentRank` 로 정렬한다.
            uint8                _bUsesViewSlotStream{ SW_FALSE }; ///< 이번 프레임에 `_instanceSlotStream` 으로 그린다.
            /**
             * @brief 지금 풀로 이 뷰를 한 번 이상 그렸다 — 시간 누적 기록(TAA 히스토리 · `_prevViewProj` · 자동 노출 EV)에 지난 프레임 값이 있다.
             * @details 풀을 새로 만들면(`updateViewTransients`) 기록도 새것이라 내린다.
             */
            uint8 _bHasHistory{ SW_FALSE };
            /**
             * @brief 이번 프레임은 시간 누적 기록을 버린다 — 처음 그리는 프레임이거나 컷(`RenderViewSettings::_bCut`)이다(언리얼 `bCameraCut`).
             * @details `applyViewProjection` 이 프레임마다 한 번 정하고 TAA(이번 원본만) · 모션 블러(지난 뷰-투영 = 이번 것) · 자동 노출(측정값을 그대로)이 모두 이것 하나를 본다.
             *          기능마다 컷을 따로 읽으면 한 기능만 컷을 빠뜨린다. TAA 는 이것을 포함한 `_bTAAHistoryReset` 을 본다.
             */
            uint8 _bHistoryReset{ SW_TRUE };
            /**
             * @brief 이번 프레임은 TAA 기록만 더 버린다 — `_bHistoryReset` 이거나 지난 프레임이 TAA 기록을 쓰지 않았다(TAA 가 빠진 프레임, `_bTAAHistoryWritten`).
             * @details 빠진 TAA(후처리를 끈 뷰 · 품질 · 읽는 쪽 없음)는 출력이 원본을 가리키거나 없고 기록을 쓰지 않는다. 다시 도는 첫 프레임의 기록은 그 앞 화면이라 섞으면 그동안 움직인 것의 가장자리에 옛 그림이 남는다.
             *          `beginViewHistory` 가 `_bHistoryReset` 과 같은 자리에서 정한다(기능마다 따로 정하지 않는다).
             */
            uint8 _bTAAHistoryReset{ SW_TRUE };
            uint8 _bTAAHistoryWritten{ SW_FALSE };     ///< 이번 프레임의 TAA 가 돌아 기록을 쓴다 — 패스 고르기(`applyViewPassSelection`)의 결과 하나다. 다음 프레임의 `beginViewHistory` 가 읽는다
            uint8 _autoExposureWrite{ 0 };             ///< 이번 프레임에 쓰는 자동 노출 기록 칸(0 · 1). 톤맵은 이 칸을 읽는다
            uint8 _bAutoExposureThisFrame{ SW_FALSE }; ///< 이번 프레임에 이 뷰의 노출을 측정한다(`prepareAutoExposure`)
            uint8 _bAutoExposureMeasured{ SW_FALSE };  ///< 지금 자동 노출 기록에 측정한 값이 있다(기록을 새로 만들면 내린다 — 첫 측정은 지난 칸을 섞지 않는다)
            uint8 _bUpscaleThisFrame{ SW_FALSE };      ///< 이번 프레임에 Present 가 FSR 1(EASU + RCAS)로 키운다(`prepareUpscale`)
        };

        /** @brief 주 출력에 그리는 패스(Present, Canvas)의 대상입니다. 지금 뷰가 정하며 `resolvePresentTarget` 이 구합니다. */
        struct PresentTarget
        {
            RHITextureHandle _texture{ 0 }; ///< 0이면 백버퍼
            uint32           _width{ 0 };
            uint32           _height{ 0 };
            RHIFormat        _format{ RHIFormat::Unknown };   ///< 대상의 실제 포맷(PSO 를 고를 때 쓴다)
            uint8            _bOwnOutput{ SW_FALSE };         ///< 추가 뷰의 자기 출력 전체(렌더 텍스처, 호스트 타깃)
            uint8            _bCapture{ SW_FALSE };           ///< 백버퍼 대신 스크린샷 캡처 텍스처에 그린다.
            uint8            _bCaptureToBack{ SW_FALSE };     ///< Swapchain 을 쓰는 마지막 패스 뒤에 캡처를 백버퍼로 복사한다(출력이 곧 백버퍼일 때).
            uint8            _bCaptureFromOutput{ SW_FALSE }; ///< Swapchain 을 쓰는 마지막 패스 뒤에 출력 RT(에디터 게임 뷰)를 캡처로 복사한다.
        };

        // ------------------------------------------------------------------------------
        // 4) 패스 자원, 콜백, 드로우
        // ------------------------------------------------------------------------------
        /** @brief 패스용 상주 GPU 자원을 확보합니다. 패스 CB 링, 컴퓨트 CB 테이블, 엔진 PSO, Present 변형, 머티리얼 폴백이 해당합니다. */
        void ensurePassResources();
        /**
         * @brief 패스용 상주 GPU 자원을 해제합니다. 디바이스가 없으면 각 자원이 핸들만 잊습니다.
         * @details 트랜지언트 크기를 따르는 자원(첨부, TAA 히스토리, Present 캡처)은 여기서 해제하지 않는다. 그것은 `releaseTransientResources` 의 몫이며,
         *          셰이더 핫 리로드처럼 패스 자원만 다시 만드는 경로에서는 이 자원이 다시 만들어지지 않기 때문이다.
         */
        void releasePassResources();

        /** @brief 컴퓨트 디스패치 상수 버퍼 한 항목입니다. 생성(`ensurePassResources`)과 해제(`releasePassResources`)가 같은 테이블을 돕니다. */
        struct ComputeConstantBufferRow
        {
            RHIConstantBufferSlot* _pSlot{ nullptr };
            uint32                 _byteSize{ 0 };
            const utf8*            _pUsage{ nullptr };
        };
        /// @brief 컴퓨트 상수 버퍼 수입니다. 고정 뷰마다 컬링과 정렬이 하나씩 있고, 인스턴스 애니메이션, 메시 모프, 메시 스킨이 하나씩 더 있습니다. 추가 뷰의 것은 그 뷰가 가집니다.
        static constexpr uint32 _s_kComputeConstantBufferCount = static_cast<uint32>( RenderViewType::Count ) * 2 + 3;
        /** @brief 컴퓨트 상수 버퍼 테이블을 채웁니다. 새 컴퓨트 상수 버퍼는 여기에 한 줄만 추가하면 됩니다. 생성과 해제를 따로 적지 않습니다. */
        void collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] );
        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 반환합니다.
         * @details 반환값은 곧 "Clear 로 열어도 되는가" 다. 같은 레벨의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계 구역 안에 있어야 한다. 나누면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지운다.
         */
        bool markAttachmentCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다. 프레임 시작에 부릅니다. */
        void resetClearedAttachments();
        /**
         * @brief 이번 프레임의 패스 상수 버퍼 슬롯을 하나 골라 context 에 붙입니다.
         * @details 커맨드 기록은 지연되고 상수 버퍼 쓰기는 즉시 일어난다. 그래서 패스마다 별도 버퍼를
         *          써야 재생 시점에 각 패스의 상수가 남는다. 커서는 원자적이라
         *          병렬 기록에서도 안전하다. 슬롯이 모자라면 false 이고 부르는 쪽은 그리지 않는다(앞 슬롯을 공유하지 않는다).
         */
        [[nodiscard]] bool acquirePassCb( FramePassContext& context );
        /** @brief 프레임을 시작할 때마다 패스 상수 슬롯 커서를 되감고 시드를 0번 슬롯에 맞춥니다. */
        void resetPassCbRing();
        /** @brief 주 시점의 트랜지언트 텍스처를 확보합니다. 크기는 출력 크기 × 사각형 × 해상도 배율이며, 출력 크기는 덮어쓴 크기가 있으면 그 값, 없으면 백버퍼입니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /** @brief 뷰 하나의 트랜지언트를 이 크기로 맞춥니다. 크기가 같으면 그대로 두고, 다시 만들었으면 true 입니다. */
        bool updateViewTransients( ViewTarget& view, uint32 width, uint32 height );
        /** @brief 뷰 하나의 트랜지언트와 TAA 히스토리를 해제합니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseViewTransients( ViewTarget& view );
        /**
         * @brief 이 뷰가 이번 프레임에 기록할 풀스크린 패스를 고릅니다. 꺼진 효과는 출력이 원본을 가리키거나(`kBypassesToSource`) 없고(`kOmittedWhenOff`),
         *        읽는 패스가 모두 빠진 풀스크린 패스(블룸이 꺼진 뷰의 BloomDownsample)도 빠집니다. 도는 패스의 출력 타깃은 처음이면 만듭니다.
         * @details 셋업 단계에서(기록 전) 뷰마다 한 번 부른다. 빠진 패스는 기록하지 않는다(`executeGenericFullscreenPass`). 타깃은 한 번 만들면 풀이 바뀔 때까지 둔다.
         *          빠질 수 있는 것은 출력을 혼자 쓰는 일반 풀스크린 패스뿐이고, 파이프라인에 읽는 패스가 하나도 없는 출력의 패스는 그대로 돈다(디버그 · 스크린샷용).
         * @param viewProj 이번 프레임 이 뷰의 뷰-투영입니다(지난번 것 `_prevViewProj` 와 같으면 카메라 모션 블러는 꺼진 것이다). `_bHistoryReset` 을 정한 뒤 부른다.
         */
        void applyViewPassSelection( ViewTarget& view, const float4x4& viewProj );
        /** @brief 추가 뷰 하나를 통째로 해제합니다. 트랜지언트, 컬링 상수 버퍼, 리스트, 빌린 렌더 텍스처가 해당합니다. */
        void releaseExtraView( ViewTarget& view );
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
        ViewTarget& getOrCreateExtraView( uint64 viewID );
        /** @brief 요청 하나로 추가 뷰를 준비합니다(설정 · 출력 · 렌더 텍스처 빌리기 · 컬링 상수버퍼 · 트랜지언트). 준비하지 못하면 이번 프레임에 그리지 않습니다. */
        void prepareExtraView( const RenderViewRequest& request );
        /**
         * @brief 스냅샷에 든 이 뷰의 투명 순서를 뷰 자원(순번 테이블, 슬롯 스트림, 배치 순서)에 옮깁니다. 없으면 주 순서로 그립니다.
         * @details GPU 정렬 백엔드는 순번 테이블을 정렬 디스패치에 쓰고, 컬링이 없는 백엔드는 배치 안을 뷰 순서로 다시 배치한 슬롯 스트림을 드로우에 쓴다. 업로드 뒤, 기록 전에 부른다.
         */
        void applyViewTransparentOrder( ViewTarget& view );
        /** @brief 뷰 하나의 투명 순서 자원(순번 테이블, 슬롯 스트림)을 해제합니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseViewTransparentOrder( ViewTarget& view );
        /** @brief 추가 뷰 하나를 그래프로 그립니다(직렬, 자기 리스트). 지금 뷰를 그 뷰로 바꿨다가 주 시점으로 되돌립니다. */
        void renderExtraView( IRHIDevice* pDevice, ViewTarget& view );
        /** @brief 지금 그리는 뷰의 트랜지언트 풀입니다. */
        TransientAttachmentPool&       activePool() { return _pActiveView->_transientPool; }
        const TransientAttachmentPool& activePool() const { return _pActiveView->_transientPool; }
        /** @brief 지금 그리는 뷰의 컬링 입력입니다. 주 시점은 `_arrView[Main]` 입니다. */
        RenderView& activeCullInput() { return _pActiveView == &_mainView ? view( RenderViewType::Main ) : _pActiveView->_cullInput; }
        /** @brief 지금 그리는 뷰가 추가 뷰면 true 입니다. */
        bool isRenderingExtraView() const { return _pActiveView != &_mainView; }
        /**
         * @brief 뷰의 TAA 히스토리 텍스처를 **셋업 단계에서** 만들어 둡니다.
         * @details 주의: TAA 패스 콜백은 병렬 기록에서 태스크 스레드가 실행하므로, 콜백 안에서 만들고 bindless 에 등록하면
         *          다른 스레드가 같은 레지스트리를 읽는 도중에 레지스트리가 resize 된다. 파이프라인이 TAA 를
         *          선언했는지, 대상 첨부의 포맷이 무엇인지는 셋업 시점에 이미 알 수 있다.
         */
        void ensureTAAHistory( ViewTarget& view );
        /**
         * @brief 뷰의 깊이 히스토리(R32 실수 RT · RW 텍스처)를 **셋업 단계에서** 풀 크기로 만들어 둡니다(`ensureTAAHistory` 와 같은 이유). 풀을 새로 만들었으니 지난 깊이는 없다.
         * @details 파이프라인에 장면 깊이 첨부(`FrameRendererUtil::Attachment::kSceneDepth`)가 없으면 만들지 않는다(2D 파이프라인 — 깊이 충돌을 쓸 깊이가 없다).
         */
        void ensureDepthHistory( ViewTarget& view );
        /** @brief 뷰의 자동 노출 기록 둘(1x1 R32 실수 RT + SRV)을 **셋업 단계에서** 만들어 둡니다(`ensureTAAHistory` 와 같은 이유). 톤맵하는 패스가 없으면 만들지 않습니다. */
        void ensureAutoExposureHistory( ViewTarget& view );
        /** @brief 뷰의 자동 노출 기록을 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseAutoExposureHistory( ViewTarget& view );
        /**
         * @brief 지금 그리는 뷰의 이번 프레임 노출을 정합니다 — 잴지, 측정 상수(EV 범위 · 다가가는 비율), 쓰는 기록 칸입니다.
         * @details 셋업에서 뷰마다 한 번(`applyViewPassConstants`) 부른다. 기록 중의 패스(`recordAutoExposure`)는 여기서 정한 것을 읽기만 한다(병렬 기록 — 뷰 상태를 쓰지 않는다).
         *          카메라의 최소 EV 와 최대 EV 가 같으면 재지 않고 고정 노출 2^-EV 를 `g_CameraGrade.x` 가 될 @p inoutExposureScale 에 곱한다.
         */
        void prepareAutoExposure( float32& inoutExposureScale );
        /**
         * @brief 톤맵하는 패스(`findTonemapPass`) 앞에서 자동 노출 측정 · 적응 풀스크린을 같은 리스트에 기록하고, 그 패스가 읽을 이번 EV 를 `AutoExposure` 이름으로 겁니다(autoexposure.hlsl).
         * @details @p passDesc 가 톤맵하는 패스가 아니거나 이번 프레임에 재지 않는 뷰면 아무것도 하지 않는다. 측정하는 원본은 그 패스의 SourceColor 입력이다.
         */
        void recordAutoExposure( FramePassContext& context, const RenderGraphPassDesc& passDesc );
        /**
         * @brief 뷰 @p view 가 이번 프레임에 Present 에서 FSR 1 로 키울지 정하고, 키우면 중간 RT 를 **셋업 단계에서** 뷰포트 크기로 맞춥니다.
         * @details 키우는 조건: 업스케일러가 spatial(`gv_upscaler`), 정수 업스케일이 아님, 풀이 뷰포트보다 작음, Present 가 톤맵하지 않음(EASU 는 톤맵한 색을 받는다).
         *          Present 가 톤맵하는 파이프라인(후처리 체인)에서 켜면 한 번 오류를 남기고 선형으로 키운다 — 그 파이프라인에는 톤맵한 중간 그림이 없다.
         */
        void prepareUpscale( ViewTarget& view, uint32 viewportWidth, uint32 viewportHeight );
        /** @brief 뷰의 FSR 중간 RT 를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseUpscaleIntermediate( ViewTarget& view );
        /**
         * @brief Present 렌더 패스를 열기 **전에** EASU(배율 크기 원본 → 중간 RT)를 기록합니다. 이번 프레임에 FSR 로 키우지 않는 뷰면 false 이고 아무것도 하지 않습니다.
         * @return true 면 Present 는 블릿 대신 `drawUpscaleRcas` 로 그린다
         */
        bool recordUpscaleEasu( FramePassContext& context, string_view sourceName );
        /** @brief 열린 Present 렌더 패스(뷰포트를 건 뒤)에 RCAS(중간 RT → 대상)를 그립니다. @p viewportX · @p viewportY 는 대상 안의 뷰포트 왼쪽 위(px)입니다. */
        void drawUpscaleRcas( FramePassContext& context, RHIFormat targetFormat, float32 viewportX, float32 viewportY );
        /** @brief 뷰 @p view 의 지난 깊이를 이번 프레임에 히스토리로 옮길 수 있으면 true 입니다(지난 깊이 · 히스토리 · PSO 가 모두 있다). */
        bool canCopyDepthHistory( const ViewTarget& view ) const;
        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰마다 지난 장면 깊이를 깊이 히스토리로 옮기는 풀스크린 하나를 기록합니다(depthhistory.hlsl).
         * @details GPU 입자 시뮬레이션보다 먼저 기록한다 — 깊이 충돌이 이 히스토리를 읽는다. 지금 읽는 쪽은 주 시점뿐이지만 히스토리는 뷰마다 둔다.
         */
        void recordDepthHistories();
        /** @brief 뷰 @p target 하나의 깊이 히스토리 복사를 기록합니다(`recordDepthHistories` 가 뷰마다 부른다). */
        void recordDepthHistory( ViewTarget& target );
        /** @brief 이번 프레임에 그린 뷰마다 그 뷰-투영과 눈 위치를 적어 둡니다(다음 프레임의 깊이 히스토리가 짝이 된다). 모든 뷰를 그린 뒤 부릅니다. */
        void rememberDepthHistoryViews();
        /** @brief 뷰의 가림 타일 버퍼를 **셋업 단계에서** 깊이 히스토리 크기에 맞춰 만들어 둡니다. 깊이 히스토리가 없으면(2D 파이프라인) 만들지 않습니다. */
        void ensureOcclusionTiles( ViewTarget& view );
        /**
         * @brief 그래프가 끝난 뒤 주 시점의 부동소수 컬러 패스 출력을 NaN · Inf 감시 컴퓨트로 훑습니다(`GPUNanCheck`). 꺼져 있으면(`-gv_nanCheck` 없음 · 에뮬 백엔드) 아무것도 하지 않는다.
         * @details 패스 이름 + 첨부 이름이 로그의 이름이다. 같은 텍스처를 가리키는 출력은 한 번만 훑는다. 텍스처는 훑기 위해 셰이더 읽기 상태로 옮기고 렌더 타깃 상태로 되돌린다.
         */
        void scanPassOutputsForNan( IRHIDevice* pDevice );
        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰 @p target 의 가림 타일(깊이 히스토리 → 타일마다 최대 깊이)을 기록합니다(occlusiontiles.hlsl). 깊이 히스토리를 옮긴 뒤 · 컬링 앞입니다.
         * @details `gv_occlusionCulling` 이 꺼졌거나 이번 프레임에 옮긴 지난 깊이가 없으면 짓지 않는다 — 그 뷰의 컬링은 가림을 보지 않는다.
         */
        void recordOcclusionTiles( ViewTarget& target );
        /**
         * @brief 주 시점 가림 타일을 되읽는 복사를 컴퓨트 프리패스 리스트에 넣고, 이미 도착한 사본이 있으면 우편함에 올립니다(`getOcclusionSnapshot`).
         * @details `recordOcclusionTiles` 직후에 부른다. 이번 프레임에 타일을 짓지 않았거나 기록을 버리는 프레임이면 우편함을 비운다(그 타일은 지난 장면의 것이라 맞지 않는다).
         *          요청 때의 뷰-투영 · 격자는 `_arrOcclusionReadbackMeta` 에 적어 두었다가 사본이 도착하면 붙인다.
         */
        void requestOcclusionReadback( const ViewTarget& view );
        /**
         * @brief 뷰 @p view 의 컬링 상수에 가림 판정 값을 채웁니다. 이번 프레임에 타일을 지었고 기록을 버리는 프레임(처음 · 컷)이 아닐 때만 켭니다.
         * @details 지난 프레임 깊이로 이번 경계를 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나온다(언리얼의 지난 프레임 HZB 와 같은 한계). 컷 프레임은 지난 깊이가
         *          다른 장면이라 보지 않는다.
         */
        void fillOcclusionParams( const ViewTarget& view, FrameRendererUtil::GPUCullParams& inoutParams ) const;
        /**
         * @brief 뷰의 이번 프레임 시간 누적 기록을 시작합니다 — 기록을 버릴지(`_bHistoryReset`: 이 풀로 처음 그리거나 컷)를 정하고 그렸다고 표시합니다.
         * @details 셋업에서 뷰마다 프레임에 한 번 부른다(주 시점은 `updatePassConstants`, 추가 뷰는 `prepareExtraView`). 컬링(가림)이 기록 전에 돌므로 그리기 때(`applyViewProjection`)가 아니라 여기서 정한다.
         */
        void beginViewHistory( ViewTarget& view );
        /** @brief GPU 입자 시뮬레이션의 이번 프레임 입력(PSO · 주 시점 깊이 히스토리 · 인스턴스 버퍼)입니다. GPUScene 을 올린 뒤 부릅니다. */
        VFXSimulationFrameInput makeVFXSimulationInput() const;
        /** @brief 트랜지언트 텍스처를 해제합니다. */
        void releaseTransientResources();
        /** @brief 그래프 패스 콜백을 한 번만 바인딩합니다. */
        void bindPassCallbacks();
        /** @brief RenderGraph 가 패스를 실행할 때 부르는 콜백입니다. */
        void onGraphPassExecute( const RenderGraphPassContext& context );

        /**
         * @brief 레벨을 기록하기 **직전에** 그 레벨이 만질 자원의 배리어를 미리 발행합니다. 직렬 경로에서는 패스마다 발행합니다.
         * @details 자원 이름으로 실제 텍스처를 찾아 `prepareTextureForShaderRead` 와
         *          `prepareTextureForRenderTarget` 을 레벨 첫 패스 리스트의 앞머리에 기록한다(직렬 경로에서는
         *          그 패스의 리스트). 같은 레벨의 다른 리스트는 큐 순서상 그 뒤에 실행되므로 GPU 타임라인에서도 배리어가 앞선다.
         *
         *          이렇게 하면 패스 콜백이 이미 맞는 상태를 보므로 기록 중에 리소스 상태를 바꾸지
         *          않는다. 주의: 병렬 기록 스레드가 배리어를 정하게 하면 중복 배리어와 레이아웃 불일치가 생긴다.
         */
        void onGraphLevelPrologue( const RenderGraphLevelContext& context );
        /**
         * @brief 패스 타입에 맞는 실행 함수를 부릅니다. 공통 입력(`PassExecuteInput`)을 한 번 정하고 패스별 `execute*Pass` 로 나눠 줍니다.
         * @details 패스 서술을 참조로 받는다 — 서술 없이 원본 이름(SceneColor · TAAColor 등)으로 타깃을 짐작하던 길을 형식으로 막았다.
         *          컬러 타깃은 서술의 `_listResolvedColorOutput` 이고 로드가 그 선언을 요구한다(`RenderPassTraitFlag::kDrawsDeclaredColor`).
         */
        void executePass( FramePassContext& context, const RenderGraphPassDesc& passDesc );

        /** @brief 패스 하나를 실행할 때 패스별 실행 함수가 함께 쓰는 입력입니다. `executePass` 가 한 번 정합니다. */
        struct PassExecuteInput
        {
            FramePassContext&                                      _context;
            RenderPassType                                         _passType;
            const RenderGraphPassDesc&                             _passDesc;          ///< 패스 서술
            const vector<RenderGraphPassDesc::ResolvedAttachment>& _declaredColor;     ///< 선언한 컬러 출력(선언 순서). kDrawsDeclaredColor 패스는 비어 있지 않다
            hashed_string                                          _passDepth;         ///< 바인딩할 뎁스. 선언이 없거나 이번 프레임에 없으면 빈 이름
            bool                                                   _bTransparentBatch; ///< 이 패스가 투명 배치를 그리는지(테이블 플래그)
        };
        /** @brief 첨부의 이번 프레임 첫 사용이면 Clear, 아니면 Load 입니다(`markAttachmentCleared`). */
        RHIRenderPassLoadOp colorLoadFor( const hashed_string& name );
        /** @brief @p targetName 하나에 풀스크린 삼각형을 그립니다. 열지 못하면 아무것도 하지 않습니다. */
        void executeFullscreenInto( FramePassContext& context, RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor );
        /** @brief 일반 풀스크린 패스(테이블의 kGenericFullscreen)입니다. 선언한 입력을 모두 바인딩하고 선언한 첫 컬러 출력에 그립니다. 이 뷰에서 빠진 패스(`applyViewPassSelection`)는 기록하지 않습니다. */
        void executeGenericFullscreenPass( const PassExecuteInput& input );
        /** @brief ForwardOpaque · ForwardOnlyOpaque 입니다. 그림자 맵 입력을 바인딩하고 선언한 첫 컬러 출력에 그립니다. */
        void executeForwardOpaquePass( const PassExecuteInput& input );
        /** @brief G버퍼(알베도 · 노멀 MRT)입니다. 노멀 출력이 없거나 PSO 가 없으면 그리지 않습니다. */
        void executeGBufferPass( const PassExecuteInput& input );
        /** @brief 투명 큐(메시 투명 배치와 입자)입니다. */
        void executeTransparentPass( const PassExecuteInput& input );
        /** @brief 장면 색 사본입니다. 원본 색 입력을 선언한 컬러 출력으로 복사합니다(반투명 앞 — 물의 굴절이 읽는다). 셰이더 · PSO 가 없습니다. */
        void executeSceneColorCopyPass( const PassExecuteInput& input );
        /** @brief 인버티드 헐 외곽선입니다. 와이어프레임 · 표면 값 보기에서는 빠집니다. */
        void executeMeshOutlinePass( const PassExecuteInput& input );
        /** @brief 인스턴스 강조 외곽선 패스(HighlightOutline · HighlightSense)를 실행합니다 — 패스의 강조 방식인 인스턴스만 정점이 남습니다. */
        void executeHighlightPass( const PassExecuteInput& input );
        /** @brief 하늘입니다. 씬에 하늘이 없거나 뎁스 · 컬러 출력이 없으면 그리지 않습니다. */
        void executeSkyPass( const PassExecuteInput& input );
        /** @brief TAA 와 히스토리 갱신입니다. 후처리를 끈 뷰와 컷 프레임은 원본을 넘깁니다. */
        void executeTAAPass( const PassExecuteInput& input );
        /** @brief 최종 출력(백버퍼 · 캡처 · 렌더 텍스처)으로의 블릿입니다. 화면 사각형 · 정수 업스케일 뷰포트를 적용합니다. */
        void executePresentPass( const PassExecuteInput& input );
        /** @brief 화면 2D 를 주 출력에 그립니다. 주 출력에 마지막으로 그리는 뷰만 그립니다. */
        void executeCanvasPass( const PassExecuteInput& input );
        /**
         * @brief XML 이 선언한 입력을 **역할 이름으로** 모두 바인딩합니다. 풀스크린 패스가 공통으로 씁니다.
         * @details 역할은 로드 시점에 해석해 둔다(`_listResolvedInput`). 여기서 바인딩하는 목록과 검증이 대조한 목록이 같으므로
         *          "선언은 했는데 바인딩되지 않는 입력" 이 생기지 않는다. 쇼 플래그로 끈 역할은 건너뛴다.
         */
        void registerDeclaredInputs( FramePassContext& context, const RenderGraphPassDesc& passDesc );
        /** @brief 역할의 셰이더 이름입니다. intern 된 hashed_string 입니다. */
        const hashed_string& inputRoleName( RenderPassInputRole role ) const;
        /**
         * @brief 패스 상수 값(PassConstantValues)을 채웁니다. 업로드와 바인딩은 ShaderParameterBinder 가 합니다.
         * @param mainViewProj 주 시점 카메라의 뷰-투영입니다. 카메라가 없는 프레임은 여기까지 오지 않습니다(기본 카메라로 대신 그리지 않는다).
         */
        void updatePassConstants( FramePassContext& context, const float4x4& mainViewProj );
        /** @brief 지금 그리는 뷰에 따라 달라지는 패스 상수를 채웁니다. 외곽선 텍셀 크기와 패스 플래그(후처리 끄기)가 해당합니다. */
        void applyViewPassConstants( FramePassContext& context );

        /**
         * @brief 이번 프레임의 주 광원 값입니다(`g_KeyLightDirIntensity` · `g_KeyLightColor`). 패킷이 값을 실어 오면 그 값을, 아니면 빛 없음을 씁니다.
         * @details 방향, 색, 세기, 앰비언트는 씬의 주 광원에서 온다. 기본값은 "주 광원 없음" 이다. 세기와 색과 앰비언트가 0 이라 그 씬은 어둡게 그려진다.
         *          방향은 단위 벡터로 둔다. 셰이더가 정규화하기 때문이다(water.hlsl 반사).
         */
        struct FrameLightState
        {
            float4 _dirIntensity{ 0.0f, -1.0f, 0.0f, 0.0f };
            float4 _colorAmbient{ 0.0f, 0.0f, 0.0f, 0.0f };
        };
        /**
         * @brief 이번 프레임의 하늘을 셰이더 상수 모양으로 둔 것입니다(`g_SkyZenith` · `g_SkyHorizon` · `g_SkyGround`, 배치는 `SkyGradient` 머리 주석).
         * @details 씬에 하늘이 없으면 `_bHasFrameSky` 가 0 이고 스카이 패스가 그리지 않는다 — 배경은 SceneColor 클리어 색이다(기본 하늘로 대신 칠하지 않는다).
         */
        struct FrameSkyState
        {
            float4 _zenithExponent{};  ///< rgb 천정 색, w 지평선 지수
            float4 _horizon{};         ///< rgb 지평선 색
            float4 _groundIntensity{}; ///< rgb 땅 색, w 세기
            // 대기 산란 모드(`SkyMode::Atmosphere`)와 구름 층의 상수다. 배치는 `SkyAtmosphere` · `SkyClouds` 머리 주석과 sky.hlsl 머리 주석.
            float4 _atmosphereRayleigh{}; ///< rgb 레일리 산란 계수(1/km), w 대기 모드면 1 · 그러데이션이면 0
            float4 _atmosphereMie{};      ///< x 미 산란 계수(1/km), y 미 규모 높이(km), z 미 이방성, w 해 복사휘도 배율(해 색에 곱한다)
            float4 _atmosphereGeometry{}; ///< x 행성 반지름(km), y 대기 두께(km), z 눈 고도(km), w 해 원반 각 반지름(라디안)
            float4 _cloudShape{};         ///< x 덮는 비율, y 층 고도(km), z 노이즈 배율(1/km), w 최대 불투명도
            float4 _cloudWind{};          ///< xy 바람 방향 단위 벡터(xz 평면), z 바람 속도(km/s), w 경계 부드러움
            float4 _cloudColor{};         ///< rgb 구름 색, w 구름을 그리면 1
        };
        /** @brief 이번 프레임의 하늘(`_frameSky` · `_bHasFrameSky`)을 정합니다. 씬 직접 경로와 패킷 경로가 같은 규칙으로 부릅니다. */
        void applyFrameSky( bool bHasSky, const SkyGradient& sky );
        /**
         * @brief 이번 프레임의 안개를 셰이더 상수 모양으로 둔 것입니다(`g_FogColorDensity` · `g_FogHeightParams` · `g_FogInscatter` · `g_FogVolumeParams`, 배치는 `FogSettings` 머리 주석).
         * @details 씬에 안개(`FogComponent` · 물속)가 없으면 `_bHasFrameFog` 가 0 이고 안개 패스가 그리지 않는다. 볼륨 안개 값(`_volumeParams`)은 주 시점에서 볼륨이 실제로 계산된 프레임에만
         *          x=1 로 켜진다(`executeFogPass` 가 뷰마다 덮는다 — 추가 뷰는 볼륨을 계산하지 않는다).
         */
        struct FrameFogState
        {
            float4 _colorDensity{};          ///< rgb 안개 색, w 밀도
            float4 _heightParams{};          ///< x 높이 감쇠, y 기준 높이, z 시작 거리, w 최대 불투명도
            float4 _inscatter{};             ///< rgb 방향광 산란 색 × 세기, w 지수
            float4 _volumeParams{};          ///< x 볼륨 안개를 켠 1 · 끈 0, y 볼륨 거리, z 산란 배율, w 이방성
            uint8  _bVolumetric{ SW_FALSE }; ///< 설정이 볼륨 안개를 켰다(컴퓨트가 돌지는 `_bFogVolumeActive`)
        };
        /** @brief 이번 프레임의 안개(`_frameFog` · `_bHasFrameFog`)를 정합니다. 씬 직접 경로와 패킷 경로가 같은 규칙으로 부릅니다. */
        void applyFrameFog( bool bHasFog, const FogSettings& fog );
        /**
         * @brief 볼륨 안개의 버퍼(froxel 출력, 갱신 입력)를 갖추고 주 시점의 입력을 올립니다. **기록 전에** 부릅니다(라이트 그리드와 주 시점이 정해진 뒤 — `uploadSceneAndSubmit`).
         * @details 설정이 볼륨 안개를 켰고 컴퓨트 PSO · 라이트 버퍼가 있을 때만 켠다(`_bFogVolumeActive`). 못 만들면 오류를 한 번 남기고 볼륨 없이 평범한 안개로 그린다 — 다른 갈래가 아니라 같은 패스의 꺼진 상태다.
         */
        void prepareVolumetricFog( IRHIDevice* pDevice );
        /** @brief 볼륨 안개 컴퓨트를 기록합니다(라이트 그리드 디스패치 뒤). `_bFogVolumeActive` 일 때만 돕니다. */
        void dispatchVolumetricFog();
        /**
         * @brief 실시간 라이트 프로브 갱신의 버퍼와 입력을 갖춥니다(`_listFrameRealtimeProbe`). **기록 전에** 부릅니다 - 라이트 버퍼가 올라간 뒤(프로브 버퍼의 UAV가 정해진 뒤)다.
         * @details 못 켜는 이유(컴퓨트 PSO · 프로브 버퍼 UAV · 버퍼 할당)는 갱신기가 한 번만 오류로 남기고, 실시간 볼륨은 검게 남는다(베이크로 대신하지 않는다).
         */
        void prepareRealtimeProbes( IRHIDevice* pDevice );
        /** @brief 실시간 프로브 갱신 컴퓨트를 기록합니다(라이트 그리드 · 볼륨 안개 뒤, 조명 패스 앞). `prepareRealtimeProbes` 가 켠 프레임에만 돕니다. */
        void dispatchRealtimeProbes();
        /** @brief 안개 패스(`RenderPassType::Fog`)를 실행합니다. 안개가 없거나 후처리를 끈 뷰 · 표면 값 보기에서는 그리지 않습니다. */
        void executeFogPass( const PassExecuteInput& input );
        /**
         * @brief 이번 프레임의 그림자 배치(`_frameShadow`)를 마무리합니다. 주 시점 그림자가 비어 있으면(게임 스레드가 배치를 만들지 않았다) 그림자 없는 뷰로 두고, 주 시점 뷰에 옮깁니다.
         * @details 라이트 버퍼 업로드보다 먼저 부른다 — 점광 · 스폿 타일 원소가 라이트 버퍼 꼬리에 붙는다.
         */
        void applyFrameShadow();
        /** @brief 뷰 하나의 방향광 그림자 상수(`g_LightViewProj` · `g_ShadowParams` · `g_ShadowCascadeLayout` · `g_ShadowCascade0..3`)를 패스 상수에 씁니다. */
        void applyViewShadowConstants( FramePassContext& context, const ViewShadowData& shadow );
        /**
         * @brief 그림자 패스 하나를 그립니다 — 지금 뷰의 캐스케이드 타일과 프레임의 점광 · 스폿 타일을 아틀라스의 자기 칸(뷰포트)에 그립니다.
         * @details 타일마다 `g_LightViewProj` 를 그 타일의 행렬로 바꿔 씬을 한 번씩 그린다. 컬링은 모든 타일을 덮는 상자(`ViewShadowData::_cullViewProj`) 하나다.
         */
        void drawShadowTiles( FramePassContext& context, bool bTransparentBatch );
        /** @brief 이번 프레임 한 뷰의 그림자 패스가 그리는 타일 수입니다(상수 버퍼 링 용량 산정). */
        uint32 computeShadowTileCountPerView() const;
        /**
         * @brief 뷰-투영 행렬과 **그 역행렬**을 함께 적용합니다.
         * @details 둘을 따로 채우면 언젠가 한쪽만 갱신된다. 그러면 디퍼드가 복원한 월드 위치가
         *          지난 프레임의 카메라를 가리키고, 증상은 "빛이 한 프레임 늦게 따라온다" 로 나타난다.
         */
        void applyViewProjection( FramePassContext& context, const float4x4& viewProj );
        /** @brief 월드 행렬을 단위 행렬로 설정합니다. */
        void setIdentityWorld( FramePassContext& context );
        /** @brief 드로우 직전에 리플렉션 기반으로 바인딩합니다. PassCB, MaterialCB, 텍스처, 인스턴스 버퍼가 대상입니다. */
        void bindForDraw( FramePassContext& context, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                          const RHIDescriptorIndex* pMaterialTexSRV = nullptr );
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* layoutForPSO( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 PSO 핸들에 연결합니다. */
        void registerPSOLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief GPUScene 인스턴스 구조 버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& context );
        /**
         * @brief 씬 라이트 구조 버퍼를 "SwLights" 이름으로 등록합니다. **모든 패스**에 등록합니다.
         * @details 인스턴스 버퍼와 달리 지오메트리 패스 전용이 아니다. 디퍼드 조명은 풀스크린
         *          패스라 `registerInstanceBuffer` 를 거치지 않는데, 라이트는 바로 거기서 필요하다.
         */
        void registerLightBuffer( FramePassContext& context );
        /** @brief 배치의 머티리얼 데이터 버퍼(GPUScene)를 패스 레지스트리에 "SwMaterials" 이름으로 등록합니다. */
        void registerMaterialBuffer( FramePassContext& context, const GPUMeshBatch& batch, RHIPipelineStateHandle pso );
        /** @brief 씬 메시를 그립니다. GPUScene 이 올라가 있으면 drawGPUBatches 로 넘기고, 아니면 상태만 맞춥니다. */
        void drawSceneMeshes( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass );
        /**
         * @brief GPUScene 배치를 간접 드로우로 그립니다.
         * @param pParticleSlot 투명 패스의 입자 자리(`findActiveParticleSlots`). 주면 배치 순서의 그 자리마다 입자를 끼우고 멀티 드로우 묶음을 그 자리에서 끊으며,
         *                      남은 입자는 모든 배치 뒤에 그린다. 투명 큐가 아닌 패스(외곽선 · 그림자 등)는 nullptr 이다.
         */
        void drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass, const vector<GPUTransparentParticleSlot>* pParticleSlot );
        /** @brief `drawGPUBatches` 가 한 패스 동안 들고 가는 값입니다 — 그릴 배치 · 입자 자리, 지금 걸린 PSO · 정점 버퍼, 입자 커서, 그린 호출 수. */
        struct BatchDrawState
        {
            vector_reference<const GPUMeshBatch>               _batches{};
            vector_reference<const GPUTransparentParticleSlot> _listParticleSlot{};
            const vector<uint32>*                              _pOrder{ nullptr }; ///< 추가 뷰의 투명 패스가 도는 배치 순서(없으면 배치 번호 순서)
            RHIPipelineStateHandle                             _pso{ 0 };
            RHIPipelineStateHandle                             _boundPSO{ 0 }; ///< 지금 커맨드 리스트에 걸린 PSO(배치마다 다를 수 있어 바뀔 때만 다시 건다)
            RHIBufferHandle                                    _boundVertexBuffer{ 0 };
            RHIBufferHandle                                    _slotStream{ 0 }; ///< 인스턴스 슬롯 스트림(슬롯 1)
            RHIBufferHandle                                    _argsBuffer{ 0 }; ///< 이 패스의 뷰가 만든 간접 인자
            RenderPassType                                     _passType{};
            uint32                                             _batchOffset{ 0 }; ///< 간접 인자 슬롯의 시작(불투명 다음 투명)
            uint32                                             _particleSlotCursor{ 0 };
            uint32                                             _drawCallCount{ 0 };
            bool                                               _bMerge{ false }; ///< 연속 배치를 멀티 드로우 한 번으로 묶는지
        };
        /** @brief 보기 모드 한 바퀴를 그립니다. 첫 바퀴(@p bDrawsParticles)만 배치 순서의 자기 자리에 입자를 끼웁니다. */
        void drawBatchRound( FramePassContext& context, BatchDrawState& inoutState, RenderViewMode drawMode, bool bDrawsParticles );
        /** @brief @p orderIndex 의 배치와 한 멀티 드로우 묶음이 되는 연속 배치 수를 셉니다(묶지 않으면 1, 다음 입자 자리가 묶음 안에 들면 그 자리에서 끊는다). */
        uint32 countDrawGroup( const BatchDrawState& state, RenderViewMode drawMode, uint32 orderIndex, bool bDrawsParticles ) const;
        /** @brief 그 PSO 의 레이아웃이 머티리얼 CB 슬롯을 거는지입니다. 거는 셰이더에서만 머티리얼 CB 가 멀티 드로우 병합 키다. */
        bool layoutBindsMaterialCb( RHIPipelineStateHandle batchPSO ) const;
        /** @brief @p other 가 @p head 와 같은 멀티 드로우 묶음에 들 수 있는지입니다(그릴 수 있고 정점 버퍼 · PSO · 머티리얼 바인딩이 같다). */
        bool isSameDrawGroup( RHIPipelineStateHandle pso, RenderPassType passType, RenderViewMode drawMode, const GPUMeshBatch& head, const GPUMeshBatch& other ) const;
        /**
         * @brief 묶음 하나를 그립니다 — 바뀐 정점 버퍼 · 배치 PSO 만 다시 걸고, 머티리얼 바인딩 뒤 간접 인자 @p argIndex 부터 @p groupCount 개를 부릅니다.
         * @return 그렸으면 true 입니다. 그 보기 모드의 머티리얼 변형이 없으면(만들 때 오류를 남겼다) 그리지 않고 false 입니다.
         */
        bool drawBatchGroup( FramePassContext& context, RHIPipelineStateHandle pso, RenderViewMode drawMode, const GPUMeshBatch& head, RHIPipelineStateHandle& ioBoundPSO,
                             RHIBufferHandle& ioBoundVertexBuffer, RHIBufferHandle argsBuffer, uint32 argIndex, uint32 groupCount );
        /** @brief 투명 큐(메시 투명 배치와 입자 그리기를 한 순서로)를 지금 열린 투명 패스에 그립니다. */
        void drawTransparentQueue( FramePassContext& context, RHIPipelineStateHandle pso );
        /** @brief 지금 그리는 뷰의 입자 자리입니다. 추가 뷰는 자기 순서가 있으면 그것, 없으면 주 시점의 것(그 뷰도 주 배치 순서로 그린다)입니다. */
        const vector<GPUTransparentParticleSlot>& findActiveParticleSlots() const;
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso );
        /**
         * @brief 입자 그리기 구간 @p listSlot 을 지금 열린 투명 패스에 그 순서로 그립니다.
         * @details 입자 PSO(`ensureParticlePSO`)를 걸고 PassCB 를 바인딩한 뒤 `VFXParticleRenderer::drawBatches` 로 넘긴다. PSO · 정점 버퍼 슬롯을 바꾸므로
         *          메시 드로우로 돌아가는 쪽(`drawGPUBatches`)이 그 상태를 다시 건다.
         * @return 입자 상태를 걸었으면 true 입니다(그릴 것이 없거나 PSO 가 없으면 false — 상태도 그대로다)
         */
        bool drawParticleSlots( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot );
        /**
         * @brief @p listSlot 의 @p inOutCursor 부터 자리가 @p position 이하인 입자를 그리고 커서를 그 뒤로 옮깁니다. 메시 드로우로 돌아갈 패스 상태(인스턴스 슬롯
         *        스트림 @p slotStream, 에뮬 텍스처 슬롯)를 다시 겁니다.
         * @return 입자 상태를 걸었으면 true 입니다 — 부르는 쪽은 자기가 기억한 PSO · 정점 버퍼를 잊어야 한다
         */
        bool drawParticleRun( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot, uint32& inOutCursor, uint32 position,
                              RHIBufferHandle slotStream );
        /** @brief 뷰의 풀에서 트랜지언트 텍스처를 할당합니다. */
        void allocateTransient( TransientAttachmentPool& pool, string_view name, RHIFormat format, bool bDepth, const float4& clearColor,
                                uint32 resolutionDivisor = 1 );
        /** @brief 컬러(및 깊이) 패스를 시작합니다. 열지 못하면 false 를 반환하며, 그때는 그리지도 닫지도 않습니다(`beginColorPassMrt` 도 같습니다). */
        bool beginColorPass( FramePassContext& context, string_view colorName, string_view depthName, const float4& clearColor,
                             RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief MRT 컬러 패스를 시작합니다.
         * @return 열었으면 true 입니다. 컬러 타깃 이름 중 이번 프레임에 없는 것이 있으면 **열지 않고** false 를 반환하며 오류를 한 번 남깁니다.
         *         없는 첨부의 핸들 0 은 백버퍼를 가리키므로, 그대로 열면 패스가 화면에 그립니다.
         */
        bool beginColorPassMrt( FramePassContext& context, const string_view* pColorNames, const float4* pTargetClearColor,
                                const RHIRenderPassLoadOp* pColorLoad, uint32 colorCount, string_view depthName,
                                RHIRenderPassLoadOp depthLoad );
        /** @brief 깊이 전용 패스를 시작합니다. */
        void beginDepthOnlyPass( FramePassContext& context, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief 트랜지언트 텍스처를 리소스 레지스트리에 canonicalName 으로 등록합니다(bindless SRV 자동 매칭).
         * @param canonicalName **미리 intern 해 둔** 이름입니다. 문자열을 받으면 패스마다 다시 intern 하게 되므로
         *        attachmentNames() 의 캐시를 넘기세요.
         */
        void registerPassTexture( FramePassContext& context, const hashed_string& canonicalName, string_view attachmentName );
        /** @brief bindless 텍스처 바인딩을 커밋합니다. PassConstantValues 를 갱신하고, 에뮬레이션 백엔드에는 폴백 바인딩을 합니다. */
        void commitBindlessTextureBindings( FramePassContext& context );
        /** @brief 새로 칸을 받은 쿠키를 아틀라스 칸에 그립니다(프레임 프리패스 리스트, `submitGraph`). 그릴 칸이 없으면 아무것도 하지 않습니다. */
        void drawLight2DCookieAtlas();

        // ------------------------------------------------------------------------------
        // 5) 커맨드 리스트와 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 함수입니다. commandList 를 준비하고 device 에 연결합니다.
         * @param pCallerName 오류 로그에 찍을 호출자 이름
         * @return 성공하면 true 입니다. false 면 호출자가 바로 반환해야 합니다.
         */
        [[nodiscard]] bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 함수입니다. graph 를 실행하고 commandList 를 제출합니다.
         * @return graph.execute() 의 결과
         */
        [[nodiscard]] bool submitGraph( IRHIDevice* pDevice );
        /** @brief 이번 프레임에 그릴 추가 뷰 가운데 출력 종류가 @p outputKind 인 것을 그리고 그린 수를 돌려줍니다. */
        uint32 renderExtraViewsOfKind( IRHIDevice* pDevice, RenderViewOutputKind outputKind );
        /** @brief 주 시점의 그래프를 실행합니다. 병렬 기록이 되면 레벨마다 여러 리스트로, 아니면 주 리스트 하나로 직렬 기록해 제출합니다. */
        [[nodiscard]] bool executeMainGraph( IRHIDevice* pDevice );

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
         * @details 라이트와 그림자 타일은 앞서 `GPULightBuffer::populate` 로 담겨 있어야 한다. 뷰의 그리드 번호는 `ViewTarget::_lightGridIndex` 에 남는다.
         *          GPU 생산자면 그리드 채우기는 커맨드 리스트가 열린 뒤 `dispatchLightGrid` 가 한다.
         */
        void updateLightBuffer( IRHIDevice* pDevice );
        /**
         * @brief 이번 프레임의 라이트 그리드 생산자입니다. 개발 빌드는 `gv_lightCulling`(None · CPU · GPU), Shipping 은 GPU 로 고정입니다.
         * @details GPU 를 골랐는데 컴퓨트 PSO 가 없으면 오류를 한 번 남기고 그리드 없이 그린다 — 채우지 않은 그리드 버퍼를 셰이더가 읽게 두지 않는다.
         */
        LightGridProducer selectLightGridProducer();

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        [[nodiscard]] bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;
        /**
         * @brief 패스 타깃의 클리어 색입니다. 파이프라인 XML 의 첨부 선언(풀이 든 값)이 원본이고, 풀에 없는 이름(백버퍼)은 렌더러의 클리어 색입니다.
         * @details 호출부가 기본값을 넘기지 않는다 — 패스마다 적던 기본값이 선언과 갈라지는 길을 형식으로 막는다.
         */
        float4 getTargetClearColor( string_view attachmentName ) const;
        /**
         * @brief 트랜지언트 텍스처와 그 SRV 를 한 번의 조회로 찾습니다. 없으면 빈 값입니다.
         * @details 이름 하나로 둘 다 필요한 곳(registerPassTexture)이 패스마다 여러 번 돈다. 조회를 한 번만 하므로 해시도 한 번이다.
         */
        TransientAttachmentPool::Attachment findTransientAttachment( string_view name ) const;
        /** @brief 트랜지언트 텍스처 핸들을 찾습니다. 없으면 0 입니다. */
        RHITextureHandle findTransient( string_view name ) const;
        /**
         * @brief Present 소스 어태치먼트 이름을 정합니다. 없으면 빈 뷰입니다.
         * @details 반환한 뷰는 파이프라인 선언(`_listResolvedInput`)이나 폴백 리터럴을 가리킨다. 매 프레임 불리므로 사본을 만들지 않는다.
         *          파이프라인을 바꾸기 전까지만 유효하므로 프레임을 넘겨 보관하지 않는다.
         */
        string_view resolvePresentSource() const;
        /**
         * @brief 지금 뷰의 주 출력 대상을 고릅니다. 렌더 텍스처 뷰는 자기 텍스처를, 주 시점과 화면 사각형 뷰는 주 출력(백버퍼 또는 게임 뷰 RT)을 씁니다.
         *        스크린샷 실행이면 캡처 텍스처를 씁니다. Present 와 Canvas 가 같은 판단을 씁니다.
         */
        PresentTarget resolvePresentTarget() const;
        /** @brief 이 패스가 Swapchain 을 쓰는 마지막 패스면 true 입니다. 스크린샷 캡처를 백버퍼로 복사하는 일을 이 패스 끝에서 합니다. */
        bool isLastSwapchainWriter( const RenderGraphPassDesc& passDesc ) const;
        /** @brief 패스 타입으로 파이프라인의 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;
        /**
         * @brief HDR 색을 톤맵하는 첫 패스입니다(Tonemap, 또는 퍼뮤테이션 `SW_POST_TONEMAP` 을 켠 후처리 체인 Present — `passAppliesTonemap`). 없으면(2D 파이프라인) nullptr 입니다.
         * @details 자동 노출은 이 패스의 원본을 재고 이 패스 바로 앞에 기록한다(뷰마다 프레임에 한 번). 포인터를 들고 있지 않고 지금 파이프라인에서 찾는다 —
         *          파이프라인을 다시 읽으면 패스 목록이 바뀌는데, 트랜지언트 준비가 콜백 바인딩보다 먼저라 들고 있던 포인터는 옛 목록을 가리킨다.
         */
        const RenderGraphPassDesc* findTonemapPass() const;

        /**
         * @brief 패스 종류 테이블(RenderPassTypeInfo)과 파이프라인 XML 의 패스 서술로 패스 PSO 를 만듭니다.
         * @param pRTVFormatOverride 컬러 RT 포맷을 이 배열로 고정합니다(Present 변형). nullptr 이면 테이블의 고정 포맷을 쓰고, 없으면 출력 선언 순서를 따릅니다.
         */
        RHIPipelineStateHandle createPSOForPassType( RenderPassType passType, const RHIFormat* pRTVFormatOverride = nullptr );
        /** @brief `createPSOForPassType` 의 몸체입니다. 패스 서술을 직접 받습니다 — 같은 종류의 패스가 여럿인 모듈 패스가 패스마다 부릅니다. */
        RHIPipelineStateHandle createPSOForPass( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride );
        /**
         * @brief 모듈 확장 패스의 프로그램(`ExtensionProgramDesc`) PSO 를 셋업에서 찾거나 만듭니다(키 = 셰이더 · 고정 상태 · 타깃 포맷). 기록 중에는 부르지 않습니다.
         * @return 0 이면 만들지 못한 것입니다(오류를 남겼다 — 부르는 쪽은 그 패스를 그리지 않는다).
         */
        RHIPipelineStateHandle findOrCreateExtensionProgramPSO( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest );

        // --- 모듈 확장(ExtensionRenderPass M3 · M4) — 구현은 FrameRendererModule.cpp ---
        /**
         * @brief 모듈 구간의 첫 일입니다(`uploadSceneAndSubmit`, `RenderScope` 안). 대기 중인 프록시 갱신을 적용하고, 확장의 `addPasses` 와 프록시의 `collectDrawElements` 를 부르고,
         *        패스 선언을 검사해 프로그램 PSO · 임시 텍스처를 기록 전에 만들고, 선언이 바뀌었으면 그래프를 다시 묶게 합니다.
         */
        void collectExtensionFrame( RenderExtensionRegistry& registry );
        /** @brief 모듈 구간의 끝입니다. 모듈이 넣은 패스 콜백 · 요소를 모두 놓습니다(람다의 소멸 코드가 모듈에 있어 모듈 구간 안에서 놓아야 한다). */
        void releaseExtensionFrame();
        /** @brief 이 뷰의 풀에 이번 프레임에 요청된 모듈 임시 텍스처를 만듭니다(이미 있으면 그대로). */
        void allocateExtensionTextures( ViewTarget& view );
        /** @brief 요소가 쓰는 메시를 찾거나 읽고 GPU 에 올립니다. 기록 전에만 부릅니다. 얻지 못하면 nullptr 입니다(이름마다 한 번 오류를 남겼다). */
        Mesh* resolveExtensionMesh( RenderExtensionRegistry& registry, const hashed_string& meshID );
        /** @brief 모듈 확장 패스 하나를 실행합니다(그래프 노드 콜백이 부른다). 모듈의 실행 콜백에 `ExtensionPassContext` 를 넘깁니다. */
        void executeExtensionPass( FramePassContext& context, const ExtensionFramePass& pass );
        /** @brief `ExtensionPassContext::drawFullscreen` 의 몸체입니다. */
        bool drawModuleFullscreen( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant );
        /** @brief `ExtensionPassContext::drawProxyElements` 의 몸체입니다. */
        bool drawExtensionElements( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant );

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 **기록 시작 전에** 모두 만들어 둡니다.
         * @details 기록 중에는 PSO 를 만들 수 없다. 상수 버퍼 용량을 미리 늘리는 것과 같은 이유다. 패스는
         *          병렬로 기록되므로 그때 만들면 백엔드마다 다른 방식으로 깨진다.
         */
        void ensureMaterialPSOS();
        /**
         * @brief 고정한 머티리얼(`MaterialCache::pinAsset` — 프리로드 세트)의 PSO 를 **기록 시작 전에** 미리 만듭니다(계획 PreloadAndLOD P6). 주 렌더러만 합니다.
         * @details 씬에 든 머티리얼은 `ensureMaterialPSOS` 가 배치마다(보이든 아니든) 만들므로 대기 뷰에 보일 것은 이미 준비돼 있다. 아직 씬에 없는 머티리얼(다음 레벨 · 컷에
         *          나올 것)만 이 길로 데운다 — 그 머티리얼이 씬에 들어오는 프레임에 DX12 PSO 생성 히치가 없게. 이번 프레임 보기 모드의 불투명 또는 투명 패스마다, 등장 디더
         *          변형까지 만든다.
         */
        void warmPinnedMaterialPSOS();
        /**
         * @brief 입자 그리기가 있는 프레임에 입자 PSO 를 **기록 시작 전에** 만들어 둡니다(머티리얼 변형 캐시에 둔다 — 해제 순서가 같다).
         * @details 투명 패스 PSO 의 렌더 상태(블렌드 · 깊이 테스트 · RT 포맷)를 물려받고 셰이더만 `EngineDefaultAssets::_shaderParticle` 로 바꾼다. 컬링은 끄고
         *          깊이 쓰기도 끄며, 블렌드는 프리멀티플라이로 고정한다(더하기는 셰이더가 알파 0 으로 낸다). define 은 비운다 — 쿠커가 VSMain · PSMain 을 define 없이 베이크한다.
         */
        void ensureParticlePSO();
        /** @brief 입자 PSO 입니다. 투명 패스 PSO 가 없거나 만들지 못했으면 0 입니다(`ensureParticlePSO` 가 이번 프레임에 만들었어야 한다). */
        RHIPipelineStateHandle findParticlePSO() const;
        /**
         * @brief 패스 PSO 에 머티리얼 퍼뮤테이션, 뷰 모드, 컬 반전을 적용한 변형을 만듭니다. 적용할 것이 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 렌더 상태(블렌드, 뎁스, RT 포맷)는 **패스의 것을 그대로 물려받고** 셰이더만 교체한다.
         *          이 상태는 머티리얼이 아니라 패스가 정한다. 예외는 컬 모드 하나다. `bReverseCulling` 이면
         *          Back 과 Front 를 맞바꾼다(거울 변환이 감김 방향을 뒤집기 때문이다). None 은 그대로 둔다.
         */
        RenderPSOCache::MaterialPSOEntry createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType,
                                                                   const GPUShaderPermutation* pPermutation, RenderViewMode viewMode,
                                                                   bool bReverseCulling, RHIPipelineStateDesc* pOutDeferredDesc );
        /**
         * @brief 머티리얼 변형을 빌더 스레드가 만들게 하는지 반환합니다(`-gv_psoAsync` 가 켜져 있고 백엔드가 `_bThreadSafeResourceCreation` 이면 true).
         * @details false 이면 렌더 스레드가 같은 줄을 셋업 구간에서 직접 만듭니다(GL 은 프레임당 시간 예산, `-gv_psoAsync=0` 은 전부 — A/B 측정용).
         */
        [[nodiscard]] bool usesAsyncMaterialPSOBuilder() const;
        /**
         * @brief 빌더가 끝낸 머티리얼 변형을 캐시에 합치고(스레드를 쓰지 않으면 이 자리에서 만들고), 새로 `Ready` 가 된 것이 있으면 폴백 버퍼를 점검합니다.
         * @details 렌더 스레드의 셋업 구간(기록 전)에서만 부릅니다 — 캐시를 쓰는 스레드가 렌더 스레드 하나라 "삽입은 기록 시작 전에만" 이 선다.
         */
        void pumpMaterialPSOBuilds();
        /**
         * @brief 이번 프레임 배치 수에 맞춰 상수 버퍼 슬롯을 **기록 시작 전에** 늘려 둡니다. 드로우마다 슬롯을 하나씩 쓰기 때문입니다.
         * @details 기록 중에는 버퍼 생성과 bindless 등록을 할 수 없다. 지난 프레임의 최대 사용량(하이워터)도 최솟값으로 본다.
         */
        void ensurePassCbCapacityForFrame();
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다(셋업 전용).
         * @details 기록 중에는 만들 수 없다. 버퍼 생성과 `registerBindlessBufferSRV` 는 bindless 레지스트리를 바꾸는데,
         *          패스 콜백은 태스크 워커에서 병렬로 돈다(`requireRegistryMutableNow` 가 감시하는 규칙).
         *          그래서 PSO 를 모두 등록한 뒤 여기서 한 번에 만든다.
         */
        void ensureMaterialFallbackBuffers();
        /**
         * @brief 주 출력에 그리는 패스(Present, Canvas)의 PSO 를 **대상 포맷별로** 찾습니다.
         * @details 주 출력의 대상은 두 가지다. 하나는 백버퍼로, 포맷은 디바이스가 실제로 채택한 값이다(Vulkan 은 서피스가 B8G8R8A8 만 제공할 수 있다).
         *          다른 하나는 에디터 씬 뷰와 게임 뷰 RT(R8G8B8A8)다. PSO 의 렌더 타깃 포맷이 대상과
         *          다르면 Vulkan 은 렌더 패스 비호환으로 판단해 검증 레이어가 매 프레임 오류를 낸다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같은 방식이다.
         *          주 출력에 그리는 패스만 포맷이 키이므로 (패스, 포맷) 맵 하나로 충분하다.
         *          **조회만 한다.** 패스 실행(태스크 워커) 중에 불리므로 없다고 만들면 안 된다. 셋업에 없는 포맷이면 대신 그리지 않고 멈춘다(`SW_FATAL`) —
         *          돌려준 PSO 는 0 이 아니다. 포맷을 모르는 대상(`RHIFormat::Unknown`)은 셋업이 만든 그 패스의 엔진 PSO 다.
         */
        RHIPipelineStateHandle findOutputPSO( RenderPassType passType, RHIFormat targetFormat );
        /** @brief 주 출력에 그리는 패스(Present, Canvas, RCAS)가 그릴 수 있는 대상 포맷(백버퍼, 오프스크린, 캡처)의 PSO 를 셋업에서 미리 만듭니다. 하나라도 못 만들면 멈춥니다. */
        void createOutputPSOVariants();

    private:
        IRHIDevice* _pDevice;
        /**
         * @brief 렌더 패스와 파이프라인 에셋 캐시입니다. `initialize` 뒤에만 있고 `shutdown` 이 비웁니다.
         * @details 언리얼의 RHI 가 렌더 패스 *에셋*을 모르듯, 소유는 렌더러가 한다. 디바이스 추상(RHI)이 이것을
         *          가지면 RHI 가 Renderer 를 include 하게 된다.
         */
        unique_ptr<RenderPipelineAssetCache> _renderPipelineAssetCache;
        IRHIDevice*                          _pCmdOwnerDevice;
        unique_ptr<IRHICommandList>          _frameCmd;
        IRHICommandList*                     _pCmd;
        TaskManager*                         _pTaskManager;
        GPUScene                             _gpuScene;
        /// @brief 씬 직접 경로(`execute( scene )`, 에디터와 테스트)가 쓰는 빌더입니다. 패킷 경로에서는 Engine 의 빌더를 씁니다.
        GPUSceneBuilder     _sceneBuilder;
        RenderPipelineAsset _pipelineResource;
        RenderGraph         _graph;
        string              _pipelinePath;
        float4              _clearColor;
        /// @brief 주 시점입니다. 파이프라인이 선언한 첨부(창 크기로 만들고 구성이 바뀔 때만 다시 만든다), TAA 히스토리, 직렬 리스트, 출력 사각형을 가집니다.
        ViewTarget _mainView;
        /// @brief 추가 뷰들입니다. 카메라 id 순서가 아니라 처음 본 순서로 쌓습니다. 뷰마다 자기 풀과 컬링 슬롯을 가집니다.
        vector<unique_ptr<ViewTarget>> _listExtraView;
        /// @brief 지금 그리는 뷰입니다. 프레임 밖에서는 항상 `_mainView` 입니다.
        ViewTarget* _pActiveView;
        /**
         * @brief 프레임 단위 패스 상태입니다. 직렬 경로에서 쓰며 병렬 패스의 시드가 됩니다.
         * @details 병렬 기록에서는 패스마다 이것을 복사해 각자의 커맨드 리스트와 상수 버퍼를 붙인다.
         */
        FramePassContext _frameContext;
        /**
         * @brief 패스 슬롯별 컨텍스트입니다. 프레임마다 `_frameContext` 를 **대입**해 씁니다. 용량이 남아 있어 힙을 건드리지 않습니다.
         * @details 패스마다 지역 복사본을 만들면 상수 값 목록과 레지스트리 맵이 프레임마다 패스 수만큼 새로
         *          늘어난다. 크기는 병렬 기록 **전**(`submitGraph`)에 맞춘다. 기록 중에 늘리면 워커끼리 경합한다.
         *          마지막 슬롯은 이름을 찾지 못한 패스가 쓴다.
         */
        vector<FramePassContext> _listPassContext;
        /// @brief 씬 직접 경로가 내보내는 스냅샷입니다. 맞바꾸기(swap)로 저장소를 돌려받습니다.
        GPUSceneSnapshot _sceneSnapshotScratch;
        /// @brief 주 시점의 패스 시드입니다. 추가 뷰는 이것에서 출발해 자기 값(뷰 행렬, 플래그, 컬링 슬롯)만 덮어씁니다. 프레임마다 대입하므로 용량이 남습니다.
        FramePassContext _mainSeedScratch;
        /// @brief 씬 직접 경로(`execute( scene )`)가 추가 뷰를 고르는 스케줄러와 요청 목록입니다. 패킷 경로에서는 Engine 의 것을 씁니다.
        RenderViewScheduler       _directViewScheduler;
        vector<RenderViewRequest> _listDirectViewScratch;
        /** @brief 이번 프레임 배치가 쓰는 PSO 변형 하나의 조건(퍼뮤테이션, 컬 반전)입니다. 퍼뮤테이션이 없으면 kInvalidShaderPermutation 입니다. */
        struct MaterialPSORequest
        {
            uint32 _shaderPermutation{ kInvalidShaderPermutation };
            uint8  _bReverseCulling{ SW_FALSE };
        };
        /// @brief `ensureMaterialPSOS` 가 이번 프레임 배치에서 모으는 변형 조건입니다. 중복이 없고, 프레임마다 다시 채웁니다.
        vector<MaterialPSORequest> _listOpaquePSORequestScratch;
        vector<MaterialPSORequest> _listTransparentPSORequestScratch;
        /// @brief `warmPinnedMaterialPSOS` 가 줄에서 꺼낸 머티리얼입니다. 프레임마다 비운다(저장소만 재사용한다).
        vector<shared_ptr<Material>> _listPSOWarmMaterialScratch;
        /** @brief 이번 프레임에 그리는 뷰들의 보기 모드를 비트 집합으로 모읍니다(주 시점은 늘 들어갑니다). */
        uint32 collectFrameViewModeMask() const;
        /** @brief 배치 목록에서 (퍼뮤테이션, 컬 반전) 조합을 중복 없이 모읍니다. 퍼뮤테이션이 없는 배치도 넣습니다. */
        static void collectMaterialPSORequests( vector_reference<const GPUMeshBatch> listBatch, vector<MaterialPSORequest>& outListRequest );
        /** @brief 요청 하나에 대해 이번 프레임의 보기 모드마다 변형을 만들어 둡니다. @return 새 변형을 하나라도 만들었으면 true 입니다. */
        [[nodiscard]] bool ensureRequestMaterialPSOS( RHIPipelineStateHandle passPSO, RenderPassType passType, const MaterialPSORequest& request, uint32 viewModeMask );
        /**
         * @brief 변형 하나가 캐시에 없으면 만들어 넣습니다. 셰이더를 컴파일해야 하는 변형은 `Building` 으로 올리고 빌더 줄에 넣습니다(`pumpMaterialPSOBuilds` 가 합칩니다).
         * @param bUrgent 이번 프레임 배치가 필요한 키면 true, 미리 만들기(`warmPinnedMaterialPSOS`)는 false 입니다.
         * @return 이 호출에서 `Ready` · `Failed` 로 끝난 변형이 있으면 true 입니다(`Building` 으로 넘긴 것은 false).
         */
        [[nodiscard]] bool ensureMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation, uint64 permutationHash,
                                                     bool bReverseCulling, RenderViewMode viewMode, bool bUrgent = true );
        /**
         * @brief 패스 PSO 의 컬러 RT 수와 포맷을 정합니다. 고정 배열이 있으면 그것을, 없으면 출력 선언 순서를 씁니다.
         * @return 컬러 RT 가 있는데 포맷을 정할 근거가 없거나(둘 다 없음) 출력 선언에 포맷을 모르는 출력(Swapchain)이 있으면 false 입니다 — RGBA8 로 짐작하지 않는다.
         */
        [[nodiscard]] bool fillPassRenderTargetFormats( RHIPipelineStateDesc& desc, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormats, uint32 numRenderTargets ) const;
        /** @brief 이 패스 종류의 컬러 포맷을 정할 근거(표의 고정 포맷 · 컬러 없음 · 파이프라인의 출력 선언)가 있는지 반환합니다. 없으면 이 파이프라인에서 실행되지 않는 패스입니다. */
        bool hasRenderTargetFormatSource( RenderPassType passType ) const;
        /// @brief 배치 하나가 한 프레임에 몇 개의 지오메트리 패스(그림자, 프리패스, 불투명, 반투명)에서 그려지는지의 어림값입니다.
        static constexpr uint32 _s_kDrawCbPassEstimate = 4;
        /// @brief 패스와 드로우별 상수 버퍼 슬롯 링입니다. 병렬 기록에서 드로우마다 하나씩 가져갑니다.
        PassConstantRing _passCbRing;
        /**
         * @brief 이번 프레임의 뷰들입니다. 행렬, 절두체, 상수 버퍼를 각자 소유합니다.
         * @details 뷰를 얻으면 그 뷰의 값이 함께 따라온다. 주의: 이 값들을 뷰 밖 멤버로 흩어 두면 "이 값은 어느 뷰
         *          것인가" 를 사람이 기억해야 하고, 컬링 상수 버퍼를 뷰끼리 나눠 쓰게 되어 뒤 업로드가 앞 디스패치를 덮어쓴다.
         */
        RenderView _arrView[static_cast<uint32>( RenderViewType::Count )];
        /**
         * @brief 주 시점 방향광 캐스케이드마다의 그림자 컬링 입력(절두체 · 자기 컬링 상수 버퍼)입니다. 앞 `_cascadeCullCount` 개만 씁니다.
         * @details 컬링 슬롯은 추가 뷰 슬롯 뒤에 붙는다(`_cascadeCullSlotBase` + 캐스케이드 번호). 추가 뷰(CCTV · PiP)는 작은 장면이라 합친 칸 하나를 그대로 쓴다.
         */
        RenderView _arrCascadeCullInput[shaderslot::kShadowCascadeMax];
        uint32     _cascadeCullSlotBase{ 0 }; ///< 캐스케이드 컬링 슬롯의 첫 번호
        uint32     _cascadeCullCount{ 0 };    ///< 이번 프레임 캐스케이드 컬링 슬롯 수. 0 이면 합친 칸(`_shadowCullSlot`) 하나로 모든 타일을 거른다

        /// @brief 정점 변형(인스턴스 애니메이션, 모프, 스킨, VAT) 컴퓨트의 상수 버퍼, 풀, 스크래치, 시계입니다.
        FrameMeshDeformState _deform;
#if !defined( SW_SHIPPING )
        /// @brief `setDebugOverrides` 로 받은 값입니다. 음수인 칸은 전역 변수(시각은 `_deform._animationTimer`)를 따릅니다.
        FrameDebugOverrides _debugOverrides;
#endif
        /// @brief 이번 프레임의 씬 간접 드로우 호출 수입니다. 패스를 병렬로 기록하므로 atomic 으로 둡니다.
        atomic<uint32> _indirectDrawCallCount;
        /**
         * @brief 패스별 GPU 시간입니다. 지난 프레임의 타임스탬프를 읽어 프로파일러(`GPU.<패스>`)와 Tracy GPU 타임라인으로 냅니다.
         * @details 프로파일러는 `initialize` 가 주입한다. 타임스탬프를 쓸지(`isRecording`)도 여기에 묻는다.
         *          GPU 타임스탬프가 없으면 GPU 비용을 `RT.BeginFrame`(백프레셔) 같은 대리값으로 추측하거나 패스를 하나씩 지워 가며
         *          차이로 구해야 한다. 그런 추측(클리어, 포맷 비용 등)은 쉽게 틀린다.
         */
        GPUPassTimeline _gpuPassTimeline;
        /// @brief 마지막 프레임의 값입니다(getLastIndirectDrawCallCount).
        uint32 _lastIndirectDrawCallCount;
        /// @brief `setInputRoleEnabled( role, false )` 가 켠 비트입니다. 그 역할의 입력은 바인딩하지 않습니다.
        uint32 _disabledInputRoleMask;
        /// @brief 씬 라이트 구조 버퍼입니다. RT 가 소유하며 포워드와 디퍼드가 같은 버퍼를 읽습니다.
        GPULightBuffer   _lightBuffer;
        vector<float4x4> _listScratchGridViewProj;     ///< 라이트 그리드를 만들 뷰의 뷰-투영(프레임마다 다시 쓴다)
        vector<uint8>    _listScratchNeededAttachment; ///< 뷰의 패스를 고를 때 선언한 첨부 자리마다 "이번 프레임에 읽힌다"(`applyViewPassSelection`, 뷰마다 다시 쓴다)
        /// @brief GPU 방출기의 파티클 라이트 슬롯 범위입니다. 라이트 버퍼의 GPU 구간 크기를 정하는 데만 쓴다(범위는 시뮬레이션이 같은 함수로 다시 얻는다).
        vector<VFXGPULightRange> _listScratchGPULightRange;
        /// @brief 씬 직접 경로에서 라이트를 모으는 버퍼입니다. 프레임마다 할당하지 않으려고 보관합니다.
        vector<GPULight> _listScratchLight;
        /// @brief 씬 직접 경로에서 라이트 프로브 SH9 원소를 모으는 버퍼입니다(`collectSceneLights`). 프레임마다 재사용합니다.
        vector<float4>         _listScratchLightProbe;
        vector<GPULightCookie> _listScratchLightCookie; ///< 직접 경로의 쿠키 목록(`collectSceneLights`)
        /// @brief 실시간 프로브 갱신 요청입니다 - 직접 경로가 모으는 임시 버퍼(`_listScratchRealtimeProbe`)와, 이번 프레임 기록이 읽는 사본(`_listFrameRealtimeProbe` - 패킷 경로도 여기에 받는다).
        vector<RealtimeProbeRequest> _listScratchRealtimeProbe;
        vector<RealtimeProbeRequest> _listFrameRealtimeProbe;
        RealtimeProbeUpdater         _probeUpdater;   ///< 실시간 프로브 갱신의 버퍼와 컴퓨트 기록(`prepareRealtimeProbes` · `dispatchRealtimeProbes`)
        SkyGradient                  _frameSkySource; ///< 이번 프레임 하늘의 원본 값입니다(실시간 프로브 갱신이 하늘 SH 를 투영한다). `_bHasFrameSky` 가 0 이면 기본값
        /** @brief 2D 빛 쿠키 아틀라스입니다(칸 나누기 · 쿠키 텍스처 수명 · 아틀라스 렌더 타깃). */
        Light2DCookieAtlas _cookieAtlas;
        /// @brief 씬 직접 경로에서 그림자를 켠 점광 · 스폿의 라이트 원소 번호입니다. 프레임마다 재사용합니다.
        vector<uint32> _listScratchShadowLightIndex;
        /**
         * @brief 이번 프레임의 그림자 배치(아틀라스 칸 · 주 시점 캐스케이드 · 점광 · 스폿 타일)입니다.
         * @details 패킷 경로는 패킷과 맞바꾸기로 받고, 씬 직접 경로는 여기에 바로 채운다. 기록 중에는 읽기만 한다.
         */
        FrameShadowData _frameShadow;

        /** @brief 뷰 하나를 얻습니다. 그 뷰의 행렬, 절두체, 상수 버퍼가 함께 옵니다. */
        RenderView&       view( RenderViewType type ) { return _arrView[static_cast<uint32>( type )]; }
        const RenderView& view( RenderViewType type ) const { return _arrView[static_cast<uint32>( type )]; }

        /** @brief 이번 프레임에 컴퓨트로 드로우 커맨드를 만들 예정인지 반환합니다. 업로드 전에 GPUScene 에 알리는 데 씁니다. */
        bool usesGPUGeneratedCommands() const;

        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다(instanceanimation.hlsl).
         * @details **컬링보다 먼저** 돌아야 한다. 순서가 뒤집히면 컬링이 이번 프레임에 움직이기 전의
         *          바운드로 판정한다. 회전을 요청한 인스턴스가 없으면 통째로 건너뛴다.
         */
        void dispatchInstanceAnimation( uint32 instanceCount );
        /**
         * @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트에서 결과로).
         * @details **컬링보다 앞**이어야 한다. 모프는 회전과 달리 실제로 형태와 바운드를 바꾸므로,
         *          뒤에 두면 컬링이 한 프레임 늦은 형태로 판정한다.
         */
        void dispatchMeshMorph();
        /**
         * @brief 스킨드 메시들의 정점을 GPU 가 본 팔레트로 블렌딩합니다(바인드 포즈에서 모프 풀의 스킨 구간으로). 모프와 같이 **컬링보다 앞**이어야 합니다.
         * @details 디스패치 하나가 모든 스킨드 메시를 처리한다. 정점마다의 팔레트 행 번호에 메시의 팔레트 시작이 이미 더해져 있다.
         */
        void dispatchMeshSkin();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 기록합니다. **업로드 전에** 부릅니다.
         * @details 오프셋은 배치 테이블(g_SwBatches)에 실려 upload() 가 올리므로 테이블은 업로드 시점에 완성되어 있어야 한다. 디스패치(dispatchMeshMorph)는 커맨드 리스트가 열린 뒤 따로 돈다.
         */
        void prepareMeshMorphPool();
        /** @brief 지금 적용되는 모프 진단 모드입니다. 오버라이드가 있으면 그 값을, 없으면 `gv_morphDiag` 를 씁니다. */
        MeshMorphDiagMode getEffectiveMeshMorphDiag() const;
        /** @brief `gv_morphDiag` 가 모드 열거자인지 봅니다. 에디터 · 콘솔이 숫자로 넣은 범위 밖 값이면 오류를 남기고 false 입니다(`initialize` 가 멈춘다). */
        [[nodiscard]] static bool isMeshMorphDiagValid();
        /** @brief 시간으로 구동하는 컴퓨트가 읽을 절대 시간(초)입니다. `setAnimationTimeOverride` 로 받은 값(0 이상, 배포본에는 없음)을 쓰고, 없으면 `_animationTimer` 의 누적 시간을 씁니다. */
        float32 getAnimationTime() const;
        /** @brief 씬 배치를 멀티 드로우로 묶을지 반환합니다. `setDrawMergeEnabled` 로 받은 값을 쓰고, 없으면 전역 `gv_drawMerge` 를 씁니다. */
        bool isDrawMergeEnabled() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpucull/instancesort.hlsl).
         * @details 컬링 결과는 절두체에 종속되므로 뷰(메인, 그림자)마다 자기 인자와 목록을 따로 만든다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /**
         * @brief GPU 생산자로 뷰마다의 라이트 그리드를 채웁니다(lightgrid.hlsl, 그룹 하나 = 뷰 하나). 생산자가 GPU 가 아니면 아무것도 하지 않습니다.
         * @details 그리기 전에, 그래프 밖의 컴퓨트 프리패스로 기록한다(렌더 그래프에 컴퓨트 패스 상태가 없다). 끝나면 그리드 버퍼를 SRV 로 돌린다.
         */
        void dispatchLightGrid();
        /**
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 디스패치를 돌렸으면 true 입니다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const ViewTarget* pExtraView );
        /** @brief 이번 프레임의 LOD 전환 진행량(`_lodFadeStep`)을 렌더 프레임 간격에서 정합니다. 컬링 디스패치 앞에서 한 번 부른다. */
        void computeLODFadeStep();
        /**
         * @brief 머티리얼이 없는 배치에 바인딩하는 원소 하나짜리 구조 버퍼입니다. **내용이 다른 원소마다 하나**씩 있습니다.
         * @details DX12 에서 아무것도 바인딩하지 않은 t5 를 읽으면 GPU 폴트(디바이스 제거)가 난다. 어떤 드로우도 빈 슬롯으로
         *          나가지 않도록 항상 유효한 버퍼를 바인딩한다. 언리얼의 기본 머티리얼에 해당한다.
         *
         *          언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같은 방식이다.
         *          주의: 고정 크기 원소 하나를 모든 셰이더에 공용으로 바인딩하면 셰이더의 `MaterialData` stride 와 어긋나
         *          DX11 디버그 레이어가 드로우마다 "structure stride 256 vs 24" 같은 경고를 낸다.
         *          원소는 0 이지만 텍스처 인덱스 필드만 `kInvalidDescriptorIndex` 다(`MaterialUtil::makeFallbackElement`). 0 은 Vulkan 의 첫 실제
         *          텍스처 · DX11 · GL 의 머티리얼 텍스처 서수 0 이라, 0 으로 두면 백엔드마다 다른 텍스처를 읽는다.
         *
         *          셋업(ensureMaterialFallbackBuffers)에서만 만들고 기록 중에는 조회만 한다.
         */
        struct MaterialFallback
        {
            vector<uint8>           _bytes;
            RHIStructuredBufferSlot _buffer;
        };
        vector<MaterialFallback> _listMaterialFallback;
        /// @brief 바인딩 레이아웃 → `_listMaterialFallback` 순번입니다. 레이아웃은 `_pPSOCache` 가 소유하며 PSO 마다 하나입니다.
        unordered_map<const ShaderBindingLayout*, uint32> _mapLayoutMaterialFallback;
        /// @brief 순번 테이블이 없는 뷰(주 시점, 그림자)의 정렬 디스패치가 t2 에 바인딩하는 원소 하나짜리 자리 표시 버퍼입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않습니다.
        RHIStructuredBufferSlot _transparentRankPlaceholder;
        /// @brief 가림 타일이 없는 컬링(그림자 슬롯 · 가림을 보지 않는 뷰)이 t2 에 바인딩하는 원소 하나(깊이 1 — 아무것도 가리지 않는다)짜리 자리 표시 버퍼입니다. 셰이더는 가림이 꺼지면 읽지 않습니다.
        RHIStructuredBufferSlot _occlusionTilesPlaceholder;
        /// @brief 주 시점 가림 타일의 되읽기 고리, 게임 스레드가 가져가는 우편함, 요청 때의 메타데이터(요청 번호 % 개수 로 찾는다)입니다. 요청한 프레임과 사본이 도착하는 프레임은 다르다.
        struct OcclusionReadbackMeta
        {
            float4x4 _viewProj{};
            uint32   _columns{ 0 };
            uint32   _rows{ 0 };
            float32  _depthWidth{ 0.0f };
            float32  _depthHeight{ 0.0f };
            uint8    _bValid{ SW_FALSE }; ///< 이 요청의 타일이 판정에 쓸 수 있는 프레임이었다(기록을 버리는 프레임이 아니었다)
        };
        static constexpr uint32 kOcclusionReadbackMetaCount = 8;
        AsyncBufferReadback     _occlusionReadback;
        OcclusionTileMailbox    _occlusionMailbox;
        OcclusionReadbackMeta   _arrOcclusionReadbackMeta[kOcclusionReadbackMetaCount];
        uint64                  _occlusionReadbackSerial;
        /// @brief 셰이더 assert 링(`-gv_gpuAssert`). 컴퓨트 프리패스의 assert 가 켜진 디스패치가 u4 로 쓰고, 프리패스 끝에 되읽기를 요청하며 다음 프레임들이 로그로 낸다. 꺼져 있으면 비어 있다.
        GPUAssertMonitor _gpuAssert;
        /// @brief 패스 출력 NaN · Inf 감시(`-gv_nanCheck`). 그래프가 끝난 뒤 `scanPassOutputsForNan` 이 자기 리스트(`_nanCheckCommandList`)에 기록한다. 꺼져 있으면 비어 있다.
        GPUNanCheck                 _nanCheck;
        unique_ptr<IRHICommandList> _nanCheckCommandList;
        /// @brief 이 렌더러가 소유하는 PSO 저장소입니다(엔진 패스 PSO, Present PSO, 머티리얼 변형과 그 바인딩 레이아웃). 소유와 해제 순서는 캐시가 관리합니다.
        ///        미리보기 씬 렌더러는 이것을 비워 둔다 — `_pPSOCache` 가 주 렌더러의 것을 가리킨다.
        RenderPSOCache _psoCache;
        /**
         * @brief 이 렌더러가 PSO 를 찾고 넣는 저장소입니다. 주 렌더러는 자기 `_psoCache`, 미리보기 씬 렌더러는 주 렌더러의 것입니다(`executeScenePreviewPacket` 이 만들 때 건다).
         * @details 미리보기 씬은 주 렌더러와 같은 디바이스 · 파이프라인 XML 로 그리므로 PSO 가 같다. 나눠 두면 창을 열 때마다(닫으면 렌더러를 내리므로 다시 열 때마다)
         *          엔진 PSO 를 모두 다시 만들어 첫 프레임이 끊기고, 같은 PSO 가 창 수만큼 메모리에 남는다(다중 월드 결정 2). 해제는 소유자만 한다(`releasePassResources`).
         *          미리보기 렌더러는 주 렌더러보다 먼저 내려가고(`shutdown`), 렌더 스레드에서 주 렌더러와 차례로 기록하므로 캐시를 동시에 고치지 않는다.
         */
        RenderPSOCache*                      _pPSOCache;
        unordered_map<hashed_string, uint32> _mapPassNameToIndex;
        /// @brief 모듈 확장 패스(`_listExtensionFramePass`)의 그래프 노드 이름 → 목록 번호입니다. `bindPassCallbacks` 가 채웁니다.
        unordered_map<hashed_string, uint32> _mapExtensionPassIndex;
        /// @brief 이번 프레임의 모듈 확장 패스입니다. 모듈 구간 안(`collectExtensionFrame` ~ `releaseExtensionFrame`)에서만 비지 않습니다. 기록 중에는 읽기만 합니다.
        vector<ExtensionFramePass> _listExtensionFramePass;
        /// @brief 이번 프레임에 요청된 모듈 임시 텍스처입니다(`ExtensionPassBuilder::createTexture`).
        vector<ExtensionTextureDesc> _listExtensionTexture;
        /// @brief 이번 프레임에 프록시들이 낸 요소입니다(`collectDrawElements`). 기록 중에는 읽기만 합니다.
        vector<ExtensionDrawElement> _listExtensionDrawElement;
        /// @brief 요소가 쓰는 메시를 쥐는 표입니다(`MeshUtil::acquirePrimitive` · `MeshCache` 는 약한 참조라 렌더러가 쥔다). 기록 전(셋업)에만 바꾸고, 파이프라인 · 디바이스 자원을 놓을 때 비웁니다.
        unordered_map<hashed_string, shared_ptr<Mesh>> _mapExtensionMesh;
        /// @brief 이번 프레임 모듈 패스 선언의 해시(0 = 없음)입니다. 그래프에 묶인 선언의 해시(`_moduleBoundSignature`)와 다르면 그래프를 다시 묶습니다. 프레임 끝에 0 으로 돌립니다.
        uint64           _moduleFrameSignature{ 0 };
        uint64           _moduleBoundSignature{ 0 };
        RHITextureHandle _outputRenderTarget;
        /// @brief 캡처 · 되읽기입니다. Present 결과 텍스처(`-gv_screenshot`, 크기는 주 출력)를 소유하고 `_pDevice` · 주 시점 풀 · 파이프라인을 빌려 씁니다(그래서 그 셋보다 뒤에 선언한다).
        FrameCapture _capture;
        /// @brief 캔버스(화면 2D) 렌더러입니다. 아틀라스 사본, 사각형 버퍼, 일괄 드로우, 렌더 텍스처 대상(월드 공간 UI)을 렌더 스레드에서 처리합니다.
        CanvasRenderer _canvasRenderer;
        /// @brief 파티클 렌더러입니다. 입자 버퍼와 입자 텍스처를 렌더 스레드에서 처리하고, 투명 패스가 방출기마다 빌보드를 그립니다.
        VFXParticleRenderer _particleRenderer;
        /// @brief 이번 프레임 입자 PSO 의 변형 캐시 키입니다(`ensureParticlePSO` 가 기록 전에 정한다). 입자가 없거나 투명 패스가 없으면 0 입니다.
        uint64 _particlePSOKey;
        /// @brief 그릴 캔버스입니다. 패킷과 `setCanvasFrame` 에서 맞바꾸기로 받으며, 기록 중에는 읽기만 합니다.
        CanvasFrameData _canvasFrame;
        /// @brief Swapchain 을 쓰는 마지막 패스입니다. 선언 순서 기준이며, 그래프가 같은 출력에 대한 쓰기를 선언 순서로 연결합니다. 콜백을 바인딩할 때 정합니다.
        const RenderGraphPassDesc* _pLastSwapchainWriter;
        /// @brief Present 가 톤맵하는 파이프라인에서 FSR 1 을 켰다는 오류를 남겼다(셋업에서만 읽고 쓴다 — 한 번만 알린다).
        uint8 _bUpscalePostChainLogged;
        /**
         * @brief 이번 프레임에 주 출력의 캔버스(화면 2D)를 그리는 뷰입니다. 주 출력에 마지막으로 그리는 뷰(마지막 화면 사각형 뷰, 없으면 주 시점)입니다.
         * @details 화면 사각형 뷰(PiP · 분할 화면)는 주 시점 뒤에 같은 출력에 겹쳐 그리므로, 주 시점의 Canvas 패스가 UI 를 그리면 그 뷰가 UI 를 덮는다.
         *          언리얼이 모든 장면 뷰 뒤에 Slate 를 한 번 그리는 것과 같은 순서로, 마지막 뷰의 Canvas 패스만 UI 를 그린다. `submitGraph` 가 기록 전에 정한다.
         */
        const ViewTarget* _pMainOutputCanvasView;
        /** @brief 미리보기 씬 하나를 그리는 렌더러입니다(`executeScenePreviewPacket`). 씬 키로 찾습니다. */
        struct ScenePreviewRenderer
        {
            unique_ptr<FrameRenderer> _renderer;
            uint32                    _sceneKey{ 0 };
        };
        /// @brief 미리보기 씬마다의 렌더러입니다. 주 렌더러가 소유하고 `shutdown` 이 함께 내립니다(디바이스가 살아 있을 때).
        vector<ScenePreviewRenderer> _listScenePreviewRenderer;
        string                       _statusMessage;
        RenderGraphExecutionContext  _graphContext;

        // 아래는 8바이트보다 작은 필드다. 사이에 끼면 패딩이 생기므로 큰 것부터 두고 작은 것을 끝에 모은다.
        FrameLightState _frameLight; ///< float4 둘(32바이트)이다.
        FrameSkyState   _frameSky;   ///< float4 셋(48바이트)이다. `_bHasFrameSky` 가 0 이면 쓰지 않는다.
        FrameFogState   _frameFog;   ///< 안개 상수 float4 넷이다. `_bHasFrameFog` 가 0 이면 쓰지 않는다.
        /// @brief 볼륨 안개의 froxel 출력(구조 버퍼 float4 — 산란 rgb, 투과율 a)입니다. 컴퓨트가 쓰고 안개 패스가 t11 로 읽습니다(`volumetricfog.hlsl`).
        RHIStructuredBufferSlot _fogVolumeBuffer;
        /// @brief 볼륨 안개 컴퓨트의 입력(float4 행, `SW_FOG_VOLUME_PARAM_FLOAT4_COUNT` 개)입니다. 프레임마다 올립니다.
        RHIStructuredBufferSlot _fogVolumeParamBuffer;
        vector<float4>          _listScratchFogVolumeParam; ///< 그 행의 업로드 임시 버퍼(프레임마다 다시 쓴다)
        /// @brief 주 출력(백버퍼, 게임 뷰 RT)의 크기입니다. 주 시점의 풀은 이 크기 × 사각형 × 해상도 배율이고, 화면 사각형 뷰와 Present 캡처는 이 크기를 씁니다.
        uint32 _outputWidth;
        uint32 _outputHeight;
        /// @brief 마지막 프레임에 그린 추가 뷰 수입니다.
        uint32 _lastRenderedExtraViewCount;
        /// @brief 씬 직접 경로의 출력 크기(`setOutputSizeOverride`)입니다. 0이면 백버퍼 크기를 씁니다.
        uint32 _directOutputWidth;
        uint32 _directOutputHeight;
        /// @brief `_listScenePreviewRenderer` 의 크기를 렌더 스레드가 공개한 값입니다. 다른 스레드(자동화 감시 값)는 목록 대신 이것을 읽습니다.
        atomic<uint32> _scenePreviewRendererCount;
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지 여부입니다. 가시 목록이 유효한지를 나타냅니다.
         * @details 드로우가 가시 목록을 바인딩할지 정하는 값이다. 목록을 바인딩했는데 컬링이 돌지 않았다면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 모두 같은 인스턴스를 그린다.
         */
        uint8 _bGPUCullingActive;
        /// @brief 이번 프레임의 LOD 전환 진행량(0 ~ 255)입니다. 0 이면 크로스페이드 없이 바로 바꾼다(`dispatchCullAndSort` 가 정하고 `dispatchCullView` 가 컬링 상수로 싣는다).
        uint32 _lodFadeStep;
        /// @brief 지난 컬링 프레임의 시각(나노초)입니다. 전환 진행량의 프레임 간격을 잰다(0 이면 아직 없다).
        int64               _lodFadePreviousNanoseconds;
        FrameRendererStatus _status;
        uint8               _bCallbacksBound            : 1;
        uint8               _bPassResourcesReady        : 1;
        uint8               _bScenePreview              : 1; ///< 미리보기 씬 렌더러다 — 프로파일러 보고 · 렌더 타깃 공개를 하지 않는다
        uint8               _bHasFrameSky               : 1; ///< 이번 프레임의 씬에 하늘이 있다(`_frameSky`). 기록 전에 정하고 패스 콜백은 읽기만 한다
        uint8               _bLightGridPSOMissingLogged : 1; ///< 라이트 그리드 컴퓨트 PSO 가 없다는 것을 한 번만 알리는 래치다(셋업 단계에서만 쓴다)
        uint8               _bHasFrameFog               : 1; ///< 이번 프레임에 안개(`FogComponent` · 물속)가 있다(`_frameFog`). 기록 전에 정하고 패스 콜백은 읽기만 한다
        uint8               _bFogVolumeActive           : 1; ///< 이번 프레임에 볼륨 안개 컴퓨트가 돈다(`prepareVolumetricFog` 가 정한다) — 안개 패스가 주 시점에서 볼륨을 읽는다
        /**
         * @brief 주 출력의 보기 모드(`RenderViewMode`)입니다.
         * @details 게임 스레드(패킷)와 직접 경로가 프레임 시작에 읽어 뷰 설정에 싣고, 드로우는 뷰 설정만 본다. 쓰는 쪽이 다른 스레드이므로 atomic 이다.
         */
        atomic<uint8> _viewMode;
        /** @brief 에디터 씬 뷰의 보기 모드(`RenderViewMode`)입니다. UI 스레드가 쓰고, 게임 스레드가 패킷을 만들 때 읽습니다. */
        atomic<uint8> _sceneViewMode;
        /// @brief 머티리얼 폴백 stride 가 없다는 것을 한 번만 알리기 위한 래치입니다. 드로우 경로이므로 프레임마다 로그를 찍으면 안 됩니다.
        atomic<uint8> _bMaterialFallbackMissingLogged;
        /// @brief 컬러 타깃이 없어 패스를 건너뛴다는 것을 한 번만 알리기 위한 래치입니다. 패스 경로이므로 프레임마다 로그를 찍으면 안 됩니다.
        atomic<uint8> _bMissingColorTargetLogged;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 레벨끼리 병렬로 돈다(RenderGraph::executeParallel).
        // 비트필드로 두면 인접 비트를 쓰는 다른 패스와 같은 바이트를 read-modify-write 해서
        // 서로의 값을 덮어쓴다. 그래서 독립된 atomic 변수로 둔다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행되었는지 여부입니다. ForwardOpaque 의 PSO 를 고를 때 씁니다.
        atomic<uint8> _bHasExecutedDepthPrepass;
    };
} // namespace sw
