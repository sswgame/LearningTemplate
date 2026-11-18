/**
 * @file GPUBatchMembershipPatch.h
 * @brief 배치 순서를 그대로 두고 배치의 구성원만 바꾸는 한 빌드의 변경(빠지는 칸 · 들어오는 칸)과, 그것을 그리는 차례 · 배치 구간에 반영하는 규칙입니다.
 * @details 빌더(`GPUSceneBuilder::patchBatchMembership`)가 불투명 후보가 기존 배치에 드나들기만 한 빌드에서 쓴다 — 다시 나누기(정렬)와 전체 방출 없이
 *          배치마다 남은 구성원 뒤에 들어온 구성원을 붙인다(언리얼 FScene 의 프리미티브 더하기 · 빼기, 유니티 GPU Resident Drawer 의 배치 인스턴스 목록).
 *          배치 번호가 그대로라 칸 순 인스턴스 배열의 다른 칸(배치 번호)은 바뀌지 않는다. 씬 · 머티리얼을 모르는 규칙만 따로 두어 단위 테스트가 직접 본다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /** @brief 배치 @p _batchIndex 에 들어가는 칸 @p _slot 입니다. */
    struct GPUBatchMemberEntry
    {
        uint32 _batchIndex{ 0 };
        uint32 _slot{ 0 };
    };

    /**
     * @class GPUBatchMembershipPatch
     * @brief 한 빌드에서 배치에 들어가고 빠지는 칸입니다. `reset` 으로 칸 수를 정하고 `markLeave` · `addEnter` 로 채운 뒤 `rewriteOrder` 로 차례에 반영합니다.
     * @details 키가 바뀐 칸은 빠짐(옛 배치)과 들어옴(새 배치)이 둘 다다.
     */
    class SW_API GPUBatchMembershipPatch
    {
    public:
        static constexpr uint8 kLeave = 1u << 0; ///< 지난 차례에서 빠진다(지움 · 숨김 · 키 바뀜)
        static constexpr uint8 kEnter = 1u << 1; ///< 배치에 들어간다(더함 · 보임 · 키 바뀜)

        /** @brief 칸 @p slotCount 개의 빈 변경으로 되돌립니다. */
        void reset( uint32 slotCount );
        /** @brief 칸 @p slot 이 지난 차례에서 빠집니다. */
        void markLeave( uint32 slot );
        /** @brief 칸 @p slot 이 배치 @p batchIndex 에 들어갑니다. */
        void addEnter( uint32 batchIndex, uint32 slot );
        /** @brief 칸마다의 표시(`kLeave` · `kEnter`)입니다. 칸 수만큼입니다. */
        const vector<uint8>& getFlags() const { return _listFlag; }
        /** @brief 들어오는 칸입니다(`rewriteOrder` 뒤에는 배치 번호 오름차순). */
        const vector<GPUBatchMemberEntry>& getEntries() const { return _listEnter; }

        /**
         * @brief 배치 @p pBatch[0, @p batchCount) 의 차례를 다시 씁니다. 배치마다 지난 차례에서 빠지는 칸을 빼고, 들어오는 칸을 끝에 붙입니다.
         * @details 배치의 `_instanceBase` · `_instanceCount` · `_visibleBase` · `_visibleCapacity` 를 새 차례로 고친다. LOD 단계 배치(인스턴스가 없고 단계 수가 0)는 인스턴스 구간이 비고
         *          가시 구간은 바로 앞 머리 배치만큼이다(`GPUSceneBuilder::emitLODLevelBatches`). 들어오는 칸의 배치 번호는 @p batchCount 보다 작아야 한다.
         *          [@p freshBatchBegin, @p batchCount) 는 이번에 끝에 새로 붙인 배치다 — 지난 구성원이 없고 LOD 단계 배치로 보지 않는다(들어오는 칸만 받는다).
         * @param freshBatchBegin 새로 붙인 첫 배치 번호입니다. 새 배치가 없으면 @p batchCount 입니다.
         * @param listOldOrder  지난 차례 → 칸입니다. 배치들의 지난 구간이 이 번호다.
         * @param outListOrder  새 차례 → 칸입니다(앞 내용은 지운다). 배치 @p batchCount 개의 구간만 담는다.
         * @param ioListSlotToOrder 칸 → 새 차례를 적습니다(칸 수만큼 있어야 한다). 빠지기만 한 칸은 건드리지 않는다.
         */
        void rewriteOrder( GPUMeshBatch* pBatch, uint32 batchCount, uint32 freshBatchBegin, const vector<uint32>& listOldOrder, vector<uint32>& outListOrder, vector<uint32>& ioListSlotToOrder );

    private:
        vector<uint8>               _listFlag;
        vector<GPUBatchMemberEntry> _listEnter;
    };
} // namespace sw
