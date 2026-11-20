/**
 * @file HZBOcclusion.h
 * @brief 뷰마다의 HZB 피라미드(장면 깊이의 최소 · 최대 밉 사슬 — `HZBLayout`) 짓기와, GPU 컬링의 가림 판정 상수 · 주 시점 가림 타일 되읽기입니다.
 * @details 피라미드는 뷰의 깊이 사본(깊이 히스토리 텍스처 — R32, RW 텍스처로 읽는다)에서 짓는다(hzb_build.hlsl, 밉마다 디스패치 하나). 한 프레임에 세 번까지 짓는다:
 *          1) 프레임 처음(프레임 패스 `HZBBuild`) — 지난 프레임 장면 깊이. 컬링의 앞 단계(또는 한 단계)가 읽고, 주 시점은 16 px 밉의 최대 깊이를 가림 타일 버퍼에도 써
 *             몇 프레임 늦게 CPU 로 되읽는다(`requestReadback` · `getSnapshot` — 게임 스레드의 가림 질의).
 *          2) 깊이 프리패스 뒤(2 단계 가림의 뒤 단계 패스 `LateOcclusion`, `FrameGraphExecutor::recordLateOcclusion`) — 이번 프레임에 앞 단계가 그린 깊이. 뒤 단계 컬링이 읽는다.
 *          3) 화면 공간 반사 앞(`FrameGraphExecutor::recordSceneDepthHZB`) — 이번 프레임 불투명을 다 그린 깊이. hi-z trace 가 최소 깊이를 읽는다.
 *          버퍼는 뷰마다 하나이고 짓는 때마다 덮는다 — 읽는 쪽은 늘 바로 앞에 지은 것을 읽는다(그래서 무엇을 그린 깊이인지 · 배치를 뷰가 함께 든다: `FrameViewTarget::_hzbLayout`).
 *          명령 리스트 · 디바이스 · PSO 는 실행기(`FrameGraphExecutor`)가 넘긴다 — 이 객체는 되읽기 고리 · 우편함 · 자리표만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/Renderer/Graph/RenderGraph.h"

namespace sw
{
    struct OcclusionCullingConfig;
    class FrameBufferTable;
    class IRHICommandList;
    class IRHIPassCommandList;
    class IRHIDevice;
    struct FrameViewTarget;

    /**
     * @class HZBOcclusion
     * @brief HZB 짓기 · 가림 타일 되읽기 · 컬링 상수입니다. 파일 머리말 참고. 렌더 스레드에서만 쓴다(`getSnapshot` 은 어느 스레드든).
     */
    class HZBOcclusion
    {
    public:
        HZBOcclusion()                                 = default;
        HZBOcclusion( const HZBOcclusion& )            = delete;
        HZBOcclusion& operator=( const HZBOcclusion& ) = delete;

        /** @brief 가림 판정을 보지 않는 컬링이 t2 에 묶는 자리표 버퍼를 만듭니다. 만들지 못하면 오류를 남긴다(컬링이 t2 에 버퍼를 묶지 못한다). */
        void ensurePlaceholder( IRHIDevice* pDevice );
        /** @brief 자리표와 되읽기 고리 · 우편함을 놓습니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 프레임 그래프의 피라미드 자원(파이프라인 XML `HZBBuild` 의 출력 `HZB`)입니다 — 뷰 칸마다 `FrameViewTarget::_hzb`. */
        static RenderGraphBufferUAV getPyramidUAV();
        /** @brief 프레임 그래프의 가림 타일 자원(파이프라인 XML `HZBBuild` 의 출력 `OcclusionTiles`)입니다 — 뷰 칸마다 `FrameViewTarget::_occlusionTiles`. */
        static RenderGraphBufferUAV getTilesUAV();
        /**
         * @brief 이번 프레임 프레임 패스 `HZBBuild` 가 짓는 뷰의 피라미드 · 가림 타일을 @p table 의 칸 @p viewSlot 으로 등록합니다(처음 접근 셰이더 읽기 — 첫 사용이 UAV 쓰기라
         *        그 전이는 늘 나간다). 버퍼가 없으면 등록하지 않는다(짓지도 않는다).
         */
        static void registerFrameResources( FrameBufferTable& table, uint32 viewSlot, const FrameViewTarget& target );

        /**
         * @brief 프레임 패스 `HZBBuild` 에서 뷰 @p target 의 깊이 사본(이미 채운 `_depthHistory`)으로 피라미드를 짓는 디스패치를 기록하고, 지은 배치 · 뷰-투영을 뷰에 적습니다(`_bHZBValid`).
         * @details 전이는 그래프 레벨 콜백이 냈다(`HZB` · `OcclusionTiles` 를 UAV 로, 읽는 컬링 앞에서 SRV 로). 밉 사이 의존은 선언한 UAV 핸들의 `uavBarrier` 다.
         *          깊이 사본은 생산자(`FrameGraphExecutor::recordDepthHistory`)가 RW 텍스처 상태로 넘겨 둔다.
         * @param graphContext 이 패스의 그래프 문맥(선언한 접근 · 자원 표 · 리스트)입니다.
         * @param viewSlot 이 뷰의 칸(`registerFrameResources` 와 같은 값)입니다.
         * @param pso `RenderPassKind::HZBBuild` 의 PSO 입니다(없으면 0 — 짓지 않는다).
         * @param depthWidth · depthHeight 깊이 사본에서 읽을 사각형(px) — 자동 렌더 배율이면 텍스처보다 작다.
         * @param viewProj 그 깊이를 그린 뷰-투영(행 벡터 규약)입니다.
         * @param bWriteTiles 16 px 밉의 최대 깊이를 가림 타일 버퍼에도 쓸지입니다(프레임 처음 주 시점 — 되읽기).
         * @return 지었으면 true. 버퍼 · 사본 · PSO 가 없거나 용량이 모자라면 false 이고 `_bHZBValid` 를 내린다.
         */
        bool record( const RenderGraphPassContext& graphContext, uint32 viewSlot, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight,
                     const float4x4& viewProj, bool bWriteTiles, FrameViewTarget& target ) const;
        /**
         * @brief 뷰 그래프 패스 안(2 단계 가림의 뒤 단계 · 화면 공간 반사 앞)에서 피라미드를 짓습니다. 인자 뜻은 프레임 패스 판과 같다.
         * @details 부르는 패스가 피라미드 · 가림 타일을 UAV 로 선언했다(`ViewGraphResource::hzb` · `occlusionTiles` — 뷰 자원 표의 칸 0). 앞 UAV 는 레벨 콜백이 내고, 밉 사이는
         *          선언한 핸들의 `uavBarrier`, 끝은 같은 패스가 읽는 셰이더 읽기로 `transition` 한다 — 손 전이가 없다.
         */
        bool recordInViewPass( const RenderGraphPassContext& graphContext, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight,
                               const float4x4& viewProj, FrameViewTarget& target ) const;
        /**
         * @brief 주 시점 가림 타일을 되읽는 복사를 프레임 그래프 리스트에 넣고, 이미 도착한 사본이 있으면 우편함에 올립니다(`getSnapshot`).
         * @details 프레임 처음 짓기 직후에 부른다. 이번 프레임에 타일을 짓지 않았거나 기록을 버리는 프레임이면 우편함을 비운다(그 타일은 지난 장면의 것이라 맞지 않는다).
         *          요청 때의 뷰-투영 · 격자는 메타 고리에 적어 두었다가 사본이 도착하면 붙인다.
         */
        void requestReadback( IRHICommandList* pCmd, IRHIDevice* pDevice, bool bEnabled, const FrameViewTarget& view );
        /**
         * @brief 컬링 상수에 뷰 @p view 의 지금 피라미드로 가림 판정 값을 채웁니다. 피라미드가 없으면(`_bHZBValid` 가 아님) 판정을 끈다(발자국 0).
         * @details 프레임 처음 피라미드는 지난 프레임 깊이로 이번 경계를 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나온다 — 2 단계 가림의 뒤 단계가 그것을 이번 프레임 깊이로
         *          다시 본다(`FrameGraphExecutor::recordLateOcclusion`). 컷 프레임은 프레임 처음 피라미드를 짓지 않는다(지난 깊이가 다른 장면이다).
         */
        static void fillCullParams( const FrameViewTarget& view, const OcclusionCullingConfig& config, FrameRendererUtil::GPUCullParams& inoutParams );

        /** @brief 가림 판정을 보지 않는 컬링이 t2 에 묶는 자리표의 SRV 입니다. */
        RHIDescriptorIndex getPlaceholderSRV() const { return _placeholder._srv; }
        /** @brief 주 시점 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다. 어느 스레드에서 불러도 됩니다. */
        shared_ptr<const OcclusionTileSnapshot> getSnapshot() const { return _mailbox.getLatest(); }

    private:
        /** @brief 되읽기 요청 때의 메타데이터입니다(요청 번호 % 개수 로 찾는다). 요청한 프레임과 사본이 도착하는 프레임은 다르다. */
        struct ReadbackMeta
        {
            float4x4 _viewProj{};
            uint32   _columns{ 0 };
            uint32   _rows{ 0 };
            float32  _depthWidth{ 0.0f };
            float32  _depthHeight{ 0.0f };
            uint8    _bValid{ SW_FALSE }; ///< 이 요청의 타일이 판정에 쓸 수 있는 프레임이었다(기록을 버리는 프레임이 아니었다)
        };
        static constexpr uint32 kReadbackMetaCount = 8;

    private:
        /**
         * @brief 피라미드 디스패치(밉마다 하나)를 기록합니다. 짓지 못하면(버퍼 · 사본 · PSO · 용량) 아무것도 기록하지 않고 false 입니다.
         * @details @p barriers 가 디스패치 앞(`begin`) · 밉 사이(`betweenMips`) · 뒤(`end`)의 배리어를 낸다 — 프레임 패스는 그래프 핸들, 뷰 패스 안은 손 배리어다(cpp 의 두 판).
         */
        template <typename Barriers>
        bool recordPyramid( IRHIPassCommandList& cmd, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight, const float4x4& viewProj, bool bWriteTiles,
                            FrameViewTarget& target, const Barriers& barriers ) const;

    private:
        RHIStructuredBuffer  _placeholder{};                         ///< 가림 판정을 보지 않는 컬링의 t2(float2 하나 — 최소 · 최대 깊이 1)
        AsyncBufferReadback  _readback{};                            ///< 주 시점 가림 타일 되읽기 고리
        ReadbackMeta         _arrReadbackMeta[kReadbackMetaCount]{}; ///< 요청마다의 뷰-투영 · 격자
        uint64               _readbackSerial{ 0 };                   ///< 마지막 요청 번호
        OcclusionTileMailbox _mailbox{};                             ///< 게임 스레드가 가져가는 가장 최근 사본
    };
} // namespace sw
