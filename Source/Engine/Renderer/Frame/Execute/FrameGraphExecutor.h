/**
 * @file FrameGraphExecutor.h
 * @brief 프레임 패스(파이프라인 XML 의 `_listFramePass`)의 실행입니다 — 깊이 히스토리 · HZB · GPU 입자 · 인스턴스 애니메이션 · 메시 모프 · 메시 스킨 ·
 *        GPU 컬링(+ 인스턴스 정렬) · 라이트 그리드 · 볼륨 안개 · 실시간 프로브 · 화면 마커 투영.
 * @details 무엇이 어떤 순서로 도는지는 코드가 아니라 파이프라인 XML 이 정한다. 프레임 그래프(`FrameRenderState::_frameGraph`)가 패스의 읽기 · 쓰기 선언
 *          (`_listResource` 의 이름)으로 순서를 정하고, 패스마다 `executePass` 가 그 종류의 일을 기록한다. 뷰 그래프(`_listPass`)와 달리 프레임에 한 번
 *          돌고 모든 뷰의 몫(컬링 슬롯 · 라이트 그리드 · 깊이 히스토리)을 한꺼번에 기록한다 — 뷰 그래프보다 먼저 큐에 낸다(인다이렉트 인자 준비가 앞선다).
 *          언리얼 RDG 는 한 그래프에 뷰 패스와 컴퓨트 패스를 함께 넣지만, 이 엔진의 뷰 그래프는 뷰마다 다시 돌아 프레임 범위 일을 따로 둔다.
 *          패스 실행기(`FramePassExecutor`)가 소유한다. 렌더러는 업로드 전에 `usesGPUGeneratedCommands` · `prepareMeshMorphPool` · `makeVFXSimulationInput` 을 묻는다.
 *          뷰 그래프 쪽에서 부르는 일이 둘 있다 — 깊이 프리패스 뒤의 2 단계 가림 뒤 단계(`recordLateOcclusion`)와 화면 공간 반사 앞의 HZB 짓기(`recordSceneDepthHZB`).
 *          둘 다 이번 프레임 장면 깊이를 읽어 그 패스 안에서만 할 수 있고, 컬링 슬롯 · HZB 를 이 객체가 가져서 여기 둔다(언리얼 `FDeferredShadingSceneRenderer` 가
 *          프리패스 뒤 HZB · 가림을 렌더러 안에서 부르는 자리와 같다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/OcclusionCullingConfig.h"
#include "Engine/Graphics/RHI/RHIBufferTransitionBatch.h"
#include "Engine/Graphics/RHI/RHIConstantBuffer.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Renderer/Frame/Execute/HZBOcclusion.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"
#include "Engine/Renderer/VFX/VFXGPUSimulation.h"

namespace sw
{
    class FrameBufferTable;
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

        /** @brief 상주 자원(GPU assert 링 · 정렬 · 컬링 자리표 · 뒤 단계 컬링 상수)과 가림 판정 설정(`Config/Engine/OcclusionCulling.json`)을 준비합니다. assert 링을 만들지 못하거나 설정이 없거나 틀리면 멈춥니다. */
        void ensureResources();
        /** @brief 상주 자원과 가림 되읽기 · 우편함을 놓습니다. */
        void releaseResources();
        /**
         * @brief 프레임 그래프를 돌리기 전에 부릅니다 — 앞 프레임들의 셰이더 메시지 기록이 도착했으면 로그로 내고, 이번 프레임의 컬링 · HZB · 가림 타일 표시를 비우고,
         *        그래프가 전이를 낼 자원(지금은 HZB · 가림 타일 — 짓는 뷰만)을 @p table 에 등록합니다(뷰 칸: 주 시점 0, 추가 뷰 `_listExtraView` 번호 + 1).
         * @details 표시를 여기서 비우는 까닭: 파이프라인이 그 패스를 선언하지 않으면 패스가 돌지 않아 지난 파이프라인의 값이 남는다.
         *          짓지 않는 뷰는 등록하지 않는다 — 등록하면 그래프가 쓰지 않는 버퍼에 전이를 낸다(가림 판정은 기본 끔).
         */
        void beginFrame( FrameBufferTable& table );
        /**
         * @brief 프레임 패스 하나를 그래프 문맥 @p graphContext 의 리스트에 기록합니다(리스트는 부르는 쪽이 열고 닫는다). @p type 은 `isFramePassType` 이어야 합니다(로드 검증이 막는다).
         * @details 같은 종류를 두 번 선언할 수 없다(검증) — 컬링 · 그리드는 프레임에 한 번 모든 뷰를 돈다. 등록한 자원은 문맥의 접근 핸들로 받는다(`RenderGraphPassContext::getBuffer`).
         */
        void executePass( RenderPassKind type, const RenderGraphPassContext& graphContext );
        /** @brief 프레임 그래프가 끝난 뒤 부릅니다. assert 가 켜진 디스패치(컬링 · 모프 · 마커)의 링 되읽기를 @p cmd 에 요청합니다. */
        void endFrame( IRHICommandList& cmd );
        /** @brief 이번 프레임에 그린 뷰마다 그 뷰-투영과 눈 위치를 적어 둡니다(다음 프레임의 깊이 히스토리가 짝이 된다). 모든 뷰를 그린 뒤 부릅니다. */
        void rememberDepthHistoryViews();
        /**
         * @brief 2 단계 가림의 뒤 단계입니다 — 깊이 프리패스(@p view 가 주 시점일 때)가 끝난 직후 @p cmd 에 그 깊이로 HZB 를 다시 짓고, 앞 단계에서 가려졌던 차례만 다시 컬링해
         *        이제 보이는 것을 주 시점 간접 인자에 더합니다(gpu_cull.hlsl 머리말). 뒤 패스(G버퍼 · 포워드 불투명)가 더한 것까지 그린다.
         * @details 앞 단계가 이번 프레임에 차례를 적었을 때만 돈다(`_bLatePhasePending` — 설정 `_bTwoPhase`, 간접 멀티 드로우 능력, 지난 HZB 가 있어야 한다).
         *          깊이 프리패스가 없는 파이프라인은 뒤 단계가 없다 — 앞 단계에서 빠진 것은 한 단계 가림처럼 한 프레임 늦게 나온다.
         *          한계: 뒤 단계에서 더한 인스턴스는 깊이 프리패스에 없어 그 프레임의 DBuffer 데칼이 그 자리를 모른다(G버퍼가 깊이를 다시 쓴다) — 드러나는 첫 프레임 한 번이다.
         */
        void recordLateOcclusion( IRHICommandList& cmd, FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 이번 프레임 장면 깊이로 HZB 를 @p cmd 에 짓습니다(화면 공간 반사의 hi-z trace 앞 — 불투명을 다 그린 깊이). 지었으면 true 입니다.
         * @details 깊이 히스토리 텍스처에 장면 깊이를 옮긴 뒤 짓는다 — 그 텍스처는 프레임 처음 프레임 패스(입자 · HZB)가 다 읽었고 다음 프레임 처음에 다시 채운다.
         */
        [[nodiscard]] bool recordSceneDepthHZB( IRHICommandList& cmd, FrameViewTarget& view );
        /** @brief 2 단계 가림을 이번 프레임에 쓸 수 있는지입니다 — 가림 판정 켬 · 설정 `_bTwoPhase` · 간접 멀티 드로우 능력(WebGPU 는 없다 — 웹 깃발). */
        bool isTwoPhaseOcclusionEnabled() const;
        /** @brief 이번 프레임에 2 단계 가림의 뒤 단계 컬링을 기록했는지입니다(감시 값 `Render.OcclusionLatePhase`). */
        bool hasLatePhaseRun() const { return _bLatePhaseRan != 0; }
        /** @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다 — 드로우가 가시 목록을 바인딩할지 정한다(돌지 않았는데 묶으면 갱신되지 않은 목록을 읽는다). */
        bool isGPUCullingActive() const { return _bGPUCullingActive != 0; }
        /** @brief 순번 테이블이 없는 뷰의 정렬 · 마커 · 안개 디스패치가 t2 에 묶는 자리표의 SRV 입니다. */
        RHIDescriptorIndex getTransparentRankPlaceholderSRV() const { return _transparentRankPlaceholder._srv; }
        /** @brief 주 시점 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다. 어느 스레드에서 불러도 됩니다. */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _occlusion.getSnapshot(); }

        /** @brief 이번 프레임에 컴퓨트로 드로우 커맨드를 만들 예정인지 반환합니다. 업로드 전에 GPUScene 에 알리는 데 씁니다(컬링 PSO 는 파이프라인이 GPUCull 패스를 선언할 때만 있다). */
        bool usesGPUGeneratedCommands() const;
        /** @brief @p mode(`-gv_meshMorphDebugMode` 의 사본)가 모드 열거자인지 봅니다. 에디터 · 콘솔이 숫자로 넣은 범위 밖 값이면 오류를 남기고 false 입니다(`initialize` 가 멈춘다). */
        [[nodiscard]] static bool isMeshMorphDiagValid( MeshMorphDiagMode mode );
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
        void dispatchInstanceAnimation( const RenderGraphPassContext& graphContext );
        /** @brief 이번 프레임 인스턴스 애니메이션이 도는지입니다(회전 인스턴스 · PSO · 상수 버퍼 · UAV). `InstanceBuffer` 등록과 디스패치가 같은 판정을 본다. */
        bool shouldAnimateInstances() const;
        /** @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트에서 결과로). 컬링 앞이다(XML: `DeformVertices` 를 쓰고 컬링이 읽는다). 전이는 그래프가 낸다. */
        void dispatchMeshMorph( const RenderGraphPassContext& graphContext );
        /** @brief 스킨드 메시들의 정점을 GPU 가 본 팔레트로 블렌딩합니다(바인드 포즈에서 모프 풀의 스킨 구간으로). 디스패치 하나가 모든 스킨드 메시를 처리한다. */
        void dispatchMeshSkin( const RenderGraphPassContext& graphContext );
        /** @brief 지금 적용되는 모프 진단 모드입니다. 프레임 사본(`RenderFrameSettings::_meshMorphDebugMode`)입니다. */
        MeshMorphDiagMode getEffectiveMeshMorphDiag() const;
        /** @brief 파이프라인이 메시 변형 패스(MeshMorph · MeshSkin)를 하나라도 선언했는지 — 그 PSO 는 선언했을 때만 있다(`FramePSOManager::createPassPSOs`). */
        bool isDeformPassDeclared() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpu_cull/instance_sort.hlsl).
         * @details 컬링 결과는 절두체에 종속되므로 뷰(메인, 그림자)마다 자기 인자와 목록을 따로 만든다. 일감은 `collectCullJobs` 가 모았고 전이는 그래프가 낸다.
         */
        void dispatchCullAndSort( const RenderGraphPassContext& graphContext, uint32 instanceCount );
        /**
         * @brief 이번 프레임에 돌릴 컬링 슬롯(`_listCullJob`)과 2 단계 가림 차례 표시를 프레임 시작에 갖춥니다(`beginFrame`). 컬링을 돌릴 수 없으면 목록이 빈다.
         * @details 표 등록(`registerCullResources`)과 디스패치가 같은 목록을 본다. 준비되지 않은 추가 뷰는 여기서 쉬게 한다(`_bRenderThisFrame`).
         */
        void collectCullJobs( uint32 instanceCount );
        /** @brief 모은 컬링 슬롯의 버퍼를 표에 등록합니다 — `CullArgs` · `CullVisibleInstances` · `CullLODState`(칸 = 컬링 슬롯), `OccludedOrders`(칸 0). */
        void registerCullResources( FrameBufferTable& table ) const;
        /** @brief 컬링 슬롯 @p cullViewIndex 를 @p renderView 로 돌릴 수 있으면 true 입니다(슬롯 · 인자 · 가시 목록 UAV · 컬링 상수 버퍼가 있다). */
        bool isCullViewReady( uint32 cullViewIndex, const RenderView& renderView ) const;
        /**
         * @brief 돌 수 있는 컬링 슬롯이면 일감 목록(`_listCullJob`)에 넣고 true 입니다. 전이는 그래프가, 디스패치는 `dispatchCullAndSort` 가 목록을 따라 낸다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool enqueueCullView( uint32 cullViewIndex, const RenderView& renderView, const FrameViewTarget* pExtraView );
        /**
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 버퍼는 이미 UAV 상태다 — 앞 · 뒤 전이는 그래프가 모든 슬롯을 묶어 낸다.
         *        되돌리기 → 컬링 → 정렬 사이는 선언한 UAV 핸들의 `uavBarrier` 다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        void dispatchCullView( const RenderGraphPassContext& graphContext, uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount,
                               const FrameViewTarget* pExtraView );
        /** @brief 이번 프레임의 LOD 전환 진행량(`_lodFadeStep`)을 렌더 프레임 간격에서 정합니다. 컬링 디스패치 앞에서 한 번 부른다. */
        void computeLODFadeStep();
        /** @brief GPU 생산자로 뷰마다의 라이트 그리드를 채웁니다(light_grid.hlsl, 그룹 하나 = 뷰 하나). 생산자가 GPU 가 아니면 아무것도 하지 않습니다. */
        void dispatchLightGrid( const RenderGraphPassContext& graphContext );
        /** @brief 실시간 프로브 갱신 컴퓨트를 기록합니다. `prepareRealtimeProbes` 가 켠 프레임에만 돕니다. */
        void dispatchRealtimeProbes();
        /** @brief 뷰 @p view 의 지난 깊이를 이번 프레임에 히스토리로 옮길 수 있으면 true 입니다(지난 깊이 · 히스토리 · PSO 가 모두 있다). */
        bool canCopyDepthHistory( const FrameViewTarget& view ) const;
        /** @brief 뷰 @p view 의 장면 깊이 첨부를 깊이 히스토리 텍스처에 옮길 수 있으면 true 입니다(히스토리 · 복사 PSO · 장면 깊이가 있다 — 지난 깊이인지는 묻지 않는다). */
        bool canCopySceneDepth( const FrameViewTarget& view ) const;
        /** @brief 뷰마다 지난 장면 깊이를 깊이 히스토리로 옮기는 풀스크린을 기록합니다(depth_history.hlsl). 프레임 패스 `DepthHistory` 의 일입니다. */
        void recordDepthHistories();
        /**
         * @brief 뷰 @p target 하나의 깊이 히스토리 복사를 기록합니다(`recordDepthHistories` 가 뷰마다 부른다).
         * @details 끝에 히스토리를 RW 텍스처 상태로 둔다 — 읽는 쪽(가림 타일 · GPU 입자)은 둘 다 RW 텍스처로 읽고, 비동기 컴퓨트 큐에서 돌면 그래픽스 상태(렌더 타깃)에서
         *          옮기는 배리어를 기록할 수 없다(D3D12 컴퓨트 리스트는 렌더 타깃 상태를 모른다). 생산자가 소비자의 상태로 넘겨 주는 자리다.
         */
        void recordDepthHistory( FrameViewTarget& target );
        /** @brief 뷰 @p target 의 장면 깊이 첨부를 깊이 히스토리 텍스처에 옮기는 풀스크린을 기록합니다(depth_history.hlsl). 끝에 히스토리를 RW 텍스처 상태로 둔다. */
        void copySceneDepth( FrameViewTarget& target );
        /**
         * @brief 뷰마다 깊이 히스토리(지난 장면 깊이)에서 HZB 를 짓고 주 시점 가림 타일 되읽기를 요청합니다(hzb_build.hlsl). 프레임 패스 `HZBBuild` 의 일입니다.
         * @details 가림 판정을 켠 프레임에 컬링이 있는 백엔드에서만 짓는다(화면 공간 반사는 이번 프레임 깊이로 따로 짓는다 — `recordSceneDepthHZB`). 기록을 버리는 프레임(처음 · 컷)은
         *          지난 깊이가 다른 장면이라 짓지 않는다. 쉬는 추가 뷰는 컬링하지 않으므로 짓지 않는다.
         */
        void recordFrameHZBs( const RenderGraphPassContext& graphContext );
        /** @brief 프레임 패스 `HZBBuild` 가 이번 프레임에 뷰 @p view 의 피라미드를 짓는지입니다(가림 판정 켬 · 컬링 능력 · 기록을 버리지 않는 프레임 · 깊이 히스토리 복사). 등록과 짓기가 같은 판정을 본다. */
        bool shouldBuildFrameHZB( const FrameViewTarget& view ) const;
        /** @brief 이번 프레임 장면 깊이로 뷰 @p view 의 HZB 를 짓습니다(장면 깊이 → 깊이 히스토리 → 피라미드). `_pCmd` 가 지금 리스트여야 한다. */
        bool recordCurrentDepthHZB( FrameViewTarget& view );
        /** @brief 뷰 @p view 를 그리는 카메라의 뷰-투영입니다(주 시점은 주 카메라, 추가 뷰는 자기 컬링 입력 — `rememberDepthHistoryViews` 와 같은 출처). */
        const float4x4& findViewProj( const FrameViewTarget& view ) const;
        /**
         * @brief 컬링 슬롯 @p cullViewIndex 의 컬링 상수를 채웁니다(절두체 · 개수 · LOD · 가림). 앞 단계(`dispatchCullView`)와 뒤 단계(`recordLateOcclusion`)가 같은 식을 쓴다.
         * @param bResetLODState 전환 상태를 비울지입니다(뒤 단계는 앞 단계가 정한 값을 그대로 받는다 — 비우기 신호를 두 번 꺼내지 않는다).
         */
        void fillCullParams( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pExtraView, bool bResetLODState,
                             FrameRendererUtil::GPUCullParams& outParams ) const;

    private:
        IRHIDevice* const& _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        FrameRenderState&  _state;
        FramePSOManager&   _psoManager;
        FrameFog&          _fog;
        IRHICommandList*   _pCmd{ nullptr }; ///< `executePass` · `endFrame` 하는 동안만 지금 리스트를 가리킨다
        /// @brief 순번 테이블이 없는 뷰(주 시점, 그림자)의 정렬 디스패치가 t2 에 바인딩하는 원소 하나짜리 자리 표시 버퍼입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않습니다.
        RHIStructuredBuffer _transparentRankPlaceholder{};
        /// @brief GPU 컬링의 가림 판정(HZB 짓기 · 가림 타일 되읽기 · 자리표)입니다.
        HZBOcclusion _occlusion;
        /// @brief 가림 판정 설정(`Config/Engine/OcclusionCulling.json`)입니다. `ensureResources` 가 읽고 검사한다.
        OcclusionCullingConfig _occlusionConfig{};
        /// @brief 2 단계 가림 뒤 단계의 컬링 상수입니다 — 주 시점 슬롯의 상수 버퍼는 앞 단계가 이미 이번 프레임에 썼다(한 버퍼는 프레임에 한 번만 쓴다).
        RHIConstantBuffer _lateCullCb{};
        /// @brief 2 단계 가림의 차례마다 표시(uint — 앞 단계에서 가려져 빠졌으면 1, gpu_cull.hlsl u3)입니다. 주 시점 하나만 쓴다.
        RHIStructuredBuffer _occludedOrders{};
        /// @brief 앞 단계가 이번 프레임에 차례를 적어 뒤 단계가 남았다(`recordLateOcclusion` 이 내린다).
        uint8 _bLatePhasePending{ 0 };
        /// @brief 이번 프레임에 뒤 단계 컬링을 기록했다(`hasLatePhaseRun`).
        uint8 _bLatePhaseRan{ 0 };
        /// @brief 앞 단계가 쓴 전환 상태 비우기 신호입니다(뒤 단계가 같은 값을 쓴다).
        uint8 _bLateLODReset{ 0 };
        /// @brief 앞 단계의 차례 수입니다(뒤 단계도 같은 수의 스레드를 돈다).
        uint32 _lateInstanceCount{ 0 };
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
        /// @brief GPU 메시 입자 되읽기의 이번 프레임 출처입니다(`endFrame`). 프레임마다 다시 쓰고 용량은 지닌다.
        vector<VFXGPUSimulation::MeshProbeSource> _listScratchMeshProbeSource;
        /// @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다(`isGPUCullingActive`).
        uint8 _bGPUCullingActive{ 0 };
        /// @brief 이번 프레임 컬링 일감이 그려야 할 슬롯을 모두 덮는지입니다(`collectCullJobs` — 빠진 슬롯이 있으면 CPU 개수로 그린다).
        uint8 _bCullJobsCoverAllViews{ 0 };
        /// @brief 이번 프레임 2 단계 가림 차례 표시 버퍼를 갖췄는지입니다(`collectCullJobs` — 표 등록이 본다).
        uint8 _bOccludedOrdersReady{ 0 };
    };
} // namespace sw
