/**
 * @file GpuInstanceRing.h
 * @brief 게임 스레드가 짓고 렌더 스레드가 읽는 인스턴스 배열의 **링**입니다. 되복사 없는 발행과 낡은 슬롯 따라잡기를 맡습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

namespace sw
{
    /**
     * @class GpuInstanceRing
     * @brief 인스턴스 배열 슬롯의 링입니다. 쓰는 슬롯과 읽히는 슬롯이 다른 메모리라 복사도 옮기기도 되복사도 없습니다.
     * @details `GpuSceneBuilder` 가 쓰던 것을 떼어 낸 것입니다. 발행 · 재사용 · 따라잡기는 "어느 배열을 누가 읽고 있나" 의
     *          문제이지 씬 수집의 문제가 아니라서, 빌더에 섞여 있으면 빌더의 분기(부분 갱신 · 꼬리 재방출)와 얽혀 읽기가
     *          어려웠습니다. 지금은 이 타입 하나가 규칙을 듭니다:
     *          - **발행**은 슬롯의 `shared_ptr` 를 넘기는 것입니다. 받는 쪽(패킷)이 드는 동안 그 슬롯은 불변입니다.
     *          - **쓰기 슬롯**은 아무도 안 읽는 것(`use_count() == 1`, 링만 듦)을 고릅니다. 패킷이 든 슬롯은 골라지지
     *            않으므로 렌더 큐 깊이를 알 필요가 없습니다. 모자라면 하나 더 만듭니다(큐 깊이 + 1 에서 멈춥니다).
     *          - **따라잡기**: 쓰기 슬롯의 내용은 몇 프레임 전 발행분입니다. 모두 새로 쓰는 프레임은 상관없고, 일부만 고치는
     *            프레임은 먼저 마지막 발행본에 맞춥니다. 슬롯이 발행된 뒤 바뀐 구간만(발행 이력) 복사하고, 이력이 끊겼으면
     *            통째로 복사합니다. 예전의 "발행하며 옮기고 다음 프레임에 되복사"(800 KB · 73 us)가 이것으로 사라졌습니다.
     *          GPU 를 모릅니다. 렌더 스레드는 발행본을 읽어 올릴 뿐입니다.
     */
    class SW_API GpuInstanceRing
    {
    public:
        /// @brief 발행 이력을 이만큼 듭니다. 링 슬롯 수(렌더 큐 깊이 + 1)보다 넉넉하면 됩니다.
        static constexpr size_t kPublishHistoryCount = 8;

        GpuInstanceRing() noexcept = default;
        ~GpuInstanceRing()         = default;

        GpuInstanceRing( GpuInstanceRing&& ) noexcept            = default;
        GpuInstanceRing& operator=( GpuInstanceRing&& ) noexcept = default;
        GpuInstanceRing( const GpuInstanceRing& )                = delete;
        GpuInstanceRing& operator=( const GpuInstanceRing& )     = delete;

        /** @brief 이번 프레임의 쓰기 슬롯을 반환합니다. 없으면 아무도 안 읽는 슬롯을 고르거나 하나 만듭니다. 발행 전까지 같은 슬롯입니다. */
        vector<GpuInstance>& acquireWrite();
        /** @brief 쓰기 슬롯이 잡혀 있으면 true 를 반환합니다(발행하면 놓습니다). */
        bool hasWrite() const { return _pWrite != nullptr; }
        /** @brief 마지막 발행본을 반환합니다. 한 번도 발행하지 않았으면 nullptr 입니다. 읽기만 합니다. */
        const vector<GpuInstance>* getPublished() const { return _pPublished.get(); }

        /**
         * @brief 쓰기 슬롯을 발행합니다. 포인터만 넘기고 이력을 적습니다.
         * @param bAllDirty    이번 발행이 배열 전부를 바꿨는지(이력에 "전부" 로 남습니다)
         * @param listDirtyRun 전부가 아니면 직전 발행에 비해 바뀐 구간
         * @return 발행본. 쓰기 슬롯이 없었으면 마지막 발행본 그대로입니다.
         */
        shared_ptr<const vector<GpuInstance>> publish( bool bAllDirty, const vector<GpuInstanceRun>& listDirtyRun );

        /**
         * @brief 부분 갱신 전에 쓰기 슬롯을 마지막 발행본에 맞춥니다.
         * @details 슬롯이 발행된 뒤 바뀐 구간만 복사합니다. 이력이 끊겼거나 크기가 다르면 통째로 복사합니다(예전의 되복사와 같은 값).
         *          발행본이 없으면 아무것도 하지 않습니다.
         */
        void syncWriteFromPublished();

        /** @brief 슬롯 · 발행본 · 이력을 모두 놓습니다. */
        void clear();

    private:
        /** @brief 발행 하나가 직전 발행에 비해 바꾼 구간입니다. 낡은 슬롯을 따라잡게 할 때 읽습니다. */
        struct PublishRecord
        {
            uint64                 _build{ 0 };
            uint8                  _bAll{ SW_FALSE };
            vector<GpuInstanceRun> _listRun;
        };

        /// @brief 슬롯들입니다. 각 슬롯의 `use_count()` 가 곧 "누가 읽고 있나" 입니다.
        vector<shared_ptr<vector<GpuInstance>>> _listSlot;
        /// @brief 슬롯의 내용이 몇 번째 발행인지입니다(0 = 한 번도 발행 안 됨). `_listSlot` 과 나란히 갑니다.
        vector<uint64> _listSlotBuild;
        /// @brief 이번 프레임에 짓는 슬롯입니다. 발행하면 비웁니다.
        shared_ptr<vector<GpuInstance>> _pWrite;
        /// @brief `_pWrite` 가 링의 몇 번째인지입니다.
        uint32 _writeSlotIndex{ 0 };
        /// @brief 마지막 발행본입니다. 따라잡기가 이전 값을 여기서 읽습니다.
        shared_ptr<const vector<GpuInstance>> _pPublished;
        /// @brief 발행 번호입니다. 슬롯과 이력이 이 번호로 서로를 찾습니다.
        uint64 _publishCounter{ 0 };
        /// @brief 최근 발행들의 변경 구간입니다.
        vector<PublishRecord> _listHistory;
    };
} // namespace sw
