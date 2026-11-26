/**
 * @file GPUSceneSlotBuffer.h
 * @brief GPUScene 인스턴스 버퍼의 칸 순 CPU 사본과 바뀐 칸 목록입니다(렌더 스레드).
 * @details GPU 의 인스턴스 버퍼는 칸 순이다 — 원소 번호가 GPUScene 칸(= 레지스트리 칸 = 빌더의 후보 번호)이고, 빈 칸은 배치 번호가 무효인 구멍이다
 *          (언리얼 GPUScene 의 영속 프리미티브 · 인스턴스 칸, 유니티 GPU Resident Drawer 의 인스턴스 칸). 그리는 순서(배치 순)는 따로 든 번호 목록
 *          (차례 → 칸, `GPUSceneSnapshot::_pListInstanceSlot`)이 정한다. 그래서 배치를 다시 나눠 그리는 순서가 바뀌어도 칸의 내용은 그대로라 올리지 않고,
 *          내용이 바뀐 칸만 흩어 올린다(scatter).
 *          스냅샷이 칸 순 배열과 바뀐 칸 구간을 싣는다(빌더가 칸 순으로 채운다). 이 사본은 그 구간만 비교해 받아 두고, 올릴 때까지 바뀐 칸을 쌓는다 —
 *          업로드를 건너뛴 프레임(장치 없음 · 빈 장면)의 변경도 다음 업로드에 실린다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneDirtySlots.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /**
     * @class GPUSceneSlotBuffer
     * @brief 칸 순 인스턴스 배열의 CPU 사본입니다. 스냅샷마다 `apply` 로 맞추고, 바뀐 칸을 `collectRuns` 로 구간에 모아 올린 뒤 `clearPending` 합니다.
     * @details 올리지 못한 바뀐 칸은 다음 `apply` 뒤에도 남는다(올릴 때까지 쌓인다). 버퍼를 새로 만들었으면 `markFullUpload` 로 전체를 올리게 한다.
     */
    class SW_API GPUSceneSlotBuffer
    {
    public:
        /** @brief 구멍 칸의 배치 번호입니다. 컬링(`gpu_cull.hlsl`)은 배치 번호가 표 밖인 칸을 건너뛴다. */
        static constexpr uint32 kHoleBatchIndex = constant::invalid_index::kUint32;

        /** @brief 구멍 칸의 내용입니다(배치 번호 무효 · 회전 없음 · 기록 열쇠 없음). */
        static GPUInstance makeHole();

        /**
         * @brief 칸 순 배열 @p listSlotInstance 로 사본을 맞추고 내용이 바뀐 칸을 기록합니다. 칸 수가 줄면 꼬리를 버리고, 늘면 새 칸을 바뀐 칸으로 센다(GPU 쪽 내용을 모른다).
         * @param listDirtyRange 내용이 바뀐 칸 구간입니다. @p bAllDirty 면 무시하고 모든 칸을 본다(비교만 하므로 같은 칸은 올리지 않는다). 비었으면 바뀐 칸이 없다.
         */
        void apply( const vector<GPUInstance>& listSlotInstance, const vector<GPUInstanceDirtyRange>& listDirtyRange, bool bAllDirty );
        /** @brief 칸 순 사본입니다. 원소 번호가 GPUScene 칸입니다. */
        const vector<GPUInstance>& getSlots() const { return _listSlot; }
        /** @brief 다음 업로드가 전체를 올리게 합니다(버퍼를 새로 만들어 GPU 내용을 모른다). */
        void markFullUpload() { _dirty.markFullUpload(); }
        /**
         * @brief 바뀐 칸을 이어진 구간으로 모아 @p outListRun 에 담습니다(칸 오름차순).
         * @return 전체를 올려야 하거나(`isFullUploadPending`) 구간이 @p maxRunCount 를 넘으면 false — 부르는 쪽이 사본 전체를 올린다.
         */
        [[nodiscard]] bool collectRuns( uint32 maxRunCount, vector<GPUInstanceDirtyRange>& outListRun ) { return _dirty.collectRuns( maxRunCount, outListRun ); }
        /** @brief 올렸다고 기록합니다(바뀐 칸 · 전체 표시를 비운다). */
        void clearPending() { _dirty.clear(); }
        /** @brief 지난번 호출 뒤 `apply` 가 바꾼 칸을 @p outListSlot 에 넘기고 비웁니다(움직임 기록이 그 칸만 본다 — 같은 칸이 되풀이될 수 있다). 업로드 여부와 무관하다. */
        void takeChangedSlots( vector<uint32>& outListSlot );
        /** @brief 사본과 기록을 모두 버립니다(씬을 비울 때). 다음 `apply` 뒤 업로드는 전체다. */
        void reset();

    private:
        /** @brief 칸 하나를 @p instance 로 맞춥니다. 내용이 다르면 바꾸고 바뀐 칸으로 기록합니다. */
        void writeSlot( uint32 slot, const GPUInstance& instance );

    private:
        vector<GPUInstance> _listSlot;
        vector<uint32>      _listChangedSlot; ///< `takeChangedSlots` 가 가져갈 칸
        GPUSceneDirtySlots  _dirty;           ///< 올리지 않은 바뀐 칸
    };
} // namespace sw
