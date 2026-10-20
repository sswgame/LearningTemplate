/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML 을 로드해 RenderGraph 를 만들고, Shadow, Forward, Deferred, Post 패스를 실행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FrameResourceRegistry.h"
#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"
#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Light/ShadowSetup.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUScene.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"
#include "Engine/Renderer/Vfx/VfxParticleRenderer.h"

namespace sw
{
    struct RenderFramePacket;
    struct ShaderCompileResult;

    class CameraComponent;
    class IRHICommandList;
    class IRHIDevice;
    class Material;
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
     * @details 음수는 "덮어쓰지 않음" 이라 전역 변수(시각은 렌더러 시계)를 따른다. 언리얼 뷰 패밀리의 `FEngineShowFlags` 처럼 값 하나로 주고받는다(기억에 의존한 비교).
     *          배포본의 렌더러는 이 값을 갖지 않고(`FrameRenderer::setDebugOverrides` 가 없다) 늘 전역 변수를 따른다.
     */
    struct FrameDebugOverrides
    {
        /**
         * @brief GPU 메시 모프 진단 모드입니다. 값 체계는 `-gv_morphDiag` 와 같습니다(0 평소 / 1 강제 켬 / 2 디스패치 생략 / 3 번호표).
         * @details 모드 2는 컴퓨트 없이 레스트 버퍼를 정점 셰이더에 물리며, 정답은 레스트 포즈와 같은 이미지다. 그래서 이 모드 하나로
         *          "정점 셰이더의 풀 읽기가 네 백엔드에서 같은가" 를 픽셀로 단언할 수 있다. OpenGL 드라이버가 early-return 형태의
         *          `swComputeMorphElement` 를 잘못 컴파일해 원소를 하나 어긋나게 읽으면 이 단언에 걸린다(binding.hlsli 주석 참고).
         */
        int32 _meshMorphDiag{ -1 };
        /// @brief 1 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶고, 0 이면 배치마다 드로우를 부릅니다(`gv_drawMerge`). 멀티 드로우가 없는 백엔드(DX11)는 늘 배치마다입니다.
        int32 _drawMerge{ -1 };
        /// @brief 1 이면 씬 메시 정점을 한 풀 버퍼에 모으고, 0 이면 메시마다 자기 정점 버퍼로 그립니다(`gv_vertexPool`, 진단과 A/B 비교용).
        int32 _vertexPool{ -1 };
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
        bool initialize( IRHIDevice* pDevice, string_view pipelineXMLPath = {} );
        /** @brief 디바이스, TaskManager, 파이프라인 XML 로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                         string_view pipelineXMLPath = {} );
        /** @brief 핫패스에서 쓰는 TaskManager 를 연결합니다. */
        void bindServices( TaskManager* pTaskManager );
        /** @brief GPU 자원을 해제하고 종료합니다. */
        void shutdown();

        // ------------------------------------------------------------------------------
        // 2) 파이프라인 · 실행: XML 로드, execute / executePacket
        // ------------------------------------------------------------------------------
        /** @brief RenderPipeline XML 로 그래프를 다시 만듭니다(동기 로드). 패스 콜백은 한 번만 바인딩합니다. */
        [[nodiscard]] bool loadPipeline( string_view pipelineXMLPath );
        /**
         * @brief 컴파일된 그래프를 실행합니다. 자기 빌더로 scene 의 스냅샷을 만들고, 패킷 경로와 같은 경로로 GPU 에 올립니다.
         * @details 주 시점은 scene 의 게임 카메라(`Scene::ensureDefaultCameras`)다. 쓸 카메라가 없으면 오류로 알리고 그리지 않은 채 false 입니다.
         */
        bool execute( IRHIDevice* pDevice, Scene& scene );
        /**
         * @brief 렌더 스레드 경로입니다. 미리 만들어 둔 packet 의 GPUScene 을 쓰며 Scene 에는 접근하지 않습니다.
         * @details 주 시점이 없는 패킷(`RenderFramePacket::_bHasViewProj` 가 0 — 활성 씬이 없거나 씬에 카메라가 없다)은 그리지 않고 false 입니다.
         *          3D 내용을 실은 채 주 시점이 없으면 오류로 한 번 알립니다. 주 시점은 `RenderFramePacket::setMainView` 로 싣습니다.
         */
        bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );
        /**
         * @brief 미리보기 씬 패킷(`RenderFramePacket::_listScenePreview` 의 하나)을 그 씬 키의 렌더러로 그립니다. 렌더 스레드에서 주 패킷 뒤에 부릅니다.
         * @details **씬마다 렌더러 하나**를 둔다 — GPUScene(인스턴스 · 배치 · 컬링 산출물) · 트랜지언트 · 패스 상수가 씬별이라 메인 씬과 섞이지 않는다.
         *          언리얼이 미리보기 월드마다 `FScene` 을, 유니티 `PreviewRenderUtility` 가 숨은 씬을 따로 그리는 것과 같은 자리다.
         *          미리보기 렌더러는 프로파일러 · 렌더 타깃 목록 공개 · Present 캡처를 하지 않는다(주 렌더러의 것을 덮지 않게).
         *          출력은 패킷의 `_outputRenderTarget` 이고, 그 타깃의 상태 전환(렌더 타깃 → 셰이더 읽기)은 부르는 쪽(`RenderThread`)이 한다.
         */
        bool executeScenePreviewPacket( IRHIDevice* pDevice, RenderFramePacket& packet );
        /** @brief @p mainPacket 의 미리보기 목록에 없는 씬의 렌더러를 내립니다(창을 닫았거나 가렸다). 미리보기 패킷을 다 그린 뒤 부릅니다. */
        void retireScenePreviews( const RenderFramePacket& mainPacket );
        /**
         * @brief 지금 살아 있는 미리보기 씬 렌더러 수입니다(진단, 테스트, 자동화 탐침 `Editor.ScenePreviewRenderers`).
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
         * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어지므로 그때 한 번만 부르면 된다.
         *          매 프레임 부를 필요는 없다.
         */
        void publishRenderTargets() const;

        /**
         * @brief 주 출력(게임 창, 에디터 게임 뷰)의 보기 모드를 정합니다. `-gv_viewMode` 와 테스트가 씁니다.
         * @details 보기 모드는 뷰마다 갖는 설정(`RenderViewSettings::_viewMode`)이다. 이 값은 주 출력 뷰의 설정에 들어가고,
         *          에디터 씬 뷰는 `setSceneViewMode` 의 값을 쓴다. 다음 프레임의 `ensureMaterialPSOs` 가 그 프레임 뷰들의 모드로
         *          PSO 변형을 만들고, 드로우는 지금 그리는 뷰의 모드로 변형을 고른다. 모드를 바꾼 프레임에는 셰이더 컴파일이 한 번 끼어든다.
         */
        void setViewMode( RenderViewMode viewMode );
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

        /** @brief 패스 타입에 해당하는 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle getEnginePSO( RenderPassType passType ) const;
        /**
         * @brief 이 배치를 그릴 PSO 입니다. 머티리얼 퍼뮤테이션, 뷰 모드, 컬 반전을 적용한 변형이 있으면 그것을, 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 드로우 경로가 배치마다 부르는 읽기 전용 조회다. 캐시는 `ensureMaterialPSOs` 가 기록 전에 채운다.
         *          거울 변환을 건 배치(`GPUMeshBatch::_bReverseCulling`)는 패스의 컬 모드를 뒤집은 변형으로 그린다.
         */
        RHIPipelineStateHandle psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch ) const;
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
        bool findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 주 시점의 TAA 히스토리 텍스처입니다. 파이프라인에 TAA 패스가 없거나 아직 만들지 않았으면 0 입니다(진단과 테스트용). */
        RHITextureHandle getTaaHistory() const { return _mainView._taaHistory; }
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
            RHITextureHandle            _taaHistory{ 0 }; ///< TAA resolve 히스토리(지난 TaaColor 의 복사본)
            RHIDescriptorIndex          _taaHistorySrv{ kInvalidDescriptorIndex };
            RHIStructuredBufferSlot     _transparentRank;           ///< 이 뷰의 투명 순번 테이블(정렬 디스패치 t2). GPU 정렬 백엔드에서 쓴다.
            vector<uint32>              _listTransparentBatchOrder; ///< 이번 프레임 이 뷰의 투명 배치 순서(비면 주 순서를 쓴다)
            vector<uint32>              _listInstanceSlot;          ///< 지금 `_instanceSlotStream` 에 들어 있는 내용(같으면 다시 만들지 않는다)
            RHIBufferHandle             _instanceSlotStream{ 0 };   ///< 이 뷰의 인스턴스 슬롯 스트림(꼬리만 뷰 순서). 컬링이 없는 백엔드(DX11)에서 쓴다.
            uint32                      _transparentTailBase{ 0 };  ///< 순번 테이블 0번의 인스턴스 번호
            /**
             * @brief 이 뷰의 방향광 그림자(셰이더 상수 · 캐스케이드 타일 · 컬링 절두체)입니다. 주 시점은 프레임의 것, 추가 뷰는 자기 카메라에 맞춘 것이다.
             * @details 추가 뷰가 주 시점 볼륨을 쓰면 다른 곳을 보는 CCTV · 분할 화면은 그림자가 빠진다(언리얼의 뷰별 그림자 뷰).
             */
            ViewShadowData _shadow;
            /// @brief 추가 뷰의 그림자 패스 컬링 입력(행렬 · 절두체 · 자기 컬링 상수 버퍼)입니다. 주 시점은 `_arrView[Shadow]` 를 씁니다.
            RenderView           _shadowCullInput;
            uint32               _cullSlot{ 0 };
            uint32               _shadowCullSlot{ static_cast<uint32>( RenderViewType::Shadow ) }; ///< 그림자 패스가 쓰는 컬링 슬롯(주 시점은 1, 추가 뷰는 자기 슬롯)
            uint32               _outputWidth{ 0 };                                                ///< 출력 크기(렌더 텍스처, 화면 사각형)
            uint32               _outputHeight{ 0 };
            uint32               _shadowMapResolution{ 0 };                 ///< 풀이 가진 그림자 맵 한 변(0이면 아직 없음). 품질이 바뀌면 풀을 다시 만든다.
            uint32               _lightGridIndex{ invalid_index::kUint32 }; ///< 이번 프레임 이 뷰의 라이트 그리드 번호(`GPULightBuffer::getGridBase`). 없으면 셰이더는 목록 전체를 돈다
            RenderViewOutputKind _outputKind{ RenderViewOutputKind::ScreenRect };
            uint8                _bRenderThisFrame{ SW_FALSE };
            uint8                _bSeenThisFrame{ SW_FALSE };
            uint8                _bHasTransparentRank{ SW_FALSE }; ///< 이번 프레임에 `_transparentRank` 로 정렬한다.
            uint8                _bUsesViewSlotStream{ SW_FALSE }; ///< 이번 프레임에 `_instanceSlotStream` 으로 그린다.
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
         * @brief 파이프라인 XML 에 선언된 첨부의 포맷을 반환합니다. 선언이 없으면 fallback 을 반환합니다.
         * @details 첨부와 같은 크기와 포맷이어야 하는 보조 텍스처(TAA 히스토리 등)를 만들 때 쓴다.
         *          포맷을 상수로 박아 두면 파이프라인이 HDR 첨부를 쓰는 순간 어긋난다.
         */
        RHIFormat attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const;
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
         *          병렬 기록에서도 안전하다. 슬롯이 모자라면 마지막 슬롯을 공유한다.
         */
        void acquirePassCb( FramePassContext& context );
        /** @brief 프레임을 시작할 때마다 패스 상수 슬롯 커서를 되감고 시드를 0번 슬롯에 맞춥니다. */
        void resetPassCbRing();
        /** @brief 주 시점의 트랜지언트 텍스처를 확보합니다. 크기는 출력 크기 × 사각형 × 해상도 배율이며, 출력 크기는 덮어쓴 크기가 있으면 그 값, 없으면 백버퍼입니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /** @brief 뷰 하나의 트랜지언트를 이 크기로 맞춥니다. 크기가 같으면 그대로 두고, 다시 만들었으면 true 입니다. */
        bool ensureViewTransients( ViewTarget& view, uint32 width, uint32 height );
        /** @brief 뷰 하나의 트랜지언트와 TAA 히스토리를 해제합니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseViewTransients( ViewTarget& view );
        /** @brief 추가 뷰 하나를 통째로 해제합니다. 트랜지언트, 컬링 상수 버퍼, 리스트, 빌린 렌더 텍스처가 해당합니다. */
        void releaseExtraView( ViewTarget& view );
        /**
         * @brief 이번 프레임의 추가 뷰 요청을 뷰 상태에 반영합니다. 새 뷰를 만들고, 사라진 뷰를 해제하고, 출력 텍스처, 풀 크기, 컬링 입력을 맞추고,
         *        GPUScene 의 컬링 슬롯 수를 정합니다. **업로드 전에**(셋업 단계) 불러야 합니다. 버퍼와 텍스처 생성은 기록 중에 할 수 없습니다.
         */
        void prepareExtraViews( const vector<RenderViewRequest>& listRequest );
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
        void ensureTaaHistory( ViewTarget& view );
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
        /** @brief 패스 타입에 맞는 실행 함수를 부릅니다. */
        void executePass( FramePassContext& context, RenderPassType passType, string_view passName, const hashed_string& depthAttachment,
                          const RenderGraphPassDesc* pPassDesc );
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
         * @details 방향, 색, 세기, 앰비언트는 씬의 주 광원에서 온다. 기본값은 "주 광원 없음" 이다. 세기와 색과 앰비언트가 0 이라 그 씬은 어둡게 그려진다
         *          (결정 기록 5-12 D2-13: 라이트는 데이터가 정하고 키 라이트로 대신 비추지 않는다). 방향은 단위 벡터로 둔다. 셰이더가 정규화하기 때문이다(water.hlsl 반사).
         */
        struct FrameLightState
        {
            float4 _dirIntensity{ 0.0f, -1.0f, 0.0f, 0.0f };
            float4 _colorAmbient{ 0.0f, 0.0f, 0.0f, 0.0f };
        };
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
                          const RHIDescriptorIndex* pMaterialTexSrv = nullptr );
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
        /** @brief GPUScene 배치를 간접 드로우로 그립니다. */
        void drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass );
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso );
        /**
         * @brief 방출기마다의 입자 그리기(`GPUScene::getParticleBatches`)를 지금 열린 투명 패스에 그립니다. 메시 투명 배치 **뒤**에 부릅니다.
         * @details 입자 PSO(`ensureParticlePSO`)를 걸고 PassCB 를 바인딩한 뒤 `VfxParticleRenderer::drawBatches` 로 넘긴다. 순서는 주 시점의 정렬 순서다(추가 뷰도 같은 순서로 그린다).
         */
        void drawParticles( FramePassContext& context );
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

        // ------------------------------------------------------------------------------
        // 5) 커맨드 리스트와 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 함수입니다. commandList 를 준비하고 device 에 연결합니다.
         * @param pCallerName 오류 로그에 찍을 호출자 이름
         * @return 성공하면 true 입니다. false 면 호출자가 바로 반환해야 합니다.
         */
        bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 함수입니다. graph 를 실행하고 commandList 를 제출합니다.
         * @return graph.execute() 의 결과
         */
        bool submitGraph( IRHIDevice* pDevice );

        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 뒷부분입니다. 스냅샷을 GPU 로 올리고, 기록 전에 준비해야 하는 것(콜백, 상수 버퍼 슬롯, 머티리얼 PSO)을 갖춘 뒤 그래프를 제출합니다.
         * @details 두 진입점이 이 단계를 각자 가지면 한쪽만 빠지기 쉽다. 예를 들어 시드를 채우는 순서가 갈리면 한쪽만 상수가 빠진다. 그래서 한 함수로 둔다.
         * @param pCallerName 오류 로그에 찍을 호출자 이름
         */
        bool uploadSceneAndSubmit( IRHIDevice* pDevice, const utf8* pCallerName );
        /**
         * @brief 뷰마다 라이트 그리드를 만들어 라이트 버퍼를 올립니다. 업로드 전에, 추가 뷰가 정해진 뒤에 부릅니다(`gv_lightCulling` 0 이면 그리드 없이).
         * @details 라이트와 그림자 타일은 앞서 `GPULightBuffer::populate` 로 담겨 있어야 한다. 뷰의 그리드 번호는 `ViewTarget::_lightGridIndex` 에 남는다.
         */
        void updateLightBuffer( IRHIDevice* pDevice );

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        [[nodiscard]] bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;
        /** @brief 어태치먼트 클리어 색을 반환합니다. 없으면 기본값을 반환합니다. */
        float4 getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const;
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
        bool isLastSwapchainWriter( const RenderGraphPassDesc* pPassDesc ) const;
        /** @brief 패스 타입으로 파이프라인의 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;

        /**
         * @brief 패스 종류 테이블(RenderPassTypeInfo)과 파이프라인 XML 의 패스 서술로 패스 PSO 를 만듭니다.
         * @param pRtvFormatOverride 컬러 RT 포맷을 이 배열로 고정합니다(Present 변형). nullptr 이면 테이블의 고정 포맷을 쓰고, 없으면 출력 선언 순서를 따릅니다.
         */
        RHIPipelineStateHandle createPSOForPassType( RenderPassType passType, const RHIFormat* pRtvFormatOverride = nullptr );
        /** @brief 패스의 엔진 PSO 를 찾습니다. 없으면 테이블이 정한 대체 패스(`_psoFallbackType`)의 PSO 를 반환합니다. */
        RHIPipelineStateHandle findPassPSO( RenderPassType passType ) const;

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 **기록 시작 전에** 모두 만들어 둡니다.
         * @details 기록 중에는 PSO 를 만들 수 없다. 상수 버퍼 용량을 미리 늘리는 것과 같은 이유다. 패스는
         *          병렬로 기록되므로 그때 만들면 백엔드마다 다른 방식으로 깨진다.
         */
        void ensureMaterialPSOs();
        /**
         * @brief 입자 그리기가 있는 프레임에 입자 PSO 를 **기록 시작 전에** 만들어 둡니다(머티리얼 변형 캐시에 둔다 — 해제 순서가 같다).
         * @details 투명 패스 PSO 의 렌더 상태(블렌드 · 깊이 테스트 · RT 포맷)를 물려받고 셰이더만 `EngineDefaultAssets::_shaderParticle` 로 바꾼다. 컬링은 끄고
         *          깊이 쓰기도 끄며, 블렌드는 프리멀티플라이로 고정한다(더하기는 셰이더가 알파 0 으로 낸다). define 은 비운다 — 쿠커가 VSMain · PSMain 을 define 없이 굽는다.
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
                                                                   bool bReverseCulling );
        /**
         * @brief 이번 프레임 배치 수에 맞춰 상수 버퍼 슬롯을 **기록 시작 전에** 늘려 둡니다. 드로우마다 슬롯을 하나씩 쓰기 때문입니다.
         * @details 기록 중에는 버퍼 생성과 bindless 등록을 할 수 없다. 지난 프레임의 최대 사용량(하이워터)도 최솟값으로 본다.
         */
        void ensurePassCbCapacityForFrame();
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다(셋업 전용).
         * @details 기록 중에는 만들 수 없다. 버퍼 생성과 `registerBindlessBufferSrv` 는 bindless 레지스트리를 바꾸는데,
         *          패스 콜백은 태스크 워커에서 병렬로 돈다(`assertRegistryMutableNow` 가 감시하는 규칙).
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
         *          **조회만 한다.** 패스 실행(태스크 워커) 중에 불리므로 없다고 만들면 안 된다. 없으면 엔진 PSO 로 대체하고 한 번 알린다.
         */
        RHIPipelineStateHandle findOutputPSO( RenderPassType passType, RHIFormat targetFormat );
        /** @brief 주 출력에 그리는 패스(Present, Canvas)가 그릴 수 있는 대상 포맷(백버퍼, 오프스크린, 캡처)의 PSO 를 셋업에서 미리 만듭니다. */
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
        /// @brief `ensureMaterialPSOs` 가 이번 프레임 배치에서 모으는 변형 조건입니다. 중복이 없고, 프레임마다 다시 채웁니다.
        vector<MaterialPSORequest> _listOpaquePSORequestScratch;
        vector<MaterialPSORequest> _listTransparentPSORequestScratch;
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
        vector<float4x4> _listScratchGridViewProj; ///< 라이트 그리드를 만들 뷰의 뷰-투영(프레임마다 다시 쓴다)
        /// @brief 씬 직접 경로에서 라이트를 모으는 버퍼입니다. 프레임마다 할당하지 않으려고 보관합니다.
        vector<GPULight> _listScratchLight;
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
        int32 getEffectiveMeshMorphDiag() const;
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
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 디스패치를 돌렸으면 true 입니다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const ViewTarget* pExtraView );
        /**
         * @brief 머티리얼이 없는 배치에 바인딩하는 원소 하나짜리 구조 버퍼입니다. **내용이 다른 원소마다 하나**씩 있습니다.
         * @details DX12 에서 아무것도 바인딩하지 않은 t5 를 읽으면 GPU 폴트(디바이스 제거)가 난다. 어떤 드로우도 빈 슬롯으로
         *          나가지 않도록 항상 유효한 버퍼를 바인딩한다. 언리얼의 기본 머티리얼에 해당한다.
         *
         *          언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같은 방식이다.
         *          주의: 고정 크기 원소 하나를 모든 셰이더에 공용으로 바인딩하면 셰이더의 `SwMaterialData` stride 와 어긋나
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
        RHITextureHandle                     _outputRenderTarget;
        /// @brief 캡처 · 되읽기입니다. Present 결과 텍스처(`-gv_screenshot`, 크기는 주 출력)를 소유하고 `_pDevice` · 주 시점 풀 · 파이프라인을 빌려 씁니다(그래서 그 셋보다 뒤에 선언한다).
        FrameCapture _capture;
        /// @brief 캔버스(화면 2D) 렌더러입니다. 아틀라스 사본, 사각형 버퍼, 일괄 드로우, 렌더 텍스처 대상(월드 공간 UI)을 렌더 스레드에서 처리합니다.
        CanvasRenderer _canvasRenderer;
        /// @brief 파티클 렌더러입니다. 입자 버퍼와 입자 텍스처를 렌더 스레드에서 처리하고, 투명 패스가 방출기마다 빌보드를 그립니다.
        VfxParticleRenderer _particleRenderer;
        /// @brief 이번 프레임 입자 PSO 의 변형 캐시 키입니다(`ensureParticlePSO` 가 기록 전에 정한다). 입자가 없거나 투명 패스가 없으면 0 입니다.
        uint64 _particlePSOKey;
        /// @brief 그릴 캔버스입니다. 패킷과 `setCanvasFrame` 에서 맞바꾸기로 받으며, 기록 중에는 읽기만 합니다.
        CanvasFrameData _canvasFrame;
        /// @brief Swapchain 을 쓰는 마지막 패스입니다. 선언 순서 기준이며, 그래프가 같은 출력에 대한 쓰기를 선언 순서로 연결합니다. 콜백을 바인딩할 때 정합니다.
        const RenderGraphPassDesc* _pLastSwapchainWriter;
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
        /// @brief 주 출력(백버퍼, 게임 뷰 RT)의 크기입니다. 주 시점의 풀은 이 크기 × 사각형 × 해상도 배율이고, 화면 사각형 뷰와 Present 캡처는 이 크기를 씁니다.
        uint32 _outputWidth;
        uint32 _outputHeight;
        /// @brief 마지막 프레임에 그린 추가 뷰 수입니다.
        uint32 _lastRenderedExtraViewCount;
        /// @brief 씬 직접 경로의 출력 크기(`setOutputSizeOverride`)입니다. 0이면 백버퍼 크기를 씁니다.
        uint32 _directOutputWidth;
        uint32 _directOutputHeight;
        /// @brief `_listScenePreviewRenderer` 의 크기를 렌더 스레드가 공개한 값입니다. 다른 스레드(자동화 탐침)는 목록 대신 이것을 읽습니다.
        atomic<uint32> _scenePreviewRendererCount;
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지 여부입니다. 가시 목록이 유효한지를 나타냅니다.
         * @details 드로우가 가시 목록을 바인딩할지 정하는 값이다. 목록을 바인딩했는데 컬링이 돌지 않았다면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 모두 같은 인스턴스를 그린다.
         */
        uint8                  _bGPUCullingActive;
        FrameRendererStatus    _status;
        uint8                  _bCallbacksBound     : 1;
        uint8                  _bPassResourcesReady : 1;
        uint8                  _bScenePreview       : 1; ///< 미리보기 씬 렌더러다 — 프로파일러 보고 · 렌더 타깃 공개를 하지 않는다
        [[maybe_unused]] uint8 _reservedFlags       : 4;
        /**
         * @brief 주 출력의 보기 모드(`RenderViewMode`)입니다.
         * @details 게임 스레드(패킷)와 직접 경로가 프레임 시작에 읽어 뷰 설정에 싣고, 드로우는 뷰 설정만 본다. 쓰는 쪽이 다른 스레드이므로 atomic 이다.
         */
        atomic<uint8> _viewMode;
        /** @brief 에디터 씬 뷰의 보기 모드(`RenderViewMode`)입니다. UI 스레드가 쓰고, 게임 스레드가 패킷을 만들 때 읽습니다. */
        atomic<uint8> _sceneViewMode;
        /// @brief 셋업에 없는 출력 대상 포맷(Present, Canvas)을 만났을 때 한 번만 알리기 위한 래치입니다.
        atomic<uint8> _bOutputPSOMissingLogged;
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
