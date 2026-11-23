/**
 * @file GPUSceneDirtySlots.h
 * @brief 칸 순 GPU 버퍼의 "아직 올리지 않은 바뀐 칸" 장부입니다 — 인스턴스(`GPUSceneSlotBuffer`)와 지난 월드 행렬(`GPUInstanceMotionHistory`)이 함께 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /**
     * @class GPUSceneDirtySlots
     * @brief 바뀐 칸을 중복 없이 모아 구간으로 냅니다. 올리지 못한 칸은 `clear` 까지 남습니다.
     */
    class SW_API GPUSceneDirtySlots
    {
    public:
        /** @brief 칸 수를 맞춥니다. 줄면 버린 꼬리의 기록을 걷어 낸다. */
        void resize( uint32 slotCount );
        /** @brief 칸 하나를 바뀐 칸으로 적습니다(같은 칸은 한 번). */
        void mark( uint32 slot );
        /** @brief 올리지 않은 바뀐 칸 수입니다. */
        uint32 getCount() const { return static_cast<uint32>( _listSlot.size() ); }
        /** @brief 다음 업로드가 전체를 올려야 하면 true 입니다(처음 · 버퍼를 새로 만들었다). */
        bool isFullUploadPending() const { return _bFullUploadPending != SW_FALSE; }
        /** @brief 다음 업로드가 전체를 올리게 합니다(버퍼를 새로 만들어 GPU 내용을 모른다). */
        void markFullUpload() { _bFullUploadPending = SW_TRUE; }
        /**
         * @brief 바뀐 칸을 이어진 구간으로 모아 @p outListRun 에 담습니다(칸 오름차순).
         * @return 전체를 올려야 하거나(`isFullUploadPending`) 구간이 @p maxRunCount 를 넘으면 false — 부르는 쪽이 전체를 올린다.
         */
        [[nodiscard]] bool collectRuns( uint32 maxRunCount, vector<GPUInstanceDirtyRange>& outListRun );
        /** @brief 올렸다고 기록합니다(바뀐 칸 · 전체 표시를 비운다). */
        void clear();
        /** @brief 칸을 모두 버립니다. 다음 업로드는 전체다. */
        void reset();

    private:
        vector<uint32> _listSlot; ///< 올리지 않은 바뀐 칸(같은 칸은 한 번 — `_listFlag`)
        vector<uint8>  _listFlag; ///< 칸마다 `_listSlot` 에 들었는지
        uint8          _bFullUploadPending{ SW_TRUE };
    };
} // namespace sw
