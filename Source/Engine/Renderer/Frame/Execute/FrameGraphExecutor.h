/**
 * @file FrameGraphExecutor.h
 * @brief 프레임 패스(파이프라인 XML 의 `_listFramePass`)의 실행입니다 — 깊이 히스토리 · 가림 타일 · GPU 입자 · 인스턴스 애니메이션 · 메시 모프 · 메시 스킨 ·
 *        GPU 컬링(+ 인스턴스 정렬) · 라이트 그리드 · 볼륨 안개 · 실시간 프로브 · 화면 마커 투영.
 * @details 무엇이 어떤 순서로 도는지는 코드가 아니라 파이프라인 XML 이 정한다. 프레임 그래프(`FrameRenderState::_frameGraph`)가 패스의 읽기 · 쓰기 선언
 *          (`_listResource` 의 이름)으로 순서를 정하고, 패스마다 `executePass` 가 그 종류의 일을 기록한다. 뷰 그래프(`_listPass`)와 달리 프레임에 한 번
 *          돌고 모든 뷰의 몫(컬링 슬롯 · 라이트 그리드 · 깊이 히스토리)을 한꺼번에 기록한다 — 뷰 그래프보다 먼저 큐에 낸다(인다이렉트 인자 준비가 앞선다).
 *          언리얼 RDG 는 한 그래프에 뷰 패스와 컴퓨트 패스를 함께 넣지만, 이 엔진의 뷰 그래프는 뷰마다 다시 돌아 프레임 범위 일을 따로 둔다.
 *          패스 실행기(`FramePassExecutor`)가 소유한다. 렌더러는 업로드 전에 `usesGPUGeneratedCommands` · `prepareMeshMorphPool` · `makeVFXSimulationInput` 을 묻는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIBufferTransitionBatch.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Renderer/Frame/Execute/OcclusionTiles.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"
#include "Engine/Renderer/VFX/VFXGPUSimulation.h"

namespace sw
{
    class FrameFog;
    class FramePSOManager;
    class IRHICommandList;
    class IRHIDevice;
    struct FrameRenderState;
    struct FrameViewTarget;
    struct RenderView;

    /**
     * @class FrameGraphExecutor
     * @brief 프레임 패스 종류(`isFramePassType`)마다의 실행입니다. 파일 머리말 참고. 렌더 스레드에서만 쓴다.
     */
    class FrameGraphExecutor
    {
    public:
        FrameGraphExecutor( IRHIDevice* const& pDevice, FrameRenderState& state, FramePSOManager& psoManager, FrameFog& fog );
        FrameGraphExecutor( const FrameGraphExecutor& )            = delete;
        FrameGraphExecutor& operator=( const FrameGraphExecutor& ) = delete;

        /** @brief 상주 자원(GPU assert 링 · 정렬 · 컬링 자리표)을 만듭니다. assert 링을 만들지 못하면 멈춥니다. */
        void ensureResources();
        /** @brief 상주 자원과 가림 되읽기 · 우편함을 놓습니다. */
        void releaseResources();
        /**
         * @brief 프레임 그래프를 돌리기 전에 부릅니다 — 앞 프레임들의 셰이더 메시지 기록이 도착했으면 로그로 내고, 이번 프레임의 컬링 · 가림 타일 표시를 비웁니다.
         * @details 표시를 여기서 비우는 까닭: 파이프라인이 그 패스를 선언하지 않으면 패스가 돌지 않아 지난 파이프라인의 값이 남는다.
         */
        void beginFrame();
        /**
         * @brief 프레임 패스 하나를 @p cmd 에 기록합니다(리스트는 부르는 쪽이 열고 닫는다). @p type 은 `isFramePassType` 이어야 합니다(로드 검증이 막는다).
         * @details 같은 종류를 두 번 선언할 수 없다(검증) — 컬링 · 그리드는 프레임에 한 번 모든 뷰를 돈다.
         */
        void executePass( RenderPassType type, IRHICommandList& cmd );
        /** @brief 프레임 그래프가 끝난 뒤 부릅니다. assert 가 켜진 디스패치(컬링 · 모프 · 마커)의 링 되읽기를 @p cmd 에 요청합니다. */
        void endFrame( IRHICommandList& cmd );
        /** @brief 이번 프레임에 그린 뷰마다 그 뷰-투영과 눈 위치를 적어 둡니다(다음 프레임의 깊이 히스토리가 짝이 된다). 모든 뷰를 그린 뒤 부릅니다. */
        void rememberDepthHistoryViews();
        /** @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다 — 드로우가 가시 목록을 바인딩할지 정한다(돌지 않았는데 묶으면 갱신되지 않은 목록을 읽는다). */
        bool isGPUCullingActive() const { return _bGPUCullingActive != 0; }
        /** @brief 순번 테이블이 없는 뷰의 정렬 · 마커 · 안개 디스패치가 t2 에 묶는 자리표의 SRV 입니다. */
        RHIDescriptorIndex getTransparentRankPlaceholderSRV() const { return _transparentRankPlaceholder._srv; }
        /** @brief 주 시점 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다. 어느 스레드에서 불러도 됩니다. */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _occlusion.getSnapshot(); }

        /** @brief 이번 프레임에 컴퓨트로 드로우 커맨드를 만들 예정인지 반환합니다. 업로드 전에 GPUScene 에 알리는 데 씁니다(컬링 PSO 는 파이프라인이 GPUCull 패스를 선언할 때만 있다). */
        bool usesGPUGeneratedCommands() const;
        /** @brief `gv_meshMorphDebugMode` 가 모드 열거자인지 봅니다. 에디터 · 콘솔이 숫자로 넣은 범위 밖 값이면 오류를 남기고 false 입니다(`initialize` 가 멈춘다). */
        [[nodiscard]] static bool isMeshMorphDiagValid();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 기록합니다. **업로드 전에** 부릅니다.
         * @details 오프셋은 배치 테이블(g_SwBatches)에 실려 upload() 가 올리므로 테이블은 업로드 시점에 완성되어 있어야 한다. 디스패치는 프레임 그래프의 MeshMorph · MeshSkin 패스다.
         *          파이프라인이 두 패스 중 하나도 선언하지 않으면 백엔드가 못 하는 때와 같이 풀을 만들지 않는다(결과 버퍼를 채울 쪽이 없다).
         */
        void prepareMeshMorphPool();
        /**
         * @brief 이번 프레임 스킨 인스턴스(칸 · 메시)를 팔레트에서 모읍니다 — 인스턴스가 그리는 칸만(숨은 유닛은 스키닝하지 않는다). 순서는 팔레트 순서 그대로다.
         * @details 스킨 결과는 메시가 아니라 칸마다라 배치의 메시로는 모을 수 없다(같은 메시를 캐릭터 여럿이 그린다). 팔레트가 메시 소유권을 싣는다.
         */
        void collectSkinInstances();
        /** @brief GPU 입자 시뮬레이션의 이번 프레임 입력(PSO · 주 시점 깊이 히스토리 · 인스턴스 버퍼)입니다. GPUScene 을 올린 뒤 부릅니다. */
        VFXSimulationFrameInput makeVFXSimulationInput() const;

    private:
        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다(instance_animation.hlsl).
         * @details 컬링보다 먼저여야 한다 — XML 에서 둘 다 `InstanceBuffer` 를 쓰고(애니메이션) 읽어(컬링) 그래프가 순서를 정한다. 회전을 요청한 인스턴스가 없으면 통째로 건너뛴다.
         */
        void dispatchInstanceAnimation( uint32 instanceCount );
        /** @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트에서 결과로). 컬링 앞이다(XML: `DeformVertices` 를 쓰고 컬링이 읽는다). */
        void dispatchMeshMorph();
        /** @brief 스킨드 메시들의 정점을 GPU 가 본 팔레트로 블렌딩합니다(바인드 포즈에서 모프 풀의 스킨 구간으로). 디스패치 하나가 모든 스킨드 메시를 처리한다. */
        void dispatchMeshSkin();
        /** @brief 지금 적용되는 모프 진단 모드입니다. 오버라이드가 있으면 그 값을, 없으면 `gv_meshMorphDebugMode` 를 씁니다. */
        MeshMorphDiagMode getEffectiveMeshMorphDiag() const;
        /** @brief 파이프라인이 메시 변형 패스(MeshMorph · MeshSkin)를 하나라도 선언했는지 — 그 PSO 는 선언했을 때만 있다(`FramePSOManager::createPassPSOs`). */
        bool isDeformPassDeclared() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpu_cull/instance_sort.hlsl).
         * @details 컬링 결과는 절두체에 종속되므로 뷰(메인, 그림자)마다 자기 인자와 목록을 따로 만든다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /** @brief 컬링 슬롯 @p cullViewIndex 를 @p renderView 로 돌릴 수 있으면 true 입니다(슬롯 · 인자 · 가시 목록 UAV · 컬링 상수 버퍼가 있다). */
        bool isCullViewReady( uint32 cullViewIndex, const RenderView& renderView ) const;
        /**
         * @brief 돌 수 있는 컬링 슬롯이면 일감 목록(`_listCullJob`)에 넣고 true 입니다. 디스패치 · 배리어는 `dispatchCullAndSort` 가 목록을 모은 뒤 한꺼번에 낸다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool enqueueCullView( uint32 cullViewIndex, const RenderView& renderView, const FrameViewTarget* pExtraView );
        /**
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 버퍼는 이미 UAV 상태다 — 앞 · 뒤 전이는 부르는 쪽이 모든 슬롯을 묶어 낸다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        void dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pExtraView );
        /** @brief 이번 프레임의 LOD 전환 진행량(`_lodFadeStep`)을 렌더 프레임 간격에서 정합니다. 컬링 디스패치 앞에서 한 번 부른다. */
        void computeLODFadeStep();
        /** @brief GPU 생산자로 뷰마다의 라이트 그리드를 채웁니다(light_grid.hlsl, 그룹 하나 = 뷰 하나). 생산자가 GPU 가 아니면 아무것도 하지 않습니다. */
        void dispatchLightGrid();
        /** @brief 실시간 프로브 갱신 컴퓨트를 기록합니다. `prepareRealtimeProbes` 가 켠 프레임에만 돕니다. */
        void dispatchRealtimeProbes();
        /** @brief 뷰 @p view 의 지난 깊이를 이번 프레임에 히스토리로 옮길 수 있으면 true 입니다(지난 깊이 · 히스토리 · PSO 가 모두 있다). */
        bool canCopyDepthHistory( const FrameViewTarget& view ) const;
        /** @brief 뷰마다 지난 장면 깊이를 깊이 히스토리로 옮기는 풀스크린을 기록합니다(depth_history.hlsl). 프레임 패스 `DepthHistory` 의 일입니다. */
        void recordDepthHistories();
        /**
         * @brief 뷰 @p target 하나의 깊이 히스토리 복사를 기록합니다(`recordDepthHistories` 가 뷰마다 부른다).
         * @details 끝에 히스토리를 RW 텍스처 상태로 둔다 — 읽는 쪽(가림 타일 · GPU 입자)은 둘 다 RW 텍스처로 읽고, 비동기 컴퓨트 큐에서 돌면 그래픽스 상태(렌더 타깃)에서
         *          옮기는 배리어를 기록할 수 없다(D3D12 컴퓨트 리스트는 렌더 타깃 상태를 모른다). 생산자가 소비자의 상태로 넘겨 주는 자리다.
         */
        void recordDepthHistory( FrameViewTarget& target );
        /** @brief 뷰마다 깊이 히스토리에서 가림 타일을 짓고 주 시점 타일 되읽기를 요청합니다(occlusion_tiles.hlsl). 프레임 패스 `OcclusionTiles` 의 일입니다. */
        void recordOcclusionTiles();

    private:
        IRHIDevice* const& _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        FrameRenderState&  _state;
        FramePSOManager&   _psoManager;
        FrameFog&          _fog;
        IRHICommandList*   _pCmd{ nullptr }; ///< `executePass` · `endFrame` 하는 동안만 지금 리스트를 가리킨다
        /// @brief 순번 테이블이 없는 뷰(주 시점, 그림자)의 정렬 디스패치가 t2 에 바인딩하는 원소 하나짜리 자리 표시 버퍼입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않습니다.
        RHIStructuredBuffer _transparentRankPlaceholder{};
        /// @brief GPU 컬링의 가림 판정(가림 타일 짓기 · 되읽기 · 자리표)입니다.
        OcclusionTiles _occlusion;
        /// @brief 이번 프레임의 LOD 전환 진행량(0 ~ 255)입니다. 0 이면 크로스페이드 없이 바로 바꾼다(`dispatchCullAndSort` 가 정하고 `dispatchCullView` 가 컬링 상수로 싣는다).
        uint32 _lodFadeStep{ 0 };
        /// @brief 지난 컬링 프레임의 시각(나노초)입니다. 전환 진행량의 프레임 간격을 잰다(0 이면 아직 없다).
        int64 _lodFadePreviousNanoseconds{ 0 };
        /** @brief 이번 프레임에 돌릴 컬링 슬롯 하나입니다(`dispatchCullAndSort` 가 모으고 한꺼번에 전이 · 디스패치한다). */
        struct CullJob
        {
            uint32                 _cullViewIndex{ 0 };
            const RenderView*      _pRenderView{ nullptr };
            const FrameViewTarget* _pExtraView{ nullptr };
        };
        /// @brief 이번 프레임의 컬링 일감입니다. 프레임마다 다시 쓰고 용량은 지닌다.
        vector<CullJob> _listCullJob;
        /// @brief 컬링 슬롯 모두의 앞 · 뒤 버퍼 전이를 한 번씩 내는 묶음입니다(슬롯마다 넷을 따로 내지 않는다).
        RHIBufferTransitionBatch _bufferTransitions;
        /// @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다(`isGPUCullingActive`).
        uint8 _bGPUCullingActive{ 0 };
    };
} // namespace sw
