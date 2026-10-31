/**
 * @file OcclusionTiles.h
 * @brief GPU 컬링의 가림 판정입니다 — 지난 프레임 장면 깊이(깊이 히스토리)를 타일마다 최대 깊이로 줄여(가림 타일) 컬링이 경계 구를 그 뒤에서 거릅니다.
 * @details 흐름: 프레임 처음 컴퓨트 프리패스에서 깊이 히스토리를 옮긴 직후 뷰마다 가림 타일을 짓고(`record`, occlusion_tiles.hlsl), 컬링(gpu_cull.hlsl)이
 *          그 타일을 t2 로 읽는다(`fillCullParams`). 주 시점 타일은 몇 프레임 늦게 CPU 로 되읽어 우편함에 올린다(`requestReadback` · `getSnapshot` — 게임 스레드의
 *          가림 질의). 가림 타일이 없는 컬링(그림자 슬롯 · 가림을 보지 않는 뷰)은 자리표 버퍼(깊이 1 — 아무것도 가리지 않는다)를 묶는다.
 *          명령 리스트 · 디바이스 · PSO 는 패스 실행기(`FramePassExecutor`)가 넘긴다 — 이 객체는 되읽기 고리 · 우편함 · 자리표만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    struct FrameViewTarget;

    /**
     * @class OcclusionTiles
     * @brief 가림 타일 짓기 · 되읽기 · 컬링 상수입니다. 파일 머리말 참고. 렌더 스레드에서만 쓴다(`getSnapshot` 은 어느 스레드든).
     */
    class OcclusionTiles
    {
    public:
        OcclusionTiles()                                   = default;
        OcclusionTiles( const OcclusionTiles& )            = delete;
        OcclusionTiles& operator=( const OcclusionTiles& ) = delete;

        /** @brief 가림 타일이 없는 컬링이 묶는 자리표 버퍼를 만듭니다. 만들지 못하면 오류를 남긴다(컬링이 t2 에 버퍼를 묶지 못한다). */
        void ensurePlaceholder( IRHIDevice* pDevice );
        /** @brief 자리표와 되읽기 고리 · 우편함을 놓습니다. */
        void release( IRHIDevice* pDevice );

        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰 @p target 의 가림 타일(깊이 히스토리 → 타일마다 최대 깊이)을 기록합니다. 깊이 히스토리를 옮긴 뒤 · 컬링 앞입니다.
         * @param pso `RenderPassType::OcclusionTiles` 의 PSO 입니다(없으면 0 — 짓지 않는다).
         * @param bDepthHistoryReady 이번 프레임에 지난 깊이를 옮겼는지입니다(실행기의 `canCopyDepthHistory`).
         * @details `gv_occlusionCulling` 이 꺼졌거나 옮긴 지난 깊이가 없으면 짓지 않는다 — 그 뷰의 컬링은 가림을 보지 않는다.
         */
        void record( IRHICommandList* pCmd, IRHIDevice& device, RHIPipelineStateHandle pso, bool bDepthHistoryReady, FrameViewTarget& target ) const;
        /**
         * @brief 주 시점 가림 타일을 되읽는 복사를 컴퓨트 프리패스 리스트에 넣고, 이미 도착한 사본이 있으면 우편함에 올립니다(`getSnapshot`).
         * @details `record` 직후에 부른다. 이번 프레임에 타일을 짓지 않았거나 기록을 버리는 프레임이면 우편함을 비운다(그 타일은 지난 장면의 것이라 맞지 않는다).
         *          요청 때의 뷰-투영 · 격자는 메타 고리에 적어 두었다가 사본이 도착하면 붙인다.
         */
        void requestReadback( IRHICommandList* pCmd, IRHIDevice* pDevice, const FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 컬링 상수에 가림 판정 값을 채웁니다. 이번 프레임에 타일을 지었고 기록을 버리는 프레임(처음 · 컷)이 아닐 때만 켭니다.
         * @details 지난 프레임 깊이로 이번 경계를 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나온다(언리얼의 지난 프레임 HZB 와 같은 한계). 컷 프레임은 지난 깊이가
         *          다른 장면이라 보지 않는다.
         */
        static void fillCullParams( const FrameViewTarget& view, FrameRendererUtil::GPUCullParams& inoutParams );

        /** @brief 가림 타일이 없는 컬링이 t2 에 묶는 자리표의 SRV 입니다. */
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

        RHIStructuredBufferSlot _placeholder{};                         ///< 가림 타일이 없는 컬링의 t2(원소 하나, 깊이 1)
        AsyncBufferReadback     _readback{};                            ///< 주 시점 가림 타일의 되읽기 고리
        OcclusionTileMailbox    _mailbox{};                             ///< 게임 스레드가 가져가는 우편함
        ReadbackMeta            _arrReadbackMeta[kReadbackMetaCount]{}; ///< 요청 때의 메타
        uint64                  _readbackSerial{ 0 };                   ///< 마지막 요청 번호
    };
} // namespace sw
