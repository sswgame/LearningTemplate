/**
 * @file VFXMeshSlotVisibility.h
 * @brief 메시 방출기의 인스턴스 배치에서 살아 있는 입자의 칸만 보이게 두는 표(`VFXMeshSlotVisibility`)입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class MeshInstanceBatch;

    /**
     * @class VFXMeshSlotVisibility
     * @brief 지난번 보인 칸을 들고, 이번 칸 목록과 다른 칸만 숨기거나 보입니다. 배치의 나머지 칸은 숨긴 채 둡니다.
     * @details 칸은 입자가 수명 내내 쥐는 인스턴스 칸이다(`VFXCPUSimulation::writeInstanceSlots`). 죽은 칸은 후보에서 빠지고(`MeshInstanceBatch::setEntryVisible`)
     *          살아 있는 칸만 그려진다 — 그리는 수가 살아 있는 수다(언리얼 Niagara 메시 렌더러의 인스턴스 수, 유니티 VFX Graph 의 alive list).
     *          숨김 토글은 그 칸 하나의 일이라 빌더가 부분 수집으로 그 칸만 고친다(불투명은 배치 구성원만, `GPUSceneBuilder::patchBatchMembership`).
     *          한 번에 드는 일은 지난번 · 이번 칸 수에 비례한다(배치 크기와 무관).
     */
    class SW_API VFXMeshSlotVisibility
    {
    public:
        /** @brief @p batch 의 칸을 모두 숨기고 표를 비웁니다. 배치를 새로 만든 뒤 등록 전에 부릅니다. */
        void reset( MeshInstanceBatch& batch );
        /**
         * @brief 보일 칸을 @p listSlot 으로 바꿉니다 — 지난번 보였고 이번에 없는 칸은 숨기고, 이번 칸은 보입니다. 칸은 배치 항목 수보다 작고 서로 달라야 합니다.
         * @details 값(행렬 · 바운드 · 색)은 부르는 쪽이 이번 칸마다 쓴다.
         */
        void update( MeshInstanceBatch& batch, const vector<uint32>& listSlot );
        /** @brief 지금 보이는 칸입니다(마지막 `update` 의 목록). */
        const vector<uint32>& getVisibleSlots() const { return _listVisibleSlot; }

    private:
        vector<uint32> _listVisibleSlot; ///< 지난번 보인 칸
        vector<uint8>  _listSlotMark;    ///< 칸마다 이번에 보이면 1(배치 항목 수 길이, 지난 · 이번 칸만 고쳐 쓴다)
    };
} // namespace sw
