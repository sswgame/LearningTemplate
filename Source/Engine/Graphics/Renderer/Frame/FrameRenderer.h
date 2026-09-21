/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML을 로드하고 RenderGraph를 만든 뒤 Shadow/Forward/Deferred/Post를 실행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/FrameResourceRegistry.h"
#include "Engine/Graphics/Renderer/Frame/PassConstantRing.h"
#include "Engine/Graphics/Renderer/Frame/PassConstantValues.h"
#include "Engine/Graphics/Renderer/Frame/RenderPsoCache.h"
#include "Engine/Graphics/Renderer/Frame/RenderView.h"
#include "Engine/Graphics/Renderer/Frame/TransientAttachmentPool.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputContract.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"

namespace sw
{
    struct RenderFramePacket;
    struct ShaderCompileResult;

    class CameraComponent;
    class IRHICommandList;
    class IRHIDevice;
    class Material;
    class MaterialInstance;
    class RenderPassManager;
    class Scene;
    class ShaderBindingLayout;
    class TaskArgs;
    class TaskManager;

    /** @brief FrameRenderer 초기화/파이프라인 상태. */
    enum class FrameRendererStatus : uint8
    {
        Uninitialized = 0, ///< initialize 미호출
        Ready,             ///< 파이프라인 로드·그래프 준비 완료
        Failed             ///< initialize/loadPipeline 실패 (getStatusMessage)
    };

    /**
     * @class FrameRenderer
     * @brief 프레임 경로: RenderPipeline XML → RenderGraph → GpuScene / MeshComponents → IRHIDevice
     */
    class SW_API FrameRenderer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 디바이스·파이프라인 XML, 핫패스 서비스
        // ------------------------------------------------------------------------------
        /** @brief 빈 렌더러. initialize 전에 사용하지 마세요. */
        FrameRenderer();
        /** @brief GPU 자원을 해제합니다. */
        ~FrameRenderer();

        /** @brief 복사를 금지합니다. */
        FrameRenderer( const FrameRenderer& ) = delete;
        /** @brief 대입을 금지합니다. */
        FrameRenderer& operator=( const FrameRenderer& ) = delete;

        /** @brief 디바이스와 파이프라인 XML로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, string_view pipelineXmlPath = {} );
        /** @brief 디바이스·TaskManager·파이프라인 XML로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                         string_view pipelineXmlPath = {} );
        /** @brief 핫패스 서비스(TaskManager)를 연결합니다. */
        void bindServices( TaskManager* pTaskManager );
        /** @brief GPU 자원을 해제하고 종료합니다. */
        void shutdown();
        /**
         * @brief 렌더 패스·파이프라인 에셋 캐시. `initialize` 뒤에만 있고 `shutdown` 이 비웁니다.
         * @details 예전에는 `IRHIDevice` 가 이것을 소유했다 — 디바이스 추상(RHI)이 렌더러의 에셋 개념을
         *          들고 있어 RHI 가 Renderer 를 include 했다. 언리얼의 RHI 가 렌더 패스 *에셋*을 모르듯,
         *          소유는 렌더러의 것이다.
         */

        // ------------------------------------------------------------------------------
        // 2) 파이프라인 · 실행 — XML 로드, execute / executePacket
        // ------------------------------------------------------------------------------
        /** @brief RenderPipeline XML에서 그래프를 다시 만듭니다 (동기 로드). 패스 콜백은 한 번 바인딩합니다. */
        bool loadPipeline( string_view pipelineXmlPath );
        /** @brief 컴파일된 그래프를 실행합니다. scene이 있으면 자기 빌더로 스냅샷을 만들어 패킷 경로와 같은 길로 올립니다. */
        bool execute( IRHIDevice* pDevice, Scene* pScene = nullptr );
        /** @brief 렌더 스레드 경로: 미리 만든 packet.GpuScene을 씁니다 (Scene 미접근). */
        bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );

        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        /** @brief Ready 상태면 true. */
        bool isReady() const { return _status == FrameRendererStatus::Ready; }

        /**
         * @brief GPU 메시 모프 진단 모드를 코드에서 고릅니다 (`-gv_morphDiag` 와 같은 값 체계, 음수 = 전역 변수 따름).
         * @details 테스트가 쓴다. 2(컴퓨트 없이 레스트 버퍼를 정점 셰이더에 물림)의 정답은 **레스트 포즈와 같은
         *          그림**이라, 이 모드 하나로 "정점 셰이더의 풀 읽기가 네 백엔드에서 같은가" 를 픽셀로 단언할 수
         *          있다. OpenGL 드라이버가 early-return 모양의 `SwMorphElementOf` 를 잘못 컴파일해 한 칸 어긋난
         *          원소를 읽던 버그가 정확히 이 단언에 걸린다(binding.hlsli 주석 참고).
         */
        void setMeshMorphDiag( int32 mode ) { _meshMorphDiagOverride = mode; }
        /**
         * @brief 같은 PSO·머티리얼의 연속 배치를 멀티 드로우 하나로 묶을지 (기본 켬, 전역 `gv_drawMerge` 를 덮어쓴다).
         * @details 끄면 배치마다 한 번씩 부른다 — 백엔드가 멀티 드로우를 못 하면(DX11) 어차피 그렇다. 테스트가 켬/끔의 그림을 비교한다.
         */
        void setDrawMergeEnabled( bool bEnabled ) { _drawMergeOverride = bEnabled ? 1 : 0; }
        /** @brief 정점 풀을 쓸지 (기본 켬, 전역 `gv_vertexPool` 을 덮어쓴다). 끄면 메시마다 자기 정점 버퍼 — 진단·A/B 용. */
        void setVertexPoolEnabled( bool bEnabled ) { _vertexPoolOverride = bEnabled ? 1 : 0; }
        /** @brief 마지막 프레임이 낸 씬 간접 드로우 호출 수(모든 패스 합). 배치 수보다 작으면 묶인 것이다. */
        uint32 getLastIndirectDrawCallCount() const { return _lastIndirectDrawCallCount; }
        /**
         * @brief 풀스크린 패스가 이 역할의 입력을 **걸지 않게** 합니다 (쇼 플래그 — 언리얼의 r.AmbientOcclusion.Levels=0 자리).
         * @details 셰이더는 그 인덱스를 SW_INVALID_INDEX 로 읽어 폴백한다(AO 는 1). "이 입력이 실제로 그림을 바꾸는가" 를
         *          같은 프레임 안에서 비교할 수 있다 — SSAO 가 매 프레임 돌고 버려지던 것을 픽셀로 잡는 데 썼다.
         */
        void setInputRoleEnabled( RenderPassInputRole role, bool bEnabled );
        /** @brief 초기화/파이프라인 상태를 반환합니다. */
        FrameRendererStatus getStatus() const { return _status; }
        /** @brief Failed일 때 원인 메시지 (그 외 empty). */
        const string& getStatusMessage() const { return _statusMessage; }
        /** @brief 렌더 그래프를 반환합니다. */
        const RenderGraph& getGraph() const { return _graph; }
        /** @brief GpuScene을 반환합니다. */
        const GpuScene& getGpuScene() const { return _gpuScene; }
        GpuScene&       getGpuScene() { return _gpuScene; }
        /** @brief 셰이더 핫리로드 알림 — 영향받는 PSO 바인딩 레이아웃을 다시 만든다. */
        void onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result );
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 PPM(P6) 으로 저장합니다 — 백엔드별 시각 검증용.
         * @details 프레임 경로가 아니다(GPU 를 기다린다). 창 캡처가 백엔드마다 되고 안 되고가 갈려서,
         *          네 백엔드를 같은 기준으로 비교하려면 이쪽을 쓴다.
         */
        bool dumpTransientToPpm( string_view attachmentName, string_view outFilePath );

        /**
         * @brief Present 결과를 텍스처로도 받아 둘지 정합니다 (`-gv_screenshot` 실행 전용).
         * @details 켜면 Present 가 백버퍼 대신 캡처 텍스처에 그리고 그것을 백버퍼로 복사한다 —
         *          전체화면 복사 한 번이 더 붙으므로 평소에는 꺼 둔다.
         */
        void setPresentCaptureEnabled( bool bEnabled );
        /** @brief 이 렌더러가 Present 결과를 받아 두고 있으면 true. */
        bool isPresentCaptureEnabled() const { return _bPresentCaptureEnabled != SW_FALSE && _presentCapture != 0; }
        /**
         * @brief 받아 둔 Present 결과(= 화면에 나간 그림)를 CPU 로 읽습니다.
         * @details 포맷은 늘 `constant::kBackBufferFormat` 이다. 테스트가 **최종 화면**을 픽셀로
         *          비교하는 유일한 길이다 — 트랜지언트만 읽을 수 있고 백버퍼는 핸들이 없다.
         */
        bool readbackPresentCapture( vector<uint8>& outByte, RHITextureMipSpan& outLayout );
        /** @brief 받아 둔 Present 결과(= 화면에 나간 그림)를 PPM 으로 씁니다. */
        bool dumpPresentCaptureToPpm( string_view outFilePath );

    private:
        /** @brief 읽어 온 바이트를 PPM(P6) 파일로 씁니다 — 트랜지언트 덤프와 Present 캡처 덤프가 같이 쓴다. */
        static bool writePpm( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath );

    public:
        /**
         * @brief 화면에 나간 첨부의 이름 — Present 패스가 입력으로 받는 것입니다. 없으면 빈 문자열.
         * @details 스크린샷 기본값이 `"SceneColor"` 리터럴이라 **디퍼드에서는 한 장도 못 찍었다**
         *          (디퍼드 첨부 목록에 그 이름이 없다 — 읽기 실패 로그만 남고 파일은 안 생긴다).
         *          찍고 싶은 것은 늘 "지금 보이는 그림" 이므로 파이프라인에 물어본다.
         */
        string_view getPresentedAttachmentName() const;
        /**
         * @brief 지금 살아 있는 트랜지언트 목록을 엔진 레지스트리에 공개합니다 (에디터 패널이 읽는다).
         * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어지므로 그때 한 번 부르면 된다 —
         *          매 프레임 부를 이유가 없다.
         */
        void publishRenderTargets() const;
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 옵니다 (밉 0). 테스트가 백엔드 간 픽셀을 비교하는 데 쓴다 — GPU 를 기다린다.
         * @param outFormat 첨부의 RHIFormat (채널 순서 해석용).
         */
        bool readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat );

        /**
         * @brief 씬 지오메트리 보기 방식을 정합니다 (Lit/Unlit/Wireframe).
         * @details 다음 프레임의 `ensureMaterialPsos` 가 그 모드의 PSO 변형을 만들고 드로우가 그것을 고른다 —
         *          모드를 바꾼 프레임에 셰이더 컴파일이 한 번 끼고, 그 뒤로는 캐시에서 나온다.
         *          렌더 스레드가 드로우마다 읽으므로 atomic 이다(락을 걸 자리가 아니다).
         */
        void setViewMode( RenderViewMode viewMode );
        /** @brief 현재 보기 방식. */
        RenderViewMode getViewMode() const;

        /** @brief 패스 타입에 대응하는 엔진 PSO. 없으면 0. */
        RHIPipelineStateHandle getEnginePso( RenderPassType passType ) const;
        /**
         * @brief 이 배치를 그릴 PSO — 머티리얼 퍼뮤테이션 변형이 있으면 그것, 없으면 패스 PSO 그대로.
         * @details 드로우 경로가 배치마다 부르는 조회다(읽기 전용). 캐시는 `ensureMaterialPsos` 가 기록 전에 채운다.
         */
        RHIPipelineStateHandle psoForBatch( RHIPipelineStateHandle passPso, const GpuMeshBatch& batch ) const;
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터를 돌려줍니다 (셰이더 경로·define·렌더 상태). 모르는 PSO 면 nullptr.
         * @details 어떤 퍼뮤테이션이 실제로 걸렸는지 밖에서 볼 수 있는 유일한 창이다 — 픽셀로는 안 보이는
         *          차이(알파 경로가 컴파일됐는가 같은)를 테스트가 여기서 확인한다.
         */
        bool findPsoDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;

    private:
        /**
         * @brief 패스 하나를 기록하는 동안의 로컬 상태입니다.
         * @details 병렬 기록에서는 패스마다 하나씩 존재합니다. 예전에는 이 값들이 전부
         *          FrameRenderer 멤버였고 onGraphPassExecute 가 _pCmd 를 저장/복원했는데,
         *          그건 "한 번에 한 패스만 돈다" 는 전제라 병렬 기록에서 서로를 덮어썼습니다.
         *          상수 버퍼도 패스마다 따로 있어야 합니다. 하나를 공유하면 기록은 지연이고
         *          버퍼 쓰기는 즉시라, replay 시점엔 마지막 writer 의 값만 남습니다.
         */
        struct FramePassContext
        {
            IRHICommandList* _pCmd{ nullptr };
            /** @brief 엔진 PassCB 값 (이름 기반). ShaderBindingBinder 가 리플렉션 오프셋에 기록. */
            PassConstantValues _passValues{};
            /** @brief `g_World` — 인스턴스 버퍼가 없는 드로우(풀스크린·픽스처)의 월드 행렬. 씬 메시는 인스턴스 버퍼에서 읽는다. */
            float4x4           _world{};
            RHIBufferHandle    _passCb{ 0 };
            RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
            /** @brief 패스 스코프 이름→리소스 레지스트리. 패스 시작마다 새로 시작(reset) — 병렬 기록 시
             *         패스마다 독립이어야 하므로 FrameRenderer 공유 멤버가 아니라 여기 둔다. */
            FrameResourceRegistry _resourceRegistry{};
            /// @brief 마지막으로 엔진 상수버퍼를 올린 시점의 (버퍼, 값 버전, 레지스트리 버전).
            ///        셋이 그대로면 그 드로우는 버퍼를 다시 만들 필요가 없다.
            RHIBufferHandle _lastCbBuffer{ 0 };
            uint32          _lastCbValuesVersion{ 0 };
            uint32          _lastCbRegistryVersion{ 0 };
            /**
             * @brief 마지막으로 리소스를 **실제로 건** PSO.
             * @details 슬롯 상태는 PSO 단위다 — `setPipelineState` 는 이전 PSO 가 건 t/u 슬롯이 다음 PSO 로
             *          새지 않게 슬롯 상태를 통째로 비운다. 그래서 값·레지스트리가 그대로여도 PSO 가 바뀌었으면
             *          다시 걸어야 한다. 이걸 빼먹으면 배치가 퍼뮤테이션 PSO 로 갈아탄 순간 t9(g_SwMaterials)
             *          가 **바인딩되지 않은 채** 드로우가 나가고, Vulkan 은 초기화되지 않은 디스크립터를 읽어
             *          디바이스를 잃는다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
             */
            RHIPipelineStateHandle _lastBindPso{ 0 };
            /**
             * @brief 이 패스가 어떤 뷰의 컬링 결과를 쓸지.
             * @details 그림자 패스만 Shadow 이고 나머지는 Main 이다. 컬링 결과는 절두체에 종속이라
             *          뷰를 잘못 고르면 그림자 드리우개가 사라지거나 화면 밖 물체를 그린다.
             */
            RenderViewType _cullView{ RenderViewType::Main };
            /// @brief 이 드로우 그룹의 루트 상수 값 — 머티리얼 원소 수. 배치마다 다른 값(인스턴스 시작·모프 풀·정점 풀)은
            ///        배치 표(g_SwBatches)와 인스턴스 슬롯 스트림이 주므로 그룹 안에서 루트 상수를 다시 걸지 않는다.
            ///        PassCB 에 넣으면 한 패스의 드로우들이 서로를 덮어쓴다(binding.hlsli 1-0 참고).
            uint32 _drawMaterialCount{ 0 };
            /// @brief bindForDraw 가 마지막으로 조회한 PSO→레이아웃. 같은 PSO 로 연속 드로우할 때
            ///        layoutForPso() 의 뮤텍스+해시맵 조회를 건너뛰는 패스-로컬 1-entry 캐시.
            RHIPipelineStateHandle     _lastLayoutPso{ 0 };
            const ShaderBindingLayout* _pLastLayout{ nullptr };

            /**
             * @brief "마지막으로 건 것" 캐시를 전부 잊습니다. PSO·버퍼 핸들이 무효가 되는 자리(디바이스 교체)에서 부릅니다.
             * @details 핸들 값은 **디바이스 안에서만** 정체성이다. 새 디바이스의 PSO 는 옛 디바이스의 PSO 와 같은 값을
             *          받을 수 있고(둘 다 첫 PSO 가 같은 번호), 그러면 `_lastLayoutPso == pso` 가 참이 되어 이미 파괴된
             *          레이아웃(`_pLastLayout`)을 쓰고 리소스 재바인딩을 건너뛴다 — 백엔드 교체 뒤 아무것도 안 그려지던
             *          원인이다. 파괴와 함께 캐시도 지워야 "같은 값 = 같은 것" 이 성립한다.
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

        // ------------------------------------------------------------------------------
        // 4) 패스 자원 · 콜백 · 드로우
        // ------------------------------------------------------------------------------
        /** @brief 패스용 상주 GPU 자원을 확보합니다. */
        void ensurePassResources();
        /** @brief 패스용 상주 GPU 자원을 해제합니다. */
        void releasePassResources();
        /**
         * @brief 파이프라인 XML 에 선언된 첨부의 포맷을 돌려줍니다 (없으면 fallback).
         * @details 첨부와 같은 크기·포맷이어야 하는 보조 텍스처(TAA 히스토리 등)를 만들 때 쓴다 —
         *          포맷을 상수로 박아 두면 파이프라인이 HDR 첨부를 쓰는 순간 어긋난다.
         */
        RHIFormat attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const;
        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 돌려줍니다.
         * @details 반환값이 곧 "Clear 로 열어도 되는가" 다. 같은 웨이브의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계구역이어야 한다 — 나눠 놓으면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지운다.
         */
        bool markAttachmentCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다 (프레임 시작). */
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
        /** @brief 일시 텍스처를 확보합니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /**
         * @brief TAA 히스토리 텍스처를 **셋업 단계에서** 만들어 둡니다.
         * @details 예전엔 TAA 패스 콜백 안에서 처음 만들고 bindless 에 등록했다. 그 콜백은 병렬
         *          기록에서 태스크 스레드가 돌리므로, 기록 중에 bindless 레지스트리가 resize 되는
         *          셈이었다 — 다른 스레드가 같은 레지스트리를 읽고 있는 와중에. 파이프라인이 TAA 를
         *          선언했는지, 대상 첨부의 포맷이 무엇인지는 셋업 시점에 이미 다 알 수 있다.
         */
        void ensureTaaHistory();
        /** @brief Present 캡처 텍스처를 한 번만 만듭니다 (켜져 있을 때만). */
        void ensurePresentCapture();
        /** @brief 일시 텍스처를 해제합니다. */
        void releaseTransientResources();
        /** @brief 그래프 패스 콜백을 한 번 바인딩합니다. */
        void bindPassCallbacks();
        /** @brief RenderGraph 패스 실행 콜백. */
        void onGraphPassExecute( const RenderGraphPassContext& ctx );

        /**
         * @brief 웨이브를 병렬 기록하기 **직전에** 그 웨이브가 만질 자원의 배리어를 미리 발행합니다.
         * @details 자원 이름을 실제 텍스처로 풀어 `prepareTextureForShaderRead` /
         *          `prepareTextureForRenderTarget` 을 프레임 스트림에 기록한다. 프레임 스트림은 이
         *          웨이브의 패스 리스트보다 먼저 제출되므로 GPU 타임라인에서도 앞선다.
         *
         *          이렇게 하면 패스 콜백은 이미 맞는 상태를 보게 되어 기록 중에 리소스 상태를 바꾸지
         *          않는다 — 배리어를 병렬 기록 스레드가 정하던 구조는 이 프로젝트에서 실제로 여러 번
         *          깨졌다(중복 배리어, 레이아웃 불일치).
         */
        void onGraphWavePrologue( const RenderGraphWaveContext& ctx );
        /** @brief 패스 타입에 맞는 실행을 수행합니다. */
        void executePass( FramePassContext& ctx, RenderPassType passType, string_view passName, const hashed_string& depthAttachment,
                          const RenderGraphPassDesc* pPassDesc );
        /**
         * @brief XML 이 선언한 입력을 **역할 이름으로** 전부 겁니다 — 풀스크린 패스 공통.
         * @details 역할은 로드 시점에 해석돼 있다(`_listResolvedInput`). 여기서 거는 것과 검증이 대조한 것이 같은 목록이라
         *          "선언은 했는데 안 걸리는 입력" 이 생길 자리가 없다. 쇼 플래그로 끈 역할은 건너뛴다.
         */
        void registerDeclaredInputs( FramePassContext& ctx, const RenderGraphPassDesc& passDesc );
        /** @brief 역할의 셰이더 이름(intern 된 hashed_string). */
        const hashed_string& inputRoleName( RenderPassInputRole role ) const;
        /** @brief 패스 상수 값(PassConstantValues)을 채웁니다. 업로드/바인딩은 ShaderBindingBinder 가 합니다. */
        void updatePassConstants( FramePassContext& ctx );

        /**
         * @brief 이번 프레임의 주광 값입니다. 패킷이 실어 주면 그 값, 아니면 기본값입니다.
         * @details 예전엔 방향·색·세기·그림자 볼륨이 전부 .cpp 안의 constexpr 이라 씬이 커져도
         *          그림자 볼륨이 2 유닛 그대로였다.
         */
        struct FrameLightState
        {
            float4   _dirIntensity{ -0.35f, -0.85f, -0.25f, 1.35f };
            float4   _colorAmbient{ 1.0f, 0.82f, 0.62f, 0.28f };
            float4x4 _shadowViewProj{};
            uint8    _bHasShadowViewProj{ SW_FALSE };
        };
        FrameLightState _frameLight;
        /** @brief 카메라에서 뷰/투영을 적용합니다. */
        void applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera );
        /**
         * @brief 뷰-투영과 **그 역행렬**을 함께 적용합니다.
         * @details 둘을 따로 채우면 언젠가 한쪽만 갱신된다 — 그러면 디퍼드가 복원한 월드 위치가
         *          지난 프레임의 카메라를 가리키고, 증상은 "빛이 한 프레임 늦게 따라온다" 다.
         */
        void applyViewProjection( FramePassContext& ctx, const float4x4& viewProj );
        /** @brief 키라이트 뷰-투영 행렬을 만듭니다. */
        void buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const;
        /** @brief 카메라 뷰-투영 행렬을 만듭니다. */
        void buildViewProj( float4x4& outMat ) const;
        /** @brief 월드 행렬을 항등으로 둡니다. */
        void setIdentityWorld( FramePassContext& ctx );
        /** @brief 드로우 직전 리플렉션 구동 바인딩을 수행합니다 (PassCB/MaterialCB/텍스처/인스턴스 버퍼). */
        void bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                          const RHIDescriptorIndex* pMaterialTexSrv = nullptr );
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr. */
        const ShaderBindingLayout* layoutForPso( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 핸들에 매핑합니다. */
        void registerPsoLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief GPUScene 인스턴스 구조버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& ctx );
        /**
         * @brief 씬 라이트 구조버퍼를 "SwLights" 로 등록합니다 — **모든 패스**에 건다.
         * @details 인스턴스 버퍼와 달리 지오메트리 패스 전용이 아니다. 디퍼드 조명은 풀스크린
         *          패스라 `registerInstanceBuffer` 를 타지 않는데, 라이트는 바로 거기서 필요하다.
         */
        void registerLightBuffer( FramePassContext& ctx );
        /** @brief 배치의 머티리얼 데이터 버퍼(GPUScene)를 패스 레지스트리에 "SwMaterials" 로 등록합니다. */
        void registerMaterialBuffer( FramePassContext& ctx, const GpuMeshBatch& batch, RHIPipelineStateHandle pso );
        /** @brief 씬 메시를 직접 그립니다. */
        void drawSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief GpuScene 배치를 간접 드로우로 그립니다. */
        void drawGpuBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex );
        /** @brief 일시 텍스처를 할당합니다. */
        void allocateTransient( string_view name, RHIFormat format, bool bDepth, const float4& clearColor );
        /** @brief 컬러(+깊이) 패스를 시작합니다. */
        void beginColorPass( FramePassContext& ctx, string_view colorName, string_view depthName, const float4& clearColor,
                             RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
        /** @brief MRT 컬러 패스를 시작합니다. */
        void beginColorPassMrt( FramePassContext& ctx, const string_view* pColorNames, const float4* pTargetClearColor,
                                const RHIRenderPassLoadOp* pColorLoad, uint32 colorCount, string_view depthName,
                                RHIRenderPassLoadOp depthLoad );
        /** @brief 깊이 전용 패스를 시작합니다. */
        void beginDepthOnlyPass( FramePassContext& ctx, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief 일시 텍스처를 리소스 레지스트리에 canonicalName 으로 등록합니다 (bindless SRV 자동 매칭).
         * @param canonicalName **미리 intern 된** 이름. 문자열을 받으면 패스마다 다시 intern 하게 된다 —
         *        attachmentNames() 의 캐시를 넘기세요.
         */
        void registerPassTexture( FramePassContext& ctx, const hashed_string& canonicalName, string_view attachmentName );
        /** @brief 바인들리스 텍스처 바인딩을 커밋합니다 (PassConstantValues 갱신 + 에뮬 백엔드 폴백 바인딩). */
        void commitBindlessTextureBindings( FramePassContext& ctx );

        // ------------------------------------------------------------------------------
        // 5) 커맨드 리스트 · 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() / executePacket() 공통: commandList를 준비하고 device에 연결합니다.
         * @param pCallerName 오류 로그 식별용 호출자 이름
         * @return 성공 시 true; false면 호출자가 즉시 반환해야 합니다.
         */
        bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

        /**
         * @brief execute() / executePacket() 공통: graph를 실행하고 commandList를 제출합니다.
         * @return graph.execute() 결과
         */
        bool submitGraph( IRHIDevice* pDevice );

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;
        /** @brief 어태치먼트 클리어 색을 반환하거나, 없으면 기본값을 반환합니다. */
        float4 getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const;
        /**
         * @brief 일시 텍스처와 그 SRV 를 한 번의 조회로 찾습니다. 없으면 빈 값.
         * @details 이름 하나로 둘 다 필요한 자리(registerPassTexture)가 패스마다 여러 번 돈다 —
         *          맵이 둘이던 시절엔 같은 문자열을 두 번 해시했다.
         */
        TransientAttachmentPool::Attachment findTransientAttachment( string_view name ) const;
        /** @brief 일시 텍스처 핸들을 찾습니다. 없으면 0. */
        RHITextureHandle findTransient( string_view name ) const;
        /** @brief Present 소스 어태치먼트 이름을 결정합니다. */
        string resolvePresentSource() const;
        /** @brief 패스 타입으로 파이프라인 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;

        /** @brief 엔진 기본 PSO를 만듭니다. */
        RHIPipelineStateHandle createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets = 1,
                                                const RHIFormat* pRtvFormats = nullptr, bool bBlend = false,
                                                bool bDepthWrite = true );
        /** @brief 파이프라인 XML 패스 레시피로 PSO를 만들고, 없으면 타입 기본값을 씁니다. */
        RHIPipelineStateHandle createPsoForPassType( RenderPassType passType, string_view defaultShader,
                                                     bool bDepthTest, uint32 numRenderTargets = 1,
                                                     const RHIFormat* pRtvFormats = nullptr, bool bDefaultBlend = false,
                                                     bool                  bDefaultDepthWrite = true,
                                                     const vector<string>* pExtraDefines      = nullptr );

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 **기록 시작 전에** 다 만들어 둡니다.
         * @details 기록 중에는 PSO 를 만들 수 없고(상수버퍼 용량을 미리 늘리는 것과 같은 이유다), 패스는
         *          병렬로 기록되므로 그때 만들면 백엔드마다 다른 방식으로 깨진다.
         */
        void ensureMaterialPsos();
        /**
         * @brief 패스 PSO 에 머티리얼 퍼뮤테이션을 얹은 변형을 만듭니다. 얹을 게 없으면 패스 PSO 를 그대로 돌려줍니다.
         * @details 렌더 상태(블렌드·뎁스·RT 포맷)는 **패스의 것을 그대로 물려받고** 셰이더만 갈아 끼운다 —
         *          그 둘은 패스가 정하는 것이지 머티리얼이 정하는 게 아니다.
         */
        RenderPsoCache::MaterialPsoEntry createMaterialPsoVariant( RHIPipelineStateHandle passPso, RenderPassType passType,
                                                                   const GpuShaderPermutation* pPermutation, RenderViewMode viewMode );
        /**
         * @brief 이번 프레임 배치 수에 맞춰 상수버퍼 슬롯을 **기록 시작 전에** 늘려 둡니다 (드로우마다 하나씩 나가므로).
         * @details 기록 중에는 버퍼 생성·bindless 등록을 할 수 없다. 지난 프레임의 최대 사용량(하이워터)도 바닥값으로 본다.
         */
        void ensurePassCbCapacityForFrame();
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다 (셋업 전용).
         * @details 기록 중에는 만들 수 없다 — 버퍼 생성과 `registerBindlessResource` 는 bindless 레지스트리를 바꾸고,
         *          패스 콜백은 태스크 워커에서 병렬로 돈다(`assertRegistryMutableNow` 가 감시하는 규칙).
         *          그래서 PSO 를 다 등록한 뒤 여기서 한 번에 만든다.
         */
        void ensureMaterialFallbackBuffers();
        /**
         * @brief Present 패스 PSO 를 **대상 포맷별로** 얻습니다 (없으면 만든다).
         * @details Present 의 대상은 둘이다 — 백버퍼(포맷은 디바이스가 실제로 채택한 값, Vulkan 은 서피스가
         *          B8G8R8A8 만 줄 수 있다)와 에디터 GameView RT(R8G8B8A8). PSO 의 렌더타깃 포맷이 대상과
         *          다르면 Vulkan 은 렌더패스 비호환으로 검증 레이어가 매 프레임 운다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같은 방식이다 —
         *          여기서는 Present 하나만 그 키가 포맷이라 맵 하나로 충분하다.
         */
        RHIPipelineStateHandle ensurePresentPso( RHIFormat targetFormat );
        /** @brief Present 가 그릴 수 있는 대상 포맷(백버퍼·오프스크린)의 PSO 를 셋업에서 미리 만듭니다. */
        void buildPresentPsoVariants();

    private:
        IRHIDevice*                   _pDevice;
        unique_ptr<RenderPassManager> _renderPassManager;
        IRHIDevice*                   _pCmdOwnerDevice;
        unique_ptr<IRHICommandList>   _frameCmd;
        IRHICommandList*              _pCmd;
        Scene*                        _pScene;
        TaskManager*                  _pTaskManager;
        GpuScene                      _gpuScene;
        /// @brief 씬 직접 경로(`execute( pScene )`, 에디터·테스트)가 쓰는 빌더 — 패킷 경로에서는 EngineLoop 의 것이 대신한다.
        GpuSceneBuilder        _sceneBuilder;
        RenderPipelineResource _pipelineResource;
        RenderGraph            _graph;
        string                 _pipelinePath;
        float4                 _clearColor;
        /// @brief 파이프라인이 선언한 첨부들 — 창 크기로 만들고 구성이 바뀔 때만 다시 만든다.
        TransientAttachmentPool _transientPool;
        /**
         * @brief 프레임 단위 패스 상태(직렬 경로에서 사용 + 병렬 패스의 시드).
         * @details 병렬 기록에서는 패스마다 이걸 복사해 각자의 커맨드 리스트/상수 버퍼를 붙입니다.
         */
        FramePassContext _frameCtx;
        /**
         * @brief 패스 슬롯별 컨텍스트 — 프레임마다 `_frameCtx` 를 **대입**해 쓴다(용량이 남아 힙을 만지지 않는다).
         * @details 예전에는 패스마다 지역 복사본을 만들었다 — 상수 값 목록·레지스트리 맵이 프레임마다 패스 수만큼 새로
         *          자랐다. 크기는 병렬 기록 **전**(`submitGraph`)에 맞춘다 — 기록 중에 늘리면 워커끼리 경합한다.
         *          마지막 칸은 이름을 못 찾은 패스의 몫이다.
         */
        vector<FramePassContext> _listPassContext;
        /// @brief 씬 직접 경로가 내보내는 스냅샷 — 바꿔치기로 저장소가 돌아온다.
        GpuSceneSnapshot _sceneSnapshotScratch;
        /// @brief `ensureMaterialPsos` 가 이번 프레임 배치에서 모으는 퍼뮤테이션 — 프레임마다 다시 채운다.
        vector<uint32> _listOpaquePermutationScratch;
        vector<uint32> _listTransparentPermutationScratch;
        /// @brief 배치 하나가 한 프레임에 몇 개의 지오메트리 패스에서 그려지는지 어림값 (그림자·프리패스·불투명·반투명).
        static constexpr uint32 _s_kDrawCbPassEstimate = 4;
        /// @brief 패스·드로우별 상수버퍼 슬롯 링 — 병렬 기록에서 드로우마다 하나씩 집어간다.
        PassConstantRing _passCbRing;
        /**
         * @brief 이번 프레임의 뷰들 — 행렬·절두체·상수버퍼를 각자 소유합니다.
         * @details 예전엔 이 셋이 `_cullMainViewProj` / `_cullShadowViewProj` / `_arrGpuCullCb` 로
         *          흩어져 있었고, "이 값은 어느 뷰 것인가" 를 사람이 기억해야 했다. 그래서 두 번 틀렸다 —
         *          한 번은 패스 상수버퍼를 드로우들이, 한 번은 컬링 상수버퍼를 뷰들이 나눠 썼다.
         *          이제 뷰를 얻으면 그 뷰의 것이 딸려 온다.
         */
        RenderView _arrView[static_cast<uint32>( RenderViewType::Count )];

        RHIConstantBufferSlot _instanceAnimCb;
        RHIConstantBufferSlot _meshMorphCb;
        /// @brief GPU 가 변형한 정점 풀 — RT 소유(GpuMeshMorphPool 참고).
        GpuMeshMorphPool _meshMorphPool;
        /// @brief 진단(`-gv_morphDiag=2|3`)에서 정점 셰이더에 결과 대신 **레스트** 버퍼를 물렸는가.
        uint8 _bMorphBindsRest;
        /// @brief `setMeshMorphDiag` 가 준 값. 음수면 전역 변수 `gv_morphDiag` 를 따른다.
        int32 _meshMorphDiagOverride;
        /// @brief `setDrawMergeEnabled` 가 준 값. 음수면 전역 변수 `gv_drawMerge` 를 따른다.
        int32 _drawMergeOverride;
        /// @brief `setVertexPoolEnabled` 가 준 값. 음수면 전역 변수 `gv_vertexPool` 을 따른다.
        int32 _vertexPoolOverride;
        /// @brief 이번 프레임의 씬 간접 드로우 호출 수 — 패스가 병렬로 기록하므로 원자.
        atomic<uint32> _indirectDrawCallCount;
        /** @brief 지난 프레임의 타임스탬프(마이크로초, 프레임 시작 기준 누적). */
        vector<float32> _listGpuTimestampMicro;
        /** @brief 패스 이름 -> `GPU.<패스>` 문자열. 프로파일러가 이름 포인터를 들고 있어 수명이 필요하다. */
        unordered_map<hashed_string, string> _mapGpuScopeName;
        /// @brief 마지막 프레임의 값 (getLastIndirectDrawCallCount).
        uint32 _lastIndirectDrawCallCount;
        /// @brief `setInputRoleEnabled( role, false )` 가 켠 비트 — 그 역할의 입력은 걸지 않는다.
        uint32 _disabledInputRoleMask;
        /// @brief 진단(`-gv_morphDiag=3`)이 올리는 번호표 정점. 스크래치 — 프레임 밖에서 의미 없다.
        vector<GpuMorphVertex> _listScratchMorphTag;
        /// @brief 이번 프레임 모프 대상 메시 — 프레임마다 할당하지 않으려고 들고 있는다.
        vector<Mesh*> _listScratchMorphMesh;
        /// @brief 씬 라이트 구조버퍼 — RT 소유. 포워드·디퍼드가 같은 버퍼를 읽는다.
        GpuLightBuffer _lightBuffer;
        /// @brief 씬 직접 경로에서 라이트를 모으는 버퍼 — 프레임마다 할당하지 않으려고 들고 있는다.
        vector<GpuLight>      _listScratchLight;
        RHIConstantBufferSlot _instanceSortCb;
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는가 (가시 목록이 유효한가).
         * @details 드로우가 가시 목록을 걸지 말지 정하는 값이다. 목록을 걸었는데 컬링이 안 돌면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 전부 같은 인스턴스를 그린다.
         */
        uint8 _bGpuCullingActive;

        /** @brief 뷰 하나를 얻습니다 — 그 뷰의 행렬·절두체·상수버퍼가 함께 옵니다. */
        RenderView&       view( RenderViewType type ) { return _arrView[static_cast<uint32>( type )]; }
        const RenderView& view( RenderViewType type ) const { return _arrView[static_cast<uint32>( type )]; }

        /** @brief 컴퓨트가 드로우 커맨드를 만드는 경로를 이번 프레임에 쓸 생각인지 (업로드 전에 GpuScene 에 알린다). */
        bool wantsGpuGeneratedCommands() const;

        /**
         * @brief 지난 프레임의 패스별 GPU 시간을 프로파일러에 `GPU.<패스>` 로 넣습니다.
         * @details GPU 타임스탬프가 없으면 GPU 비용을 `RT.BeginFrame`(백프레셔) 같은 대리값으로
         *          추측하거나 패스를 지워 가며 차이로 구해야 한다 — 그렇게 재다가 "당연히 이것이겠지"
         *          를 두 번 틀렸다(클리어·포맷). 상용 엔진이 전부 갖춘 이유가 그것이다.
         */
        void reportGpuPassTimes( IRHIDevice* pDevice );
        /** @brief 패스 이름으로 `GPU.<패스>` 스코프 이름을 만들어 캐시합니다(포인터 수명이 필요하다). */
        const utf8* gpuScopeNameFor( const string& passName );
        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다 (instanceanim.hlsl).
         * @details **컬링보다 먼저** 돌아야 한다 — 순서가 뒤집히면 컬링이 이번 프레임에 움직이기 전의
         *          바운드로 판정한다. 회전을 요청한 인스턴스가 없으면 통째로 건너뛴다.
         */
        void dispatchInstanceAnimation( uint32 instanceCount );
        /**
         * @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트 → 결과).
         * @details **컬링보다 앞**이어야 한다. 모프는 회전과 달리 실제로 모양과 바운드를 바꾸므로,
         *          뒤에 두면 컬링이 한 프레임 늦은 모양으로 판정한다.
         */
        void dispatchMeshMorph();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 적습니다 — **업로드 전에**.
         * @details 오프셋은 배치 표(g_SwBatches)에 실려 upload() 가 올린다. 예전엔 드로우 루트 상수라 업로드 뒤에 정해도 됐지만,
         *          표는 업로드 시점에 완성돼야 한다. 디스패치(dispatchMeshMorph)는 커맨드 리스트가 열린 뒤 따로 돈다.
         */
        void prepareMeshMorphPool();
        /** @brief 지금 적용되는 모프 진단 모드 — 오버라이드가 있으면 그것, 없으면 `gv_morphDiag`. */
        int32 getEffectiveMeshMorphDiag() const;
        /** @brief 씬 배치를 멀티 드로우로 묶을지 — `setDrawMergeEnabled` 가 준 값, 없으면 전역 `gv_drawMerge`. */
        bool isDrawMergeEnabled() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 깊이순으로 되돌립니다 (gpucull/instancesort.hlsl).
         * @details 컬링 결과는 절두체에 종속이라 뷰(메인/그림자)마다 자기 인자·목록을 따로 만든다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /**
         * @brief 인스턴스 애니메이션에 넣는 절대 시간(초).
         * @details 각도를 프레임마다 누적하지 않고 **이 절대 시간에서 매번 새로 만든다**. 누적하면 프레임
         *          간격의 흔들림이 그대로 쌓여 백엔드·실행마다 다른 각도가 나오고, 스크린샷 비교가 불가능해진다.
         *          렌더러가 자기 시계를 갖는다 — 델타를 여기까지 실어 나르지 않아도 되고, 렌더 스레드에서
         *          게임 시간을 만지지 않는다.
         */
        CpuTimer _animTimer;
        /**
         * @brief 머티리얼 없는 배치에 거는 0 채운 원소 하나짜리 구조버퍼 — **stride 마다 하나**.
         * @details DX12 루트 SRV 는 경계 검사가 없어 안 걸린 t9 를 읽으면 GPU 폴트(디바이스 제거)다 —
         *          어떤 드로우도 빈 슬롯으로 나가지 않게 항상 유효한 버퍼를 건다(언리얼의 기본 머티리얼 자리).
         *
         *          언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같다.
         *          예전엔 256 바이트 원소 하나를 모든 셰이더에 공용으로 걸었는데, 셰이더의 `SwMaterialData_t` 는
         *          24 바이트라 DX11 디버그 레이어가 드로우마다 "structure stride 256 vs 24" 를 냈다.
         *
         *          키는 stride 다. 셋업(ensureMaterialFallbackBuffers)에서만 만들고 기록 중에는 조회만 한다.
         */
        unordered_map<uint32, RHIStructuredBufferSlot> _mapMaterialFallback;
        /// @brief 엔진 패스 PSO · Present PSO · 머티리얼 변형과 그 바인딩 레이아웃 — 소유와 해제 순서는 캐시가 안다.
        RenderPsoCache _psoCache;
        /**
         * @brief 현재 보기 방식 (`RenderViewMode`).
         * @details 드로우 경로가 배치마다 읽고 UI 스레드가 쓴다. 값 하나뿐이라 atomic 으로 충분하다 —
         *          프레임 중간에 바뀌어도 최악은 한 프레임이 섞여 그려지는 것이고, PSO 변형은
         *          `ensureMaterialPsos` 가 그 프레임 시작에 읽은 모드로 이미 준비되어 있다.
         */
        atomic<uint8> _viewMode;
        /// @brief 셋업에 없는 Present 대상 포맷을 만났다고 한 번만 알리기 위한 래치.
        atomic<uint8> _bPresentPsoMissingLogged;
        /// @brief 머티리얼 폴백 stride 가 없다고 한 번만 알리기 위한 래치 (드로우 경로라 프레임마다 찍으면 안 된다).
        atomic<uint8>                        _bMaterialFallbackMissingLogged;
        unordered_map<hashed_string, uint32> _mapPassNameToIndex;
        RHITextureHandle                     _outputRenderTarget;
        RHITextureHandle                     _taaHistory;    ///< TAA resolve history (ping copy of last TaaColor)
        RHIDescriptorIndex                   _taaHistorySrv; ///< `_taaHistory` bindless SRV (프레임마다 재등록하지 않음)
        /**
         * @brief Present 결과를 받아 두는 텍스처 (0 = 안 받음). 스크린샷이 **최종 화면**을 보게 하는 길이다.
         * @details 스크린샷은 트랜지언트만 읽을 수 있고 백버퍼는 핸들이 없다. 그래서 예전에는 Present 가
         *          **읽는** 첨부를 찍었다 — 즉 톤맵은 한 번도 찍힌 적이 없었고, 후처리를 Present 로
         *          합치자 후처리 전체가 스크린샷에서 사라졌다. 받아 두면 둘 다 풀린다.
         */
        RHITextureHandle       _presentCapture;
        uint8                  _bPresentCaptureEnabled; ///< `-gv_screenshot` 실행에서만 켠다 (전체화면 복사 한 번이 더 붙는다).
        FrameRendererStatus    _status;
        string                 _statusMessage;
        uint8                  _bCallbacksBound     : 1;
        uint8                  _bPassResourcesReady : 1;
        [[maybe_unused]] uint8 _reservedFlags       : 5;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 웨이브끼리 병렬로 돈다
        // (RenderGraph::executeParallel). 비트필드로 두면 인접 비트를 쓰는 다른 패스와
        // 같은 바이트를 read-modify-write 해서 서로의 값을 날린다 — 독립 원자 변수로 뺀다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행됐는가 (ForwardOpaque 의 PSO 선택에 쓴다).
        atomic<uint8>               _bHasExecutedDepthPrepass;
        RenderGraphExecutionContext _graphContext;
    };
} // namespace sw
