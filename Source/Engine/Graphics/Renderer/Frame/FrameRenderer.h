/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML 을 로드하고 RenderGraph 를 만든 뒤 Shadow/Forward/Deferred/Post 를 실행합니다.
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
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/FrameResourceRegistry.h"
#include "Engine/Graphics/Renderer/Frame/GpuTimelineExporter.h"
#include "Engine/Graphics/Renderer/Frame/PassConstantRing.h"
#include "Engine/Graphics/Renderer/Frame/PassConstantValues.h"
#include "Engine/Graphics/Renderer/Frame/RenderPsoCache.h"
#include "Engine/Graphics/Renderer/Frame/RenderView.h"
#include "Engine/Graphics/Renderer/Frame/RenderViewScheduler.h"
#include "Engine/Graphics/Renderer/Frame/TransientAttachmentPool.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Graphics/Renderer/Scene/GpuVertexAnimationPool.h"

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

    /** @brief FrameRenderer 초기화 · 파이프라인 상태입니다. */
    enum class FrameRendererStatus : uint8
    {
        Uninitialized = 0, ///< initialize 를 아직 부르지 않음
        Ready,             ///< 파이프라인 로드 · 그래프 준비 완료
        Failed             ///< initialize/loadPipeline 실패(getStatusMessage)
    };

    /**
     * @class FrameRenderer
     * @brief 프레임 경로입니다: RenderPipeline XML → RenderGraph → GpuScene / MeshComponents → IRHIDevice
     * @details **뷰가 여럿입니다.** 주 시점 하나 + 추가 뷰(`RenderViewRequest` — CCTV · 백미러 · 미니맵 렌더 텍스처, 분할 화면 · PiP 화면 사각형)를 같은
     *          그래프로 한 번씩 그립니다. 뷰마다 트랜지언트 풀 · 컬링 칸(간접 인자 · 가시 목록 · 컬링 상수버퍼) · TAA 기록 · 커맨드 리스트를 따로 듭니다
     *          (`ViewTarget`). 순서: 컴퓨트 프리패스(모든 뷰의 컬링) → 렌더 텍스처 뷰(주 시점이 그 텍스처를 읽는다) → 주 시점 → 화면 사각형 뷰(주 시점 위에 겹친다).
     */
    class SW_API FrameRenderer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명: 디바이스 · 파이프라인 XML, 핫패스 서비스
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
        bool initialize( IRHIDevice* pDevice, string_view pipelineXmlPath = {} );
        /** @brief 디바이스 · TaskManager · 파이프라인 XML 로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                         string_view pipelineXmlPath = {} );
        /** @brief 핫패스 서비스(TaskManager)를 연결합니다. */
        void bindServices( TaskManager* pTaskManager );
        /** @brief GPU 자원을 해제하고 종료합니다. */
        void shutdown();

        // ------------------------------------------------------------------------------
        // 2) 파이프라인 · 실행: XML 로드, execute / executePacket
        // ------------------------------------------------------------------------------
        /** @brief RenderPipeline XML 에서 그래프를 다시 만듭니다(동기 로드). 패스 콜백은 한 번 바인딩합니다. */
        [[nodiscard]] bool loadPipeline( string_view pipelineXmlPath );
        /** @brief 컴파일된 그래프를 실행합니다. scene 이 있으면 자기 빌더로 스냅샷을 만들어 패킷 경로와 같은 길로 올립니다. */
        bool execute( IRHIDevice* pDevice, Scene* pScene = nullptr );
        /** @brief 렌더 스레드 경로입니다. 미리 만든 packet 의 GpuScene 을 씁니다(Scene 에 접근하지 않습니다). */
        bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );

        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        /** @brief Ready 상태면 true 입니다. */
        bool isReady() const { return _status == FrameRendererStatus::Ready; }

        /**
         * @brief GPU 메시 모프 진단 모드를 코드에서 고릅니다(`-gv_morphDiag` 와 같은 값 체계, 음수 = 전역 변수를 따름).
         * @details 테스트가 씁니다. 2(컴퓨트 없이 레스트 버퍼를 정점 셰이더에 물림)의 정답은 **레스트 포즈와 같은
         *          그림**이라, 이 모드 하나로 "정점 셰이더의 풀 읽기가 네 백엔드에서 같은가" 를 픽셀로 단언할 수
         *          있습니다. OpenGL 드라이버가 early-return 모양의 `swComputeMorphElement` 를 잘못 컴파일해 한 칸 어긋난
         *          원소를 읽으면 정확히 이 단언에 걸립니다(binding.hlsli 주석 참고).
         */
        void setMeshMorphDiag( int32 mode ) { _meshMorphDiagOverride = mode; }
        /**
         * @brief 같은 PSO · 머티리얼의 연속 배치를 멀티 드로우 하나로 묶을지 정합니다(기본 켬, 전역 `gv_drawMerge` 를 덮어씁니다).
         * @details 끄면 배치마다 한 번씩 부릅니다. 백엔드가 멀티 드로우를 못 하면(DX11) 어차피 그렇습니다. 테스트가 켬/끔의 그림을 비교합니다.
         */
        void setDrawMergeEnabled( bool bEnabled ) { _drawMergeOverride = bEnabled ? 1 : 0; }
        /** @brief 정점 풀을 쓸지 정합니다(기본 켬, 전역 `gv_vertexPool` 을 덮어씁니다). 끄면 메시마다 자기 정점 버퍼를 씁니다(진단 · A/B 용). */
        void setVertexPoolEnabled( bool bEnabled ) { _vertexPoolOverride = bEnabled ? 1 : 0; }
#if !defined( SW_SHIPPING )
        /**
         * @brief 시간 구동 컴퓨트(인스턴스 애니메이션 · 메시 모프)가 읽는 절대 시간(초)을 고정합니다. 음수면 렌더러 시계(`_animTimer`)를 따릅니다(기본).
         * @details 테스트가 씁니다. 모프 변위는 sin(시간) 이라 벽시계로 찍으면 찍은 시각에 따라 그림이 달라지고, 부하가 걸린 실행에서 픽셀 단언이 흔들립니다.
         *          배포본에는 없습니다 — 시험 전용 시계 고정이라 배포본에서 바꿀 수 없게 합니다.
         */
        void setAnimationTimeOverride( float32 seconds ) { _animationTimeOverride = seconds; }
#endif
        /** @brief 마지막 프레임이 낸 씬 간접 드로우 호출 수입니다(모든 패스 합). 배치 수보다 작으면 묶인 것입니다. */
        uint32 getLastIndirectDrawCallCount() const { return _lastIndirectDrawCallCount; }
        /**
         * @brief 풀스크린 패스가 이 역할의 입력을 **걸지 않게** 합니다(쇼 플래그. 언리얼의 r.AmbientOcclusion.Levels=0 자리).
         * @details 셰이더는 그 인덱스를 kInvalidIndex 로 읽어 폴백합니다(AO 는 1). "이 입력이 실제로 그림을 바꾸는가" 를
         *          같은 프레임 안에서 비교할 수 있습니다. 입력이 매 프레임 계산만 되고 그림에 안 쓰이는 결함을 픽셀로 잡습니다.
         */
        void setInputRoleEnabled( RenderPassInputRole role, bool bEnabled );
        /** @brief 초기화 · 파이프라인 상태를 반환합니다. */
        FrameRendererStatus getStatus() const { return _status; }
        /** @brief Failed 일 때의 원인 메시지입니다(그 밖에는 비어 있습니다). */
        const string& getStatusMessage() const { return _statusMessage; }
        /** @brief 렌더 그래프를 반환합니다. */
        const RenderGraph& getGraph() const { return _graph; }
        /** @brief GpuScene 을 반환합니다. */
        const GpuScene& getGpuScene() const { return _gpuScene; }
        GpuScene&       getGpuScene() { return _gpuScene; }
        /** @brief 셰이더 핫 리로드 알림입니다. 그 셰이더의 바인딩 레이아웃을 무효화하고 패스 PSO 를 모두 다시 만듭니다. */
        void onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result );
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 PPM(P6)으로 저장합니다. 백엔드별 시각 검증용입니다.
         * @details 프레임 경로가 아닙니다(GPU 를 기다립니다). 창 캡처가 백엔드마다 되고 안 되고가 갈려서,
         *          네 백엔드를 같은 기준으로 비교하려면 이쪽을 씁니다.
         */
        bool dumpTransientToPpm( string_view attachmentName, string_view outFilePath );

        /**
         * @brief Present 결과를 텍스처로도 받아 둘지 정합니다(`-gv_screenshot` 실행 전용).
         * @details 켜면 Present 가 백버퍼 대신 캡처 텍스처에 그리고 그것을 백버퍼로 복사합니다.
         *          전체 화면 복사 한 번이 더 붙으므로 평소에는 꺼 둡니다.
         */
        void setPresentCaptureEnabled( bool bEnabled );
        /** @brief 이 렌더러가 Present 결과를 받아 두고 있으면 true 입니다. */
        bool isPresentCaptureEnabled() const { return _bPresentCaptureEnabled != SW_FALSE && _presentCapture != 0; }
        /**
         * @brief 받아 둔 Present 결과(= 화면에 나간 그림)를 CPU 로 읽습니다.
         * @details 포맷은 늘 `constant::kBackBufferFormat` 입니다. 테스트가 **최종 화면**을 픽셀로
         *          비교하는 유일한 길입니다. 트랜지언트만 읽을 수 있고 백버퍼는 핸들이 없습니다.
         */
        bool readbackPresentCapture( vector<uint8>& outByte, RHITextureMipSpan& outLayout );
        /** @brief 받아 둔 Present 결과(= 화면에 나간 그림)를 PPM 으로 씁니다. */
        bool dumpPresentCaptureToPpm( string_view outFilePath );
        /**
         * @brief 다음 프레임부터 그릴 캔버스(화면 2D)를 받습니다 — 렌더러가 든 것과 **바꿔치기**합니다(용량이 돈다).
         * @details 패킷 경로는 `executePacket` 이 패킷의 `_canvas` 로 같은 일을 합니다. 씬 직접 경로(에디터 · 시험)는 이것으로 넣고, 바꿀 때까지 매 프레임
         *          그 목록을 그립니다. 아틀라스 업로드는 한 번만 반영합니다(반영하면 비운다).
         */
        void setCanvasFrame( CanvasFrameData& inoutFrame );

    private:
        /** @brief 읽어 온 바이트를 PPM(P6) 파일로 씁니다. 트랜지언트 덤프와 Present 캡처 덤프가 같이 씁니다. */
        [[nodiscard]] static bool writePpm( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath );

    public:
        /**
         * @brief 화면에 나간 첨부의 이름입니다. Present 패스가 입력으로 받는 것입니다. 없으면 빈 문자열입니다.
         * @details 찍고 싶은 것은 늘 "지금 보이는 그림" 이므로 파이프라인에 물어봅니다. 주의: 이름을 `"SceneColor"`
         *          같은 리터럴로 박으면 그 이름이 없는 파이프라인(디퍼드)에서는 읽기 실패 로그만 남고 파일이 안 생깁니다.
         */
        string_view getPresentedAttachmentName() const;
        /**
         * @brief 그림자 맵 한 변의 텍셀 수입니다 — 그림자 품질(`gv_shadowQuality` 0~3)이 1024 · 1536 · 2048 · 4096 을 고릅니다.
         * @details 그림자 맵은 화면 크기를 따르지 않는다 — 정사각 볼륨을 1280×720 에 담으면 텍셀이 한 축으로 1.8 배 늘어나고, 출력이 작은
         *          에디터 게임 뷰에서는 그림자 맵도 같이 작아진다. 게임 스레드(그림자 행렬의 텍셀 스냅)와 렌더 스레드(첨부 할당)가 같은 값을 쓴다.
         */
        static uint32 getShadowMapResolution();
        /**
         * @brief 지금 살아 있는 트랜지언트 목록을 엔진 레지스트리에 공개합니다(에디터 패널이 읽습니다).
         * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어지므로 그때 한 번 부르면 됩니다.
         *          매 프레임 부를 이유가 없습니다.
         */
        void publishRenderTargets() const;
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 옵니다(밉 0). 테스트가 백엔드 사이 픽셀을 비교하는 데 씁니다. GPU 를 기다립니다.
         * @param outFormat 첨부의 RHIFormat(채널 순서 해석용).
         */
        bool readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat );

        /**
         * @brief 씬 지오메트리 보기 방식을 정합니다(Lit/Unlit/Wireframe).
         * @details 다음 프레임의 `ensureMaterialPsos` 가 그 모드의 PSO 변형을 만들고 드로우가 그것을 고릅니다.
         *          모드를 바꾼 프레임에 셰이더 컴파일이 한 번 끼고, 그 뒤로는 캐시에서 나옵니다.
         *          렌더 스레드가 드로우마다 읽으므로 atomic 입니다(락을 걸 자리가 아닙니다).
         */
        void setViewMode( RenderViewMode viewMode );
        /** @brief 현재 보기 방식입니다. */
        RenderViewMode getViewMode() const;

        /** @brief 패스 타입에 대응하는 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle getEnginePso( RenderPassType passType ) const;
        /**
         * @brief 이 배치를 그릴 PSO 입니다. 머티리얼 퍼뮤테이션 · 뷰 모드 · 컬 반전을 얹은 변형이 있으면 그것, 없으면 패스 PSO 그대로입니다.
         * @details 드로우 경로가 배치마다 부르는 조회입니다(읽기 전용). 캐시는 `ensureMaterialPsos` 가 기록 전에 채웁니다.
         *          거울 변환 배치(`GpuMeshBatch::_bReverseCulling`)는 패스의 컬 모드를 뒤집은 변형으로 그립니다.
         */
        RHIPipelineStateHandle psoForBatch( RHIPipelineStateHandle passPso, const GpuMeshBatch& batch ) const;
        /**
         * @brief 이 패스가 이 배치를 그리는지 반환합니다 — 패스가 머티리얼 define 으로 배치를 거르면(메시 외곽선) 퍼뮤테이션에 그 define 이 있어야 합니다.
         * @details 판정은 `FrameRendererUtil::drawsMaterialInPass` 하나입니다(머티리얼 PSO 변형 · 셰이더 쿠커와 같은 판정).
         */
        bool drawsBatchInPass( RenderPassType passType, const GpuMeshBatch& batch ) const;
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터를 돌려줍니다(셰이더 경로 · define · 렌더 상태). 모르는 PSO 면 false 입니다.
         * @details 어떤 퍼뮤테이션이 실제로 걸렸는지 밖에서 볼 수 있는 유일한 창입니다. 픽셀로는 안 보이는
         *          차이(알파 경로가 컴파일됐는가 같은)를 테스트가 여기서 확인합니다.
         */
        bool findPsoDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 주 시점의 TAA 히스토리 텍스처입니다. 파이프라인에 TAA 패스가 없거나 아직 만들지 않았으면 0 입니다(진단 · 시험용). */
        RHITextureHandle getTaaHistory() const { return _mainView._taaHistory; }
        /**
         * @brief 씬 직접 경로(`execute( pScene )`)의 출력 크기를 정합니다(0 이면 백버퍼). 초상화 굽기처럼 창과 다른 크기로 그릴 때 씁니다.
         * @details 패킷 경로는 패킷의 뷰포트 크기를 따르므로 이 값을 보지 않습니다.
         */
        void setOutputSizeOverride( uint32 width, uint32 height )
        {
            _directOutputWidth  = width;
            _directOutputHeight = height;
        }
        /** @brief 마지막 프레임에 실제로 그린 추가 뷰 수입니다(갱신 주기 · 예산으로 쉰 뷰는 빠진다 — 진단 · 시험). */
        uint32 getLastRenderedExtraViewCount() const { return _lastRenderedExtraViewCount; }
        /** @brief 들고 있는 추가 뷰 수입니다(쉬는 뷰 포함). */
        uint32 getExtraViewCount() const { return static_cast<uint32>( _listExtraView.size() ); }

    private:
        /**
         * @brief 패스 하나를 기록하는 동안의 로컬 상태입니다.
         * @details 병렬 기록에서는 패스마다 하나씩 존재합니다. 주의: 이 값들을 FrameRenderer 멤버로 두고
         *          저장/복원하면 "한 번에 한 패스만 돈다" 는 전제가 되어 병렬 기록에서 서로를 덮어씁니다.
         *          상수 버퍼도 패스마다 따로 있어야 합니다. 하나를 공유하면 기록은 지연이고
         *          버퍼 쓰기는 즉시라, replay 시점엔 마지막 writer 의 값만 남습니다.
         */
        struct FramePassContext
        {
            IRHICommandList* _pCmd{ nullptr };
            /** @brief 엔진 PassCB 값(이름 기반)입니다. ShaderParameterBinder 가 리플렉션 오프셋에 기록합니다. */
            PassConstantValues _passValues{};
            /** @brief `g_World` 입니다. 인스턴스 버퍼가 없는 드로우(풀스크린 · 픽스처)의 월드 행렬입니다. 씬 메시는 인스턴스 버퍼에서 읽습니다. */
            float4x4           _world{};
            RHIBufferHandle    _passCb{ 0 };
            RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
            /** @brief 패스 스코프 이름 → 리소스 레지스트리입니다. 패스 시작마다 새로 시작(reset)합니다. 병렬 기록 시
             *         패스마다 독립이어야 하므로 FrameRenderer 공유 멤버가 아니라 여기 둡니다. */
            FrameResourceRegistry _resourceRegistry{};
            /// @brief 마지막으로 엔진 상수버퍼를 올린 시점의 (버퍼, 값 버전, 레지스트리 버전)입니다.
            ///        셋이 그대로면 그 드로우는 버퍼를 다시 만들 필요가 없습니다.
            RHIBufferHandle _lastCbBuffer{ 0 };
            uint32          _lastCbValuesVersion{ 0 };
            uint32          _lastCbRegistryVersion{ 0 };
            /**
             * @brief 마지막으로 리소스를 **실제로 건** PSO 입니다.
             * @details 슬롯 상태는 PSO 단위입니다. `setPipelineState` 는 이전 PSO 가 건 t/u 슬롯이 다음 PSO 로
             *          새지 않게 슬롯 상태를 통째로 비웁니다. 그래서 값 · 레지스트리가 그대로여도 PSO 가 바뀌었으면
             *          다시 걸어야 합니다. 이것을 빼먹으면 배치가 퍼뮤테이션 PSO 로 갈아탄 순간 t9(g_SwMaterials)
             *          가 **바인딩되지 않은 채** 드로우가 나가고, Vulkan 은 초기화되지 않은 디스크립터를 읽어
             *          디바이스를 잃습니다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
             */
            RHIPipelineStateHandle _lastBindPso{ 0 };
            /**
             * @brief 이 패스가 어떤 컬링 칸의 결과를 쓸지 정합니다(0 주 시점 · 1 그림자 · `kFirstExtraCullView` 부터 추가 뷰).
             * @details 그림자 패스만 그림자 칸이고 나머지는 지금 그리는 뷰의 칸입니다. 컬링 결과는 절두체에 종속이라
             *          뷰를 잘못 고르면 그림자 드리우개가 사라지거나 화면 밖 물체를 그립니다.
             */
            uint32 _cullViewIndex{ 0 };
            /// @brief 지금 기록하는 패스의 종류입니다(`executePass` 가 정합니다). 드로우 루프가 이 패스가 그리지 않는 머티리얼의 배치를 거를 때 봅니다.
            RenderPassType _passType{ RenderPassType::Invalid };
            /// @brief 이 드로우 그룹의 루트 상수 값(머티리얼 원소 수)입니다. 배치마다 다른 값(인스턴스 시작 · 모프 풀 · 정점 풀)은
            ///        배치 표(g_SwBatches)와 인스턴스 슬롯 스트림이 주므로 그룹 안에서 루트 상수를 다시 걸지 않습니다.
            ///        PassCB 에 넣으면 한 패스의 드로우들이 서로를 덮어씁니다(binding.hlsli 1-0 참고).
            uint32 _drawMaterialCount{ 0 };
            /// @brief bindForDraw 가 마지막으로 조회한 PSO → 레이아웃입니다. 같은 PSO 로 연속 드로우할 때
            ///        layoutForPso() 의 뮤텍스 + 해시맵 조회를 건너뛰는 패스 로컬 1칸 캐시입니다.
            RHIPipelineStateHandle     _lastLayoutPso{ 0 };
            const ShaderBindingLayout* _pLastLayout{ nullptr };

            /**
             * @brief "마지막으로 건 것" 캐시를 모두 잊습니다. PSO · 버퍼 핸들이 무효가 되는 자리(디바이스 교체)에서 부릅니다.
             * @details 핸들 값은 **디바이스 안에서만** 정체성입니다. 새 디바이스의 PSO 는 옛 디바이스의 PSO 와 같은 값을
             *          받을 수 있고(둘 다 첫 PSO 가 같은 번호), 그러면 `_lastLayoutPso == pso` 가 참이 되어 이미 파괴된
             *          레이아웃(`_pLastLayout`)을 쓰고 리소스 재바인딩을 건너뛰어, 백엔드 교체 뒤
             *          아무것도 안 그려집니다. 파괴와 함께 캐시도 지워야 "같은 값 = 같은 것" 이 성립합니다.
             */
            void resetBindingCache()
            {
                _lastCbBuffer          = 0;
                _lastCbValuesVersion   = 0;
                _lastCbRegistryVersion = 0;
                _lastBindPso           = 0;
                _lastLayoutPso         = 0;
                _pLastLayout           = nullptr;
            }
        };

        /**
         * @brief 뷰 하나가 프레임을 넘어 드는 것입니다 — 트랜지언트 풀 · 컬링 입력(추가 뷰) · TAA 기록 · 직렬 커맨드 리스트 · 출력 텍스처.
         * @details 주 시점은 `_mainView` 이고(컬링 입력은 `_arrView[Main]`), 추가 뷰는 카메라 id(`_viewId`)로 찾습니다. 패스 코드는 지금 그리는 뷰
         *          (`_pActiveView`)의 것만 봅니다 — 뷰가 바뀌면 트랜지언트 이름(SceneColor …)이 다른 텍스처를 가리킵니다.
         */
        struct ViewTarget
        {
            TransientAttachmentPool     _transientPool;
            RenderView                  _cullInput;   ///< 추가 뷰의 컬링 입력(행렬 · 절두체 · 자기 컬링 상수버퍼)
            unique_ptr<IRHICommandList> _commandList; ///< 이 뷰를 직렬로 기록하는 리스트(주 시점은 병렬 경로가 아닐 때만)
            RenderViewSettings          _settings;
            hashed_string               _outputPath;                ///< 렌더 텍스처 경로
            Texture2D*                  _pOutputTexture{ nullptr }; ///< 빌려 든 렌더 텍스처(`TextureCache`)
            uint64                      _viewId{ 0 };
            RHITextureHandle            _taaHistory{ 0 }; ///< TAA resolve 히스토리(지난 TaaColor 의 복사본)
            RHIDescriptorIndex          _taaHistorySrv{ kInvalidDescriptorIndex };
            RHIStructuredBufferSlot     _transparentRank;           ///< 이 뷰의 투명 순번 표(정렬 디스패치 t2) — GPU 정렬 백엔드
            vector<uint32>              _listTransparentBatchOrder; ///< 이번 프레임 이 뷰의 투명 배치 순서(비면 주 순서)
            vector<uint32>              _listInstanceSlot;          ///< 지금 `_instanceSlotStream` 에 든 내용(같으면 다시 만들지 않는다)
            RHIBufferHandle             _instanceSlotStream{ 0 };   ///< 이 뷰의 인스턴스 슬롯 스트림(꼬리만 뷰 순서) — 컬링 없는 백엔드(DX11)
            uint32                      _transparentTailBase{ 0 };  ///< 순번 표 0 번의 인스턴스 번호
            uint32                      _cullSlot{ 0 };
            uint32                      _outputWidth{ 0 }; ///< 출력 크기(렌더 텍스처 · 화면 사각형)
            uint32                      _outputHeight{ 0 };
            uint32                      _shadowMapResolution{ 0 }; ///< 풀이 든 그림자 맵 한 변(0 = 아직 없음) — 품질이 바뀌면 풀을 다시 만든다
            RenderViewOutputKind        _outputKind{ RenderViewOutputKind::ScreenRect };
            uint8                       _bRenderThisFrame{ SW_FALSE };
            uint8                       _bSeenThisFrame{ SW_FALSE };
            uint8                       _bHasTransparentRank{ SW_FALSE }; ///< 이번 프레임 `_transparentRank` 로 정렬한다
            uint8                       _bUsesViewSlotStream{ SW_FALSE }; ///< 이번 프레임 `_instanceSlotStream` 으로 그린다
        };

        /** @brief 주 출력에 그리는 패스(Present · Canvas)의 대상입니다 — 지금 뷰가 정한다(`resolvePresentTarget`). */
        struct PresentTarget
        {
            RHITextureHandle _texture{ 0 }; ///< 0 = 백버퍼
            uint32           _width{ 0 };
            uint32           _height{ 0 };
            RHIFormat        _format{ RHIFormat::Unknown }; ///< 대상의 실제 포맷(PSO 를 고른다)
            uint8            _bRenderTexture{ SW_FALSE };   ///< 렌더 텍스처 뷰의 자기 텍스처
            uint8            _bCapture{ SW_FALSE };         ///< 백버퍼 대신 스크린샷 캡처 텍스처에 그린다
            uint8            _bCaptureToBack{ SW_FALSE };   ///< 캡처를 Swapchain 을 쓰는 마지막 패스 뒤에 백버퍼로 복사한다(출력이 곧 백버퍼일 때)
        };

        // ------------------------------------------------------------------------------
        // 4) 패스 자원 · 콜백 · 드로우
        // ------------------------------------------------------------------------------
        /** @brief 패스용 상주 GPU 자원(패스 CB 링 · 컴퓨트 CB 표 · 엔진 PSO · Present 변종 · 머티리얼 폴백)을 확보합니다. */
        void ensurePassResources();
        /**
         * @brief 패스용 상주 GPU 자원을 해제합니다. 디바이스가 없으면 각 자원이 핸들만 잊습니다(목록은 하나).
         * @details 트랜지언트 크기를 따르는 자원(첨부 · TAA 히스토리 · Present 캡처)은 여기서 놓지 않습니다 — `releaseTransientResources` 의 것이고,
         *          셰이더 핫 리로드처럼 패스 자원만 다시 세우는 경로에서 다시 만들어지지 않기 때문입니다.
         */
        void releasePassResources();

        /** @brief 컴퓨트 디스패치 상수버퍼 한 칸입니다. 만들기(`ensurePassResources`)와 놓기(`releasePassResources`)가 같은 표를 돕니다. */
        struct ComputeConstantBufferRow
        {
            RHIConstantBufferSlot* _pSlot{ nullptr };
            uint32                 _byteSize{ 0 };
            const utf8*            _pUsage{ nullptr };
        };
        /// @brief 컴퓨트 상수버퍼 수입니다: 고정 뷰마다 컬링 · 정렬 하나씩 + 인스턴스 애니메이션 · 메시 모프 · 메시 스킨. 추가 뷰의 것은 그 뷰가 든다.
        static constexpr uint32 _s_kComputeConstantBufferCount = static_cast<uint32>( RenderViewType::Count ) * 2 + 3;
        /** @brief 컴퓨트 상수버퍼 표를 채웁니다. 새 컴퓨트 상수버퍼는 여기 한 줄을 더합니다 — 만들기와 놓기를 따로 적지 않습니다. */
        void collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] );
        /**
         * @brief 파이프라인 XML 에 선언된 첨부의 포맷을 반환합니다(없으면 fallback).
         * @details 첨부와 같은 크기 · 포맷이어야 하는 보조 텍스처(TAA 히스토리 등)를 만들 때 씁니다.
         *          포맷을 상수로 박아 두면 파이프라인이 HDR 첨부를 쓰는 순간 어긋납니다.
         */
        RHIFormat attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const;
        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 반환합니다.
         * @details 반환값이 곧 "Clear 로 열어도 되는가" 입니다. 같은 레벨의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계 구역이어야 합니다. 나눠 놓으면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지웁니다.
         */
        bool markAttachmentCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다(프레임 시작). */
        void resetClearedAttachments();
        /**
         * @brief 이번 프레임의 패스 상수 버퍼 슬롯을 하나 집어 ctx 에 붙입니다.
         * @details 커맨드 기록은 지연이고 상수 버퍼 쓰기는 즉시라, 패스마다 별도 버퍼를
         *          써야 재생 시점에 각 패스의 상수가 살아남습니다. 커서는 원자적이라
         *          병렬 기록에서도 안전합니다. 슬롯이 모자라면 마지막 슬롯을 공유합니다.
         */
        void acquirePassCb( FramePassContext& ctx );
        /** @brief 프레임 시작마다 패스 상수 슬롯 커서를 되감고 시드를 0번 슬롯에 맞춥니다. */
        void resetPassCbRing();
        /** @brief 주 시점의 일시 텍스처를 확보합니다(출력 크기 × 사각형 × 해상도 배율). 출력 크기는 덮어쓴 크기, 없으면 백버퍼입니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /** @brief 뷰 하나의 트랜지언트를 이 크기로 맞춥니다(같으면 그대로). 다시 만들었으면 true 입니다. */
        bool ensureViewTransients( ViewTarget& view, uint32 width, uint32 height );
        /** @brief 뷰 하나의 트랜지언트 · TAA 기록을 놓습니다(디바이스가 없으면 핸들만 잊는다). */
        void releaseViewTransients( ViewTarget& view );
        /** @brief 추가 뷰 하나를 통째로 놓습니다(트랜지언트 · 컬링 상수버퍼 · 리스트 · 빌린 렌더 텍스처). */
        void releaseExtraView( ViewTarget& view );
        /**
         * @brief 이번 프레임의 추가 뷰 요청을 뷰 상태로 맞춥니다 — 새 뷰를 만들고, 사라진 뷰를 놓고, 출력 텍스처 · 풀 크기 · 컬링 입력을 맞추고,
         *        GpuScene 의 컬링 칸 수를 정합니다. **업로드 전에**(셋업 단계) 부릅니다 — 버퍼 · 텍스처 생성은 기록 중에 할 수 없다.
         */
        void prepareExtraViews( const vector<RenderViewRequest>& listRequest );
        /**
         * @brief 스냅샷의 이 뷰 투명 순서를 뷰 자원(순번 표 · 슬롯 스트림 · 배치 순서)에 옮깁니다. 없으면 주 순서로 그립니다.
         * @details GPU 정렬 백엔드는 순번 표를 정렬 디스패치에, 컬링 없는 백엔드는 배치 안을 뷰 순서로 다시 놓은 슬롯 스트림을 드로우에 쓴다. 업로드 뒤 · 기록 전에 부릅니다.
         */
        void applyViewTransparentOrder( ViewTarget& view );
        /** @brief 뷰 하나의 투명 순서 자원(순번 표 · 슬롯 스트림)을 놓습니다(디바이스가 없으면 핸들만 잊는다). */
        void releaseViewTransparentOrder( ViewTarget& view );
        /** @brief 추가 뷰 하나를 그래프로 그립니다(직렬, 자기 리스트). 지금 뷰를 그 뷰로 바꿨다가 주 시점으로 돌려놓습니다. */
        void renderExtraView( IRHIDevice* pDevice, ViewTarget& view );
        /** @brief 지금 그리는 뷰의 트랜지언트 풀입니다. */
        TransientAttachmentPool&       activePool() { return _pActiveView->_transientPool; }
        const TransientAttachmentPool& activePool() const { return _pActiveView->_transientPool; }
        /** @brief 지금 그리는 뷰의 컬링 입력입니다(주 시점은 `_arrView[Main]`). */
        RenderView& activeCullInput() { return _pActiveView == &_mainView ? view( RenderViewType::Main ) : _pActiveView->_cullInput; }
        /** @brief 지금 그리는 뷰가 추가 뷰면 true 입니다. */
        bool isRenderingExtraView() const { return _pActiveView != &_mainView; }
        /**
         * @brief 뷰의 TAA 히스토리 텍스처를 **셋업 단계에서** 만들어 둡니다.
         * @details 주의: TAA 패스 콜백은 병렬 기록에서 태스크 스레드가 돌리므로, 거기서 만들고 bindless 에 등록하면
         *          다른 스레드가 같은 레지스트리를 읽는 와중에 레지스트리가 resize 됩니다. 파이프라인이 TAA 를
         *          선언했는지, 대상 첨부의 포맷이 무엇인지는 셋업 시점에 이미 다 알 수 있습니다.
         */
        void ensureTaaHistory( ViewTarget& view );
        /** @brief Present 캡처 텍스처를 한 번만 만듭니다(켜져 있을 때만). */
        void ensurePresentCapture();
        /** @brief 일시 텍스처를 해제합니다. */
        void releaseTransientResources();
        /** @brief 그래프 패스 콜백을 한 번 바인딩합니다. */
        void bindPassCallbacks();
        /** @brief RenderGraph 패스 실행 콜백입니다. */
        void onGraphPassExecute( const RenderGraphPassContext& ctx );

        /**
         * @brief 레벨을 기록하기 **직전에**(직렬 경로에서는 패스마다) 그 레벨이 만질 자원의 배리어를 미리 발행합니다.
         * @details 자원 이름을 실제 텍스처로 풀어 `prepareTextureForShaderRead` /
         *          `prepareTextureForRenderTarget` 을 레벨 첫 패스 리스트의 앞머리에 기록합니다(직렬 경로에서는
         *          그 패스의 리스트). 같은 레벨의 다른 리스트는 큐 순서상 그 뒤에 실행되므로 GPU 타임라인에서도 배리어가 앞섭니다.
         *
         *          이렇게 하면 패스 콜백은 이미 맞는 상태를 보게 되어 기록 중에 리소스 상태를 바꾸지
         *          않습니다. 주의: 배리어를 병렬 기록 스레드가 정하게 하면 중복 배리어 · 레이아웃 불일치가 생깁니다.
         */
        void onGraphLevelPrologue( const RenderGraphLevelContext& ctx );
        /** @brief 패스 타입에 맞는 실행을 합니다. */
        void executePass( FramePassContext& ctx, RenderPassType passType, string_view passName, const hashed_string& depthAttachment,
                          const RenderGraphPassDesc* pPassDesc );
        /**
         * @brief XML 이 선언한 입력을 **역할 이름으로** 모두 겁니다. 풀스크린 패스 공통입니다.
         * @details 역할은 로드 시점에 해석돼 있습니다(`_listResolvedInput`). 여기서 거는 것과 검증이 대조한 것이 같은 목록이라
         *          "선언은 했는데 안 걸리는 입력" 이 생길 자리가 없습니다. 쇼 플래그로 끈 역할은 건너뜁니다.
         */
        void registerDeclaredInputs( FramePassContext& ctx, const RenderGraphPassDesc& passDesc );
        /** @brief 역할의 셰이더 이름(intern 된 hashed_string)입니다. */
        const hashed_string& inputRoleName( RenderPassInputRole role ) const;
        /** @brief 패스 상수 값(PassConstantValues)을 채웁니다. 업로드 · 바인딩은 ShaderParameterBinder 가 합니다. */
        void updatePassConstants( FramePassContext& ctx );
        /** @brief 지금 그리는 뷰에 따라 다른 패스 상수(외곽선 텍셀 크기 · 패스 플래그 — 후처리 끄기)를 채웁니다. */
        void applyViewPassConstants( FramePassContext& ctx );

        /**
         * @brief 이번 프레임의 주광 값입니다. 패킷이 실어 주면 그 값, 아니면 기본값입니다.
         * @details 방향 · 색 · 세기 · 그림자 행렬은 씬의 주광에서 옵니다. 아래 기본값은 주광이 없을 때의 폴백입니다.
         */
        struct FrameLightState
        {
            float4   _dirIntensity{ -0.35f, -0.85f, -0.25f, 1.35f };
            float4   _colorAmbient{ 1.0f, 0.82f, 0.62f, 0.28f };
            float4x4 _shadowViewProj{};
            float4   _shadowParams{}; ///< g_ShadowParams — 그림자 행렬과 한 묶음
            uint8    _bHasShadowViewProj{ SW_FALSE };
        };
        /** @brief 카메라에서 뷰 · 투영을 적용합니다. */
        void applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera );
        /**
         * @brief 뷰-투영과 **그 역행렬**을 함께 적용합니다.
         * @details 둘을 따로 채우면 언젠가 한쪽만 갱신됩니다. 그러면 디퍼드가 복원한 월드 위치가
         *          지난 프레임의 카메라를 가리키고, 증상은 "빛이 한 프레임 늦게 따라온다" 입니다.
         */
        void applyViewProjection( FramePassContext& ctx, const float4x4& viewProj );
        /** @brief 키라이트 뷰-투영 행렬을 만듭니다. */
        void buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const;
        /** @brief 카메라 뷰-투영 행렬을 만듭니다. */
        void buildViewProj( float4x4& outMat ) const;
        /** @brief 월드 행렬을 항등으로 둡니다. */
        void setIdentityWorld( FramePassContext& ctx );
        /** @brief 드로우 직전에 리플렉션 기반 바인딩을 합니다(PassCB/MaterialCB/텍스처/인스턴스 버퍼). */
        void bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                          const RHIDescriptorIndex* pMaterialTexSrv = nullptr );
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* layoutForPso( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 핸들에 매핑합니다. */
        void registerPsoLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief GPUScene 인스턴스 구조버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& ctx );
        /**
         * @brief 씬 라이트 구조버퍼를 "SwLights" 로 등록합니다. **모든 패스**에 겁니다.
         * @details 인스턴스 버퍼와 달리 지오메트리 패스 전용이 아닙니다. 디퍼드 조명은 풀스크린
         *          패스라 `registerInstanceBuffer` 를 타지 않는데, 라이트는 바로 거기서 필요합니다.
         */
        void registerLightBuffer( FramePassContext& ctx );
        /** @brief 배치의 머티리얼 데이터 버퍼(GPUScene)를 패스 레지스트리에 "SwMaterials" 로 등록합니다. */
        void registerMaterialBuffer( FramePassContext& ctx, const GpuMeshBatch& batch, RHIPipelineStateHandle pso );
        /** @brief 씬 메시를 그립니다. GpuScene 이 올라가 있으면 drawGpuBatches 로 넘기고, 아니면 상태만 맞춥니다. */
        void drawSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief GpuScene 배치를 간접 드로우로 그립니다. */
        void drawGpuBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex );
        /** @brief 뷰의 풀에 일시 텍스처를 할당합니다. */
        void allocateTransient( TransientAttachmentPool& pool, string_view name, RHIFormat format, bool bDepth, const float4& clearColor,
                                uint32 resolutionDivisor = 1 );
        /** @brief 컬러(+깊이) 패스를 시작합니다. 열지 못하면 false 이고, 그때는 그리거나 닫지 않습니다(`beginColorPassMrt`). */
        bool beginColorPass( FramePassContext& ctx, string_view colorName, string_view depthName, const float4& clearColor,
                             RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief MRT 컬러 패스를 시작합니다.
         * @return 열었으면 true. 컬러 타깃 이름 중 이번 프레임에 없는 것이 있으면 **열지 않고** false 를 돌려줍니다(오류는 한 번 남깁니다) —
         *         없는 첨부의 핸들 0 은 백버퍼라, 그대로 열면 패스가 화면에 그립니다.
         */
        bool beginColorPassMrt( FramePassContext& ctx, const string_view* pColorNames, const float4* pTargetClearColor,
                                const RHIRenderPassLoadOp* pColorLoad, uint32 colorCount, string_view depthName,
                                RHIRenderPassLoadOp depthLoad );
        /** @brief 깊이 전용 패스를 시작합니다. */
        void beginDepthOnlyPass( FramePassContext& ctx, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief 일시 텍스처를 리소스 레지스트리에 canonicalName 으로 등록합니다(bindless SRV 자동 매칭).
         * @param canonicalName **미리 intern 된** 이름. 문자열을 받으면 패스마다 다시 intern 하게 됩니다.
         *        attachmentNames() 의 캐시를 넘기십시오.
         */
        void registerPassTexture( FramePassContext& ctx, const hashed_string& canonicalName, string_view attachmentName );
        /** @brief bindless 텍스처 바인딩을 커밋합니다(PassConstantValues 갱신 + 에뮬 백엔드 폴백 바인딩). */
        void commitBindlessTextureBindings( FramePassContext& ctx );

        // ------------------------------------------------------------------------------
        // 5) 커맨드 리스트 · 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() / executePacket() 공통: commandList 를 준비하고 device 에 연결합니다.
         * @param pCallerName 오류 로그에 찍을 부르는 쪽 이름
         * @return 성공하면 true 입니다. false 면 부르는 쪽이 바로 반환해야 합니다.
         */
        bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

        /**
         * @brief execute() / executePacket() 공통: graph 를 실행하고 commandList 를 제출합니다.
         * @return graph.execute() 결과
         */
        bool submitGraph( IRHIDevice* pDevice );

        /**
         * @brief execute() / executePacket() 공통의 뒷부분입니다. 스냅샷을 GPU 로 올리고, 기록 전에 만들어야 하는 것(콜백 · 상수버퍼
         *        슬롯 · 머티리얼 PSO)을 갖춘 뒤 그래프를 제출합니다.
         * @details 두 진입점이 이 걸음들을 각자 들면 한쪽만 빠지기 쉬워(시드 채우기가 갈리면 한쪽만 상수가 빠집니다) 하나로 둡니다.
         * @param pCallerName 오류 로그에 찍을 부르는 쪽 이름
         */
        bool uploadSceneAndSubmit( IRHIDevice* pDevice, const utf8* pCallerName );

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        [[nodiscard]] bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;
        /** @brief 어태치먼트 클리어 색을 반환하거나, 없으면 기본값을 반환합니다. */
        float4 getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const;
        /**
         * @brief 일시 텍스처와 그 SRV 를 한 번의 조회로 찾습니다. 없으면 빈 값입니다.
         * @details 이름 하나로 둘 다 필요한 자리(registerPassTexture)가 패스마다 여러 번 돕니다. 조회 한 번이라 해시도 한 번입니다.
         */
        TransientAttachmentPool::Attachment findTransientAttachment( string_view name ) const;
        /** @brief 일시 텍스처 핸들을 찾습니다. 없으면 0 입니다. */
        RHITextureHandle findTransient( string_view name ) const;
        /** @brief Present 소스 어태치먼트 이름을 정합니다. */
        string resolvePresentSource() const;
        /**
         * @brief 지금 뷰의 주 출력 대상을 고릅니다 — 렌더 텍스처 뷰는 자기 텍스처, 주 시점 · 화면 사각형 뷰는 주 출력(백버퍼 · 게임 뷰 RT),
         *        스크린샷 실행이면 캡처 텍스처입니다. Present 와 Canvas 가 같은 판단을 쓴다.
         */
        PresentTarget resolvePresentTarget() const;
        /** @brief 이 패스가 Swapchain 을 쓰는 마지막 패스면 true 입니다(스크린샷 캡처 → 백버퍼 복사를 이 패스 끝에서 한다). */
        bool isLastSwapchainWriter( const RenderGraphPassDesc* pPassDesc ) const;
        /** @brief 패스 타입으로 파이프라인 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;

        /**
         * @brief 패스 종류의 표(RenderPassTypeInfo)와 파이프라인 XML 의 패스 서술로 패스 PSO 를 만듭니다.
         * @param pRtvFormatOverride 컬러 RT 포맷을 이 배열로 고정합니다(Present 변종). nullptr 이면 표의 고정 포맷 → 출력 선언 순입니다
         */
        RHIPipelineStateHandle createPsoForPassType( RenderPassType passType, const RHIFormat* pRtvFormatOverride = nullptr );
        /** @brief 패스의 엔진 PSO 를 찾고, 없으면 표가 정한 대신할 패스(`_psoFallbackType`)의 PSO 를 반환합니다. */
        RHIPipelineStateHandle findPassPso( RenderPassType passType ) const;

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 **기록 시작 전에** 모두 만들어 둡니다.
         * @details 기록 중에는 PSO 를 만들 수 없고(상수버퍼 용량을 미리 늘리는 것과 같은 이유입니다), 패스는
         *          병렬로 기록되므로 그때 만들면 백엔드마다 다른 방식으로 깨집니다.
         */
        void ensureMaterialPsos();
        /**
         * @brief 패스 PSO 에 머티리얼 퍼뮤테이션 · 뷰 모드 · 컬 반전을 얹은 변형을 만듭니다. 얹을 것이 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 렌더 상태(블렌드 · 뎁스 · RT 포맷)는 **패스의 것을 그대로 물려받고** 셰이더만 갈아 끼웁니다.
         *          그 둘은 패스가 정하는 것이지 머티리얼이 정하는 것이 아닙니다. 예외는 컬 모드 하나입니다 — `bReverseCulling` 이면
         *          Back 과 Front 를 맞바꿉니다(거울 변환이 감김을 뒤집으므로). None 은 그대로입니다.
         */
        RenderPsoCache::MaterialPsoEntry createMaterialPsoVariant( RHIPipelineStateHandle passPso, RenderPassType passType,
                                                                   const GpuShaderPermutation* pPermutation, RenderViewMode viewMode,
                                                                   bool bReverseCulling );
        /**
         * @brief 이번 프레임 배치 수에 맞춰 상수버퍼 슬롯을 **기록 시작 전에** 늘려 둡니다(드로우마다 하나씩 나가므로).
         * @details 기록 중에는 버퍼 생성 · bindless 등록을 할 수 없습니다. 지난 프레임의 최대 사용량(하이워터)도 바닥값으로 봅니다.
         */
        void ensurePassCbCapacityForFrame();
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다(셋업 전용).
         * @details 기록 중에는 만들 수 없습니다. 버퍼 생성과 `registerBindlessResource` 는 bindless 레지스트리를 바꾸고,
         *          패스 콜백은 태스크 워커에서 병렬로 돕니다(`assertRegistryMutableNow` 가 감시하는 규칙).
         *          그래서 PSO 를 모두 등록한 뒤 여기서 한 번에 만듭니다.
         */
        void ensureMaterialFallbackBuffers();
        /**
         * @brief 주 출력에 그리는 패스(Present · Canvas)의 PSO 를 **대상 포맷별로** 찾습니다.
         * @details 주 출력의 대상은 둘입니다. 백버퍼(포맷은 디바이스가 실제로 채택한 값, Vulkan 은 서피스가
         *          B8G8R8A8 만 줄 수 있습니다)와 에디터 GameView RT(R8G8B8A8)입니다. PSO 의 렌더 타깃 포맷이 대상과
         *          다르면 Vulkan 은 렌더 패스 비호환으로 검증 레이어가 매 프레임 웁니다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같은 방식입니다.
         *          주 출력에 그리는 패스(Present · Canvas)만 그 키가 포맷이라 (패스, 포맷) 맵 하나로 충분합니다.
         *          **조회만 합니다** — 패스 실행(태스크 워커) 중에 불리므로 없다고 만들면 안 됩니다. 없으면 엔진 PSO 로 물러나고 한 번 알립니다.
         */
        RHIPipelineStateHandle findOutputPso( RenderPassType passType, RHIFormat targetFormat );
        /** @brief 주 출력에 그리는 패스(Present · Canvas)가 그릴 수 있는 대상 포맷(백버퍼 · 오프스크린 · 캡처)의 PSO 를 셋업에서 미리 만듭니다. */
        void buildOutputPsoVariants();

    private:
        IRHIDevice* _pDevice;
        /**
         * @brief 렌더 패스 · 파이프라인 에셋 캐시입니다. `initialize` 뒤에만 있고 `shutdown` 이 비웁니다.
         * @details 언리얼의 RHI 가 렌더 패스 *에셋*을 모르듯, 소유는 렌더러의 것입니다. 디바이스 추상(RHI)이 이것을
         *          들면 RHI 가 Renderer 를 include 하게 됩니다.
         */
        unique_ptr<RenderPipelineAssetCache> _renderPipelineAssetCache;
        IRHIDevice*                          _pCmdOwnerDevice;
        unique_ptr<IRHICommandList>          _frameCmd;
        IRHICommandList*                     _pCmd;
        Scene*                               _pScene;
        TaskManager*                         _pTaskManager;
        GpuScene                             _gpuScene;
        /// @brief 씬 직접 경로(`execute( pScene )`, 에디터 · 테스트)가 쓰는 빌더입니다. 패킷 경로에서는 EngineLoop 의 것이 대신합니다.
        GpuSceneBuilder     _sceneBuilder;
        RenderPipelineAsset _pipelineResource;
        RenderGraph         _graph;
        string              _pipelinePath;
        float4              _clearColor;
        /// @brief 주 시점 — 파이프라인이 선언한 첨부(창 크기로 만들고 구성이 바뀔 때만 다시 만든다) · TAA 기록 · 직렬 리스트 · 출력 사각형.
        ViewTarget _mainView;
        /// @brief 추가 뷰들(카메라 id 순서가 아니라 처음 본 순서). 뷰마다 자기 풀 · 컬링 칸을 든다.
        vector<unique_ptr<ViewTarget>> _listExtraView;
        /// @brief 지금 그리는 뷰입니다. 프레임 밖에서는 늘 `_mainView` 입니다.
        ViewTarget* _pActiveView;
        /**
         * @brief 프레임 단위 패스 상태입니다(직렬 경로에서 쓰고, 병렬 패스의 시드가 됩니다).
         * @details 병렬 기록에서는 패스마다 이것을 복사해 각자의 커맨드 리스트 · 상수 버퍼를 붙입니다.
         */
        FramePassContext _frameCtx;
        /**
         * @brief 패스 슬롯별 컨텍스트입니다. 프레임마다 `_frameCtx` 를 **대입**해 씁니다(용량이 남아 힙을 만지지 않습니다).
         * @details 패스마다 지역 복사본을 만들면 상수 값 목록 · 레지스트리 맵이 프레임마다 패스 수만큼 새로
         *          자랍니다. 크기는 병렬 기록 **전**(`submitGraph`)에 맞춥니다. 기록 중에 늘리면 워커끼리 경합합니다.
         *          마지막 칸은 이름을 못 찾은 패스의 몫입니다.
         */
        vector<FramePassContext> _listPassContext;
        /// @brief 씬 직접 경로가 내보내는 스냅샷입니다. 바꿔치기로 저장소가 돌아옵니다.
        GpuSceneSnapshot _sceneSnapshotScratch;
        /// @brief 주 시점의 패스 시드입니다. 추가 뷰가 이것에서 출발해 자기 값(뷰 행렬 · 플래그 · 컬링 칸)만 덮어씁니다. 프레임마다 대입이라 용량이 남습니다.
        FramePassContext _mainSeedScratch;
        /// @brief 씬 직접 경로(`execute( pScene )`)가 추가 뷰를 고르는 스케줄러와 요청 목록입니다(패킷 경로에서는 EngineLoop 의 것).
        RenderViewScheduler       _directViewScheduler;
        vector<RenderViewRequest> _listDirectViewScratch;
        /** @brief 이번 프레임 배치가 쓰는 PSO 변형 하나의 조건(퍼뮤테이션, 컬 반전)입니다. 퍼뮤테이션이 없으면 kInvalidShaderPermutation 입니다. */
        struct MaterialPsoRequest
        {
            uint32 _shaderPermutation{ kInvalidShaderPermutation };
            uint8  _bReverseCulling{ SW_FALSE };
        };
        /// @brief `ensureMaterialPsos` 가 이번 프레임 배치에서 모으는 변형 조건입니다(중복 없음). 프레임마다 다시 채웁니다.
        vector<MaterialPsoRequest> _listOpaquePsoRequestScratch;
        vector<MaterialPsoRequest> _listTransparentPsoRequestScratch;
        /// @brief 배치 하나가 한 프레임에 몇 개의 지오메트리 패스에서 그려지는지의 어림값입니다(그림자 · 프리패스 · 불투명 · 반투명).
        static constexpr uint32 _s_kDrawCbPassEstimate = 4;
        /// @brief 패스 · 드로우별 상수버퍼 슬롯 링입니다. 병렬 기록에서 드로우마다 하나씩 집어 갑니다.
        PassConstantRing _passCbRing;
        /**
         * @brief 이번 프레임의 뷰들입니다. 행렬 · 절두체 · 상수버퍼를 각자 소유합니다.
         * @details 뷰를 얻으면 그 뷰의 것이 딸려 옵니다. 주의: 이 값들을 뷰 밖 멤버로 흩어 두면 "이 값은 어느 뷰
         *          것인가" 를 사람이 기억해야 하고, 컬링 상수버퍼를 뷰끼리 나눠 쓰게 되어 뒤 업로드가 앞 디스패치를 덮어씁니다.
         */
        RenderView _arrView[static_cast<uint32>( RenderViewType::Count )];

        RHIConstantBufferSlot _instanceAnimCb;
        RHIConstantBufferSlot _meshMorphCb;
        RHIConstantBufferSlot _meshSkinCb;
        /// @brief GPU 가 변형한 정점 풀입니다. RT 소유입니다(GpuMeshMorphPool 참고).
        GpuMeshMorphPool _meshMorphPool;
        /// @brief 정점 애니메이션(VAT) 표 풀입니다. RT 소유입니다(GpuVertexAnimationPool 참고).
        GpuVertexAnimationPool _vertexAnimationPool;
        /// @brief `setMeshMorphDiag` 가 준 값입니다. 음수면 전역 변수 `gv_morphDiag` 를 따릅니다.
        int32 _meshMorphDiagOverride;
        /// @brief `setDrawMergeEnabled` 가 준 값입니다. 음수면 전역 변수 `gv_drawMerge` 를 따릅니다.
        int32 _drawMergeOverride;
        /// @brief `setVertexPoolEnabled` 가 준 값입니다. 음수면 전역 변수 `gv_vertexPool` 을 따릅니다.
        int32 _vertexPoolOverride;
        /// @brief 이번 프레임의 씬 간접 드로우 호출 수입니다. 패스가 병렬로 기록하므로 원자입니다.
        atomic<uint32> _indirectDrawCallCount;
        /** @brief 지난 프레임의 타임스탬프입니다(마이크로초, 프레임 시작 기준 누적 + 기준점의 GPU 시계). 엔진 표와 Tracy 가 같은 값을 쓴다. */
        RHIGpuTimestampFrame _gpuTimestampFrame;
        /** @brief 패스 하나의 GPU 스코프입니다. 그 칸을 만든 패스 이름과 프로파일러 슬롯 · 외부 프로파일러 지점을 담습니다. */
        struct GpuPassScope
        {
            string                 _passName;
            const ProfileZoneSite* _pZoneSite{ nullptr }; ///< Tracy GPU 구간 이름(패스 이름, 프로세스 수명 사본)
            uint32                 _profilerSlot{ 0xFFFFFFFFu };
        };
        /**
         * @brief 패스 번호 → GPU 스코프 슬롯입니다. 패스 이름이 그대로면 매 프레임 슬롯만 꺼냅니다.
         * @details `registerScope` 는 등록된 스코프 모두(~80)를 문자열 비교로 훑으므로 렌더 스레드가 매 프레임 부르지 않고,
         *          칸의 패스 이름이 바뀔 때만 부릅니다. 프로파일러는 이름 **포인터**를 쥐므로 이름은 intern 아레나(프로세스 끝까지
         *          제자리)에 둡니다. 주의: 자라며 옮겨지는 저장소의 `string` 을 넘기면 짧은 이름(`GPU.Shadow` 처럼 문자열 객체 안에
         *          드는 것)의 포인터가 옮겨진 뒤의 빈자리를 가리킵니다.
         */
        vector<GpuPassScope> _listGpuPassScope;
        /// @brief `GPU.Compute` · `GPU.Frame` 의 프로파일러 슬롯입니다. 처음 한 번 등록합니다(프레임마다 선형 탐색을 하지 않습니다).
        uint32 _gpuComputeScopeSlot;
        uint32 _gpuFrameScopeSlot;
        /// @brief 외부 프로파일러(Tracy) GPU 타임라인입니다. 엔진 표와 같은 타임스탬프를 쓴다(쿼리는 한 벌).
        GpuTimelineExporter _gpuTimeline;
        /// @brief 패스 번호 → Tracy 지점입니다. `_listGpuPassScope` 에서 프레임마다 채운다(할당을 되풀이하지 않게 든다).
        vector<const ProfileZoneSite*> _listGpuPassSite;
        /// @brief GPU 시계를 읽지 못한 디바이스입니다. 막히는 읽기(DX11 · Vulkan)를 프레임마다 다시 하지 않는다.
        const IRHIDevice* _pGpuTimelineFailedDevice;
        /// @brief 마지막 프레임의 값입니다(getLastIndirectDrawCallCount).
        uint32 _lastIndirectDrawCallCount;
        /// @brief `setInputRoleEnabled( role, false )` 가 켠 비트입니다. 그 역할의 입력은 걸지 않습니다.
        uint32 _disabledInputRoleMask;
        /// @brief 진단(`-gv_morphDiag=3`)이 올리는 번호표 정점입니다. 스크래치라 프레임 밖에서는 의미가 없습니다.
        vector<GpuMorphVertex> _listScratchMorphTag;
        /// @brief 이번 프레임 모프 대상 메시입니다. 프레임마다 할당하지 않으려고 들고 있습니다.
        vector<Mesh*> _listScratchMorphMesh;
        /// @brief 이번 프레임 스킨드 메시 목록입니다(모프 풀의 스킨 구간 순서, 프레임마다 재사용).
        vector<Mesh*> _listScratchSkinMesh;
        /// @brief 이번 프레임 VAT 메시 목록입니다(프레임마다 재사용).
        vector<Mesh*> _listScratchVertexAnimationMesh;
        /// @brief 씬 라이트 구조버퍼입니다. RT 소유이고 포워드 · 디퍼드가 같은 버퍼를 읽습니다.
        GpuLightBuffer _lightBuffer;
        /// @brief 씬 직접 경로에서 라이트를 모으는 버퍼입니다. 프레임마다 할당하지 않으려고 들고 있습니다.
        vector<GpuLight> _listScratchLight;

        /** @brief 뷰 하나를 얻습니다. 그 뷰의 행렬 · 절두체 · 상수버퍼가 함께 옵니다. */
        RenderView&       view( RenderViewType type ) { return _arrView[static_cast<uint32>( type )]; }
        const RenderView& view( RenderViewType type ) const { return _arrView[static_cast<uint32>( type )]; }

        /** @brief 컴퓨트가 드로우 커맨드를 만드는 경로를 이번 프레임에 쓸 생각인지 반환합니다(업로드 전에 GpuScene 에 알립니다). */
        bool usesGpuGeneratedCommands() const;

        /**
         * @brief 지난 프레임의 패스별 GPU 시간을 프로파일러에 `GPU.<패스>` 로 넣습니다.
         * @details GPU 타임스탬프가 없으면 GPU 비용을 `RT.BeginFrame`(백프레셔) 같은 대리값으로
         *          추측하거나 패스를 지워 가며 차이로 구해야 하고, 그런 추측(클리어 · 포맷 비용 같은)은 쉽게 틀립니다.
         */
        void reportGpuPassTimes( IRHIDevice* pDevice );
        /**
         * @brief 방금 읽은 타임스탬프를 외부 프로파일러(Tracy)의 GPU 타임라인으로 냅니다. 출력이 꺼져 있으면 아무것도 하지 않습니다.
         * @details 디바이스가 바뀌면 GPU 컨텍스트를 새로 열고(그때 GPU 시계를 한 번 읽는다), 싼 시계(DX12 · GL)는 몇 프레임마다 다시 맞춘다.
         */
        void exportGpuTimeline( IRHIDevice* pDevice );
        /**
         * @brief 패스 @p passIndex 의 `GPU.<패스>` 프로파일러 슬롯입니다. 그 칸의 패스 이름이 바뀌었을 때만 다시 등록합니다.
         * @details 파이프라인을 다시 읽어 패스 구성이 바뀌어도 이름 비교가 알아챕니다(칸마다 문자열 비교 한 번).
         */
        uint32 gpuScopeSlotFor( size_t passIndex, const string& passName );
        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다(instanceanim.hlsl).
         * @details **컬링보다 먼저** 돌아야 합니다. 순서가 뒤집히면 컬링이 이번 프레임에 움직이기 전의
         *          바운드로 판정합니다. 회전을 요청한 인스턴스가 없으면 통째로 건너뜁니다.
         */
        void dispatchInstanceAnimation( uint32 instanceCount );
        /**
         * @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트 → 결과).
         * @details **컬링보다 앞**이어야 합니다. 모프는 회전과 달리 실제로 모양과 바운드를 바꾸므로,
         *          뒤에 두면 컬링이 한 프레임 늦은 모양으로 판정합니다.
         */
        void dispatchMeshMorph();
        /**
         * @brief 스킨드 메시들의 정점을 GPU 가 본 팔레트로 섞습니다(바인드 포즈 → 모프 풀의 스킨 구간). 모프와 같이 **컬링보다 앞**입니다.
         * @details 디스패치 하나가 모든 스킨드 메시를 돕니다 — 정점마다의 팔레트 행 번호에 메시의 팔레트 시작이 이미 더해져 있습니다.
         */
        void dispatchMeshSkin();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 적습니다. **업로드 전에** 부릅니다.
         * @details 오프셋은 배치 표(g_SwBatches)에 실려 upload() 가 올리므로 표는 업로드 시점에 완성돼야 합니다. 디스패치(dispatchMeshMorph)는 커맨드 리스트가 열린 뒤 따로 돕니다.
         */
        void prepareMeshMorphPool();
        /** @brief 지금 적용되는 모프 진단 모드입니다. 오버라이드가 있으면 그것, 없으면 `gv_morphDiag` 입니다. */
        int32 getEffectiveMeshMorphDiag() const;
        /** @brief 시간 구동 컴퓨트가 읽을 절대 시간(초)입니다. `setAnimationTimeOverride` 가 준 값(0 이상, 배포본 아님), 없으면 `_animTimer` 의 누적 시간입니다. */
        float32 getAnimationTime() const;
        /** @brief 씬 배치를 멀티 드로우로 묶을지 반환합니다. `setDrawMergeEnabled` 가 준 값, 없으면 전역 `gv_drawMerge` 입니다. */
        bool isDrawMergeEnabled() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpucull/instancesort.hlsl).
         * @details 컬링 결과는 절두체에 종속이라 뷰(메인 · 그림자)마다 자기 인자 · 목록을 따로 만듭니다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /**
         * @brief 컬링 칸 하나를 컬링 · 정렬합니다(그 뷰의 절두체 · 자기 상수버퍼). 돌렸으면 true 입니다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 표를 정렬에 건다), 고정 뷰(주 · 그림자)면 nullptr.
         */
        bool dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const ViewTarget* pExtraView );
        /**
         * @brief 인스턴스 애니메이션에 넣는 절대 시간(초)입니다.
         * @details 각도를 프레임마다 누적하지 않고 **이 절대 시간에서 매번 새로 만듭니다**. 누적하면 프레임
         *          간격의 흔들림이 그대로 쌓여 백엔드 · 실행마다 다른 각도가 나오고, 스크린샷 비교가 불가능해집니다.
         *          렌더러가 자기 시계를 갖습니다. 델타를 여기까지 실어 나르지 않아도 되고, 렌더 스레드에서
         *          게임 시간을 만지지 않습니다.
         */
        GameTimer _animTimer;
        /**
         * @brief 머티리얼 없는 배치에 거는 0 으로 채운 원소 하나짜리 구조버퍼입니다. **stride 마다 하나**입니다.
         * @details DX12 에서 아무것도 안 걸린 t9 를 읽으면 GPU 폴트(디바이스 제거)입니다. 어떤 드로우도 빈 슬롯으로
         *          나가지 않게 항상 유효한 버퍼를 겁니다(언리얼의 기본 머티리얼 자리).
         *
         *          언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같습니다.
         *          주의: 고정 크기 원소 하나를 모든 셰이더에 공용으로 걸면 셰이더의 `SwMaterialData` stride 와 어긋나
         *          DX11 디버그 레이어가 드로우마다 "structure stride 256 vs 24" 같은 경고를 냅니다.
         *
         *          키는 stride 입니다. 셋업(ensureMaterialFallbackBuffers)에서만 만들고 기록 중에는 조회만 합니다.
         */
        unordered_map<uint32, RHIStructuredBufferSlot> _mapMaterialFallback;
        /// @brief 순번 표가 없는 뷰(주 · 그림자)의 정렬 디스패치가 t2 에 거는 원소 하나짜리 자리표입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않는다.
        RHIStructuredBufferSlot _transparentRankPlaceholder;
        /// @brief 엔진 패스 PSO · Present PSO · 머티리얼 변형과 그 바인딩 레이아웃입니다. 소유와 해제 순서는 캐시가 압니다.
        RenderPsoCache                       _psoCache;
        unordered_map<hashed_string, uint32> _mapPassNameToIndex;
        RHITextureHandle                     _outputRenderTarget;
        /**
         * @brief Present 결과를 받아 두는 텍스처입니다(0 = 안 받음). 스크린샷이 **최종 화면**을 보게 하는 길입니다.
         * @details 스크린샷은 트랜지언트만 읽을 수 있고 백버퍼는 핸들이 없습니다. 주의: Present 가 **읽는** 첨부를
         *          찍으면 톤맵과 Present 에 합친 후처리가 스크린샷에서 빠집니다. Present 결과를 받아 두면 둘 다 담깁니다.
         */
        RHITextureHandle _presentCapture;
        /// @brief 캔버스(화면 2D) 렌더러입니다 — 아틀라스 거울 · 사각형 버퍼 · 일괄 드로우(렌더 스레드).
        CanvasRenderer _canvasRenderer;
        /// @brief 그릴 캔버스입니다 — 패킷 · `setCanvasFrame` 과 바꿔치기로 받는다. 기록 중에는 읽기만 한다.
        CanvasFrameData _canvasFrame;
        /// @brief Swapchain 을 쓰는 마지막 패스(선언 순서 — 그래프가 같은 출력의 쓰기를 선언 순서로 잇는다). 콜백을 묶을 때 정한다.
        const RenderGraphPassDesc*  _pLastSwapchainWriter;
        string                      _statusMessage;
        RenderGraphExecutionContext _graphContext;

        // 아래는 8 바이트보다 작은 필드입니다. 사이에 끼면 패딩이 생기므로 큰 것부터 끝에 모아 둡니다.
        FrameLightState _frameLight; ///< 크기가 8 의 배수가 아니라(116) 4 바이트 필드와 짝을 짓습니다
        /// @brief 주 출력(백버퍼 · 게임 뷰 RT)의 크기입니다. 주 시점의 풀은 이것 × 사각형 × 해상도 배율이고, 화면 사각형 뷰 · Present 캡처는 이 크기다.
        uint32 _outputWidth;
        uint32 _outputHeight;
        /// @brief Present 캡처 텍스처의 크기입니다(출력 크기가 바뀌면 다시 만든다).
        uint32 _presentCaptureWidth;
        uint32 _presentCaptureHeight;
        /// @brief 마지막 프레임에 그린 추가 뷰 수입니다.
        uint32 _lastRenderedExtraViewCount;
        /// @brief 씬 직접 경로의 출력 크기(`setOutputSizeOverride`, 0 이면 백버퍼)입니다.
        uint32 _directOutputWidth;
        uint32 _directOutputHeight;
#if !defined( SW_SHIPPING )
        /// @brief `setAnimationTimeOverride` 가 준 시각(초)입니다. 음수면 `_animTimer` 를 따릅니다.
        float32 _animationTimeOverride;
#endif
        /// @brief 진단(`-gv_morphDiag=2|3`)에서 정점 셰이더에 결과 대신 **레스트** 버퍼를 물렸는지 여부입니다.
        uint8 _bMorphBindsRest;
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지 여부입니다(가시 목록이 유효한가).
         * @details 드로우가 가시 목록을 걸지 말지 정하는 값입니다. 목록을 걸었는데 컬링이 안 돌면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 모두 같은 인스턴스를 그립니다.
         */
        uint8                  _bGpuCullingActive;
        uint8                  _bPresentCaptureEnabled; ///< `-gv_screenshot` 실행에서만 켬(전체 화면 복사 한 번이 더 붙음).
        FrameRendererStatus    _status;
        uint8                  _bCallbacksBound     : 1;
        uint8                  _bPassResourcesReady : 1;
        [[maybe_unused]] uint8 _reservedFlags       : 5;
        /**
         * @brief 현재 보기 방식(`RenderViewMode`)입니다.
         * @details 드로우 경로가 배치마다 읽고 UI 스레드가 씁니다. 값 하나뿐이라 atomic 으로 충분합니다.
         *          프레임 중간에 바뀌어도 최악은 한 프레임이 섞여 그려지는 것이고, PSO 변형은
         *          `ensureMaterialPsos` 가 그 프레임 시작에 읽은 모드로 이미 준비되어 있습니다.
         */
        atomic<uint8> _viewMode;
        /// @brief 셋업에 없는 출력 대상 포맷(Present · Canvas)을 만났다고 한 번만 알리기 위한 래치입니다.
        atomic<uint8> _bOutputPsoMissingLogged;
        /// @brief 머티리얼 폴백 stride 가 없다고 한 번만 알리기 위한 래치입니다(드로우 경로라 프레임마다 찍으면 안 됩니다).
        atomic<uint8> _bMaterialFallbackMissingLogged;
        /// @brief 컬러 타깃이 없어 패스를 건너뛴다고 한 번만 알리기 위한 래치입니다(패스 경로라 프레임마다 찍으면 안 됩니다).
        atomic<uint8> _bMissingColorTargetLogged;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 레벨끼리 병렬로 돈다
        // (RenderGraph::executeParallel). 비트필드로 두면 인접 비트를 쓰는 다른 패스와
        // 같은 바이트를 read-modify-write 해서 서로의 값을 날린다. 독립 원자 변수로 뺀다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행됐는지 여부입니다(ForwardOpaque 의 PSO 선택에 씁니다).
        atomic<uint8> _bHasExecutedDepthPrepass;
    };
} // namespace sw
