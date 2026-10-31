/**
 * @file FramePrepass.h
 * @brief 그래프 앞의 프레임 프리패스입니다 — 프레임 커맨드 리스트에 직접 기록하는 컴퓨트(인스턴스 애니메이션 · 메시 모프 · 메시 스킨 · GPU 컬링 · 인스턴스 정렬 ·
 *        라이트 그리드 · 실시간 프로브), 뷰마다의 깊이 히스토리와 가림 타일, GPU 입자 시뮬레이션, 볼륨 안개, 화면 마커 투영.
 * @details 모두 렌더 그래프의 패스가 아니다. 그리기 **전에** 한 리스트에 순서대로 기록되고 그 리스트를 먼저 큐에 낸다 — 인다이렉트 인자 준비가 GPU 타임라인에서
 *          뷰마다의 리스트보다 먼저 끝난다. 순서: 애니메이션 → 모프 → 스킨 → 컬링(그래야 컬링이 이번 프레임 바운드로 판정한다) → 라이트 그리드 → 안개 → 프로브.
 *          패스 실행기(`FramePassExecutor`)가 소유하고 `record` 로 부른다. 렌더러는 업로드 전에 `usesGPUGeneratedCommands` · `prepareMeshMorphPool` · `makeVFXSimulationInput` 을 묻는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Debug/GPUAssert.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Renderer/Frame/Execute/OcclusionTiles.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
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
     * @class FramePrepass
     * @brief 그래프 앞의 프레임 프리패스입니다. 파일 머리말 참고. 렌더 스레드에서만 쓴다.
     */
    class FramePrepass
    {
    public:
        FramePrepass( IRHIDevice* const& pDevice, FrameRenderState& state, FramePSOManager& psoManager, FrameFog& fog );
        FramePrepass( const FramePrepass& )            = delete;
        FramePrepass& operator=( const FramePrepass& ) = delete;

        /** @brief 상주 자원(GPU assert 링 · 정렬 · 컬링 자리표)을 만듭니다. assert 링을 만들지 못하면 멈춥니다. */
        void ensureResources();
        /** @brief 상주 자원과 가림 되읽기 · 우편함을 놓습니다. */
        void releaseResources();
        /**
         * @brief 프리패스를 @p cmd 에 순서대로 기록합니다(리스트는 부르는 쪽이 열고 닫는다). 앞 프레임들의 셰이더 assert 기록이 도착했으면 로그로 냅니다.
         * @details 기록이 끝나면 assert 링 되읽기를 요청한다. 월드 UI 렌더 텍스처 · 2D 빛 쿠키 아틀라스는 그리기 경로라 실행기가 이 뒤에 같은 리스트에 기록한다.
         */
        void record( IRHIDevice& device, IRHICommandList& cmd );
        /** @brief 이번 프레임에 그린 뷰마다 그 뷰-투영과 눈 위치를 적어 둡니다(다음 프레임의 깊이 히스토리가 짝이 된다). 모든 뷰를 그린 뒤 부릅니다. */
        void rememberDepthHistoryViews();
        /** @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다 — 드로우가 가시 목록을 바인딩할지 정한다(돌지 않았는데 묶으면 갱신되지 않은 목록을 읽는다). */
        bool isGPUCullingActive() const { return _bGPUCullingActive != 0; }
        /** @brief 순번 테이블이 없는 뷰의 정렬 · 마커 · 안개 디스패치가 t2 에 묶는 자리표의 SRV 입니다. */
        RHIDescriptorIndex getTransparentRankPlaceholderSRV() const { return _transparentRankPlaceholder._srv; }
        /** @brief 주 시점 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다. 어느 스레드에서 불러도 됩니다. */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _occlusion.getSnapshot(); }

        /** @brief 이번 프레임에 컴퓨트로 드로우 커맨드를 만들 예정인지 반환합니다. 업로드 전에 GPUScene 에 알리는 데 씁니다. */
        bool usesGPUGeneratedCommands() const;
        /** @brief `gv_meshMorphDebugMode` 가 모드 열거자인지 봅니다. 에디터 · 콘솔이 숫자로 넣은 범위 밖 값이면 오류를 남기고 false 입니다(`initialize` 가 멈춘다). */
        [[nodiscard]] static bool isMeshMorphDiagValid();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 기록합니다. **업로드 전에** 부릅니다.
         * @details 오프셋은 배치 테이블(g_SwBatches)에 실려 upload() 가 올리므로 테이블은 업로드 시점에 완성되어 있어야 한다. 디스패치(dispatchMeshMorph)는 커맨드 리스트가 열린 뒤 따로 돈다.
         */
        void prepareMeshMorphPool();
        /** @brief GPU 입자 시뮬레이션의 이번 프레임 입력(PSO · 주 시점 깊이 히스토리 · 인스턴스 버퍼)입니다. GPUScene 을 올린 뒤 부릅니다. */
        VFXSimulationFrameInput makeVFXSimulationInput() const;

    private:
        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다(instance_animation.hlsl).
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
        /** @brief 지금 적용되는 모프 진단 모드입니다. 오버라이드가 있으면 그 값을, 없으면 `gv_meshMorphDebugMode` 를 씁니다. */
        MeshMorphDiagMode getEffectiveMeshMorphDiag() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpu_cull/instance_sort.hlsl).
         * @details 컬링 결과는 절두체에 종속되므로 뷰(메인, 그림자)마다 자기 인자와 목록을 따로 만든다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /**
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 디스패치를 돌렸으면 true 입니다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pExtraView );
        /** @brief 이번 프레임의 LOD 전환 진행량(`_lodFadeStep`)을 렌더 프레임 간격에서 정합니다. 컬링 디스패치 앞에서 한 번 부른다. */
        void computeLODFadeStep();
        /**
         * @brief GPU 생산자로 뷰마다의 라이트 그리드를 채웁니다(light_grid.hlsl, 그룹 하나 = 뷰 하나). 생산자가 GPU 가 아니면 아무것도 하지 않습니다.
         * @details 그리기 전에, 그래프 밖의 컴퓨트 프리패스로 기록한다(렌더 그래프에 컴퓨트 패스 상태가 없다). 끝나면 그리드 버퍼를 SRV 로 돌린다.
         */
        void dispatchLightGrid();
        /** @brief 실시간 프로브 갱신 컴퓨트를 기록합니다(라이트 그리드 · 볼륨 안개 뒤, 조명 패스 앞). `prepareRealtimeProbes` 가 켠 프레임에만 돕니다. */
        void dispatchRealtimeProbes();
        /** @brief 뷰 @p view 의 지난 깊이를 이번 프레임에 히스토리로 옮길 수 있으면 true 입니다(지난 깊이 · 히스토리 · PSO 가 모두 있다). */
        bool canCopyDepthHistory( const FrameViewTarget& view ) const;
        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰마다 지난 장면 깊이를 깊이 히스토리로 옮기는 풀스크린 하나를 기록합니다(depth_history.hlsl).
         * @details GPU 입자 시뮬레이션보다 먼저 기록한다 — 깊이 충돌이 이 히스토리를 읽는다. 지금 읽는 쪽은 주 시점뿐이지만 히스토리는 뷰마다 둔다.
         */
        void recordDepthHistories();
        /** @brief 뷰 @p target 하나의 깊이 히스토리 복사를 기록합니다(`recordDepthHistories` 가 뷰마다 부른다). */
        void recordDepthHistory( FrameViewTarget& target );

        IRHIDevice* const& _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        FrameRenderState&  _state;
        FramePSOManager&   _psoManager;
        FrameFog&          _fog;
        IRHICommandList*   _pCmd{ nullptr }; ///< `record` 하는 동안만 프리패스 리스트를 가리킨다
        /// @brief 순번 테이블이 없는 뷰(주 시점, 그림자)의 정렬 디스패치가 t2 에 바인딩하는 원소 하나짜리 자리 표시 버퍼입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않습니다.
        RHIStructuredBufferSlot _transparentRankPlaceholder{};
        /// @brief GPU 컬링의 가림 판정(가림 타일 짓기 · 되읽기 · 자리표)입니다.
        OcclusionTiles _occlusion;
        /// @brief 셰이더 assert 링(`-gv_gpuAssert`). assert 가 켜진 디스패치가 u4 로 쓰고, 프리패스 끝에 되읽기를 요청하며 다음 프레임들이 로그로 낸다. 꺼져 있으면 비어 있다.
        GPUAssertMonitor _gpuAssert;
        /// @brief 이번 프레임의 LOD 전환 진행량(0 ~ 255)입니다. 0 이면 크로스페이드 없이 바로 바꾼다(`dispatchCullAndSort` 가 정하고 `dispatchCullView` 가 컬링 상수로 싣는다).
        uint32 _lodFadeStep{ 0 };
        /// @brief 지난 컬링 프레임의 시각(나노초)입니다. 전환 진행량의 프레임 간격을 잰다(0 이면 아직 없다).
        int64 _lodFadePreviousNanoseconds{ 0 };
        /// @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지입니다(`isGPUCullingActive`).
        uint8 _bGPUCullingActive{ 0 };
    };
} // namespace sw
