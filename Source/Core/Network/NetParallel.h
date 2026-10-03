/**
 * @file NetParallel.h
 * @brief 서버 키트가 연결(관찰자)마다의 일을 작업 스레드에 나눠 돌리는 도구 — `NetParallelFor`(나누기) · `NetParallelScratch<T>`(스레드마다 하나씩 쓰는 작업 자리)입니다.
 * @details 권위 서버의 스냅샷 만들기 · MMO 관심 영역 계산은 연결 수에 비례하고 연결끼리 서로 건드리지 않습니다. 그 몸을 `TaskManager::runParallel`
 *          로 나누면 코어 수만큼 빨라집니다. 보내기는 `NetHost` 가 잠금으로 지키므로 여러 스레드가 동시에 `sendMessage` 해도 된다.
 *          `TaskManager` 를 주지 않으면(시험 · 도구 · 작은 서버) 지금 스레드가 한 번에 돈다 — 결과는 같다(연결마다 독립이라 순서가 바뀌어도 같다).
 *          게임은 `setTaskManager( &engine::getTaskManager() )` 를 넘깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    /**
     * @class NetParallelFor
     * @brief [0, count) 를 `TaskManager` 워커에 나눠 돌리고 끝날 때까지 기다립니다. 매니저가 없거나 일이 문턱보다 적으면 지금 스레드가 돈다.
     */
    class NetParallelFor
    {
    public:
        static constexpr uint32 kDefaultSerialThreshold = 8; ///< 이보다 적은 연결은 나누지 않는다(디스패치 비용 ~7..34 us 보다 작은 일)

        NetParallelFor()
            : _pTaskManager{ nullptr }
            , _serialThreshold{ kDefaultSerialThreshold }
        {
        }

        void setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = kDefaultSerialThreshold )
        {
            _pTaskManager    = pTaskManager;
            _serialThreshold = serialThreshold > 0 ? serialThreshold : 1;
        }

        /** @brief 본문은 (start, end) 구간을 받습니다. 스레드마다의 작업 자리는 `getCurrentSlot` 으로 찾는다. */
        void run( uint32 count, const ParallelBlockDelegate& body ) const
        {
            if ( count == 0 || body.isBound() == false )
                return;
            if ( _pTaskManager == nullptr || _pTaskManager->getWorkerCount() == 0 )
            {
                body( 0, count );
                return;
            }
            _pTaskManager->runParallel( count, _serialThreshold, body );
        }

        /** @brief 작업 자리 수 — 워커 수 + 기다리며 돕는 스레드 몫(매니저가 없으면 1). */
        uint32 getSlotCount() const { return _pTaskManager != nullptr ? _pTaskManager->getScratchSlotCount() : 1; }
        /** @brief 지금 스레드의 작업 자리입니다(본문 안에서 부른다). 한 자리는 한 번에 한 스레드만 쓴다. */
        uint32 getCurrentSlot() const { return _pTaskManager != nullptr ? _pTaskManager->getCurrentThreadScratchSlot() : 0; }
        bool   isParallel() const { return _pTaskManager != nullptr && _pTaskManager->getWorkerCount() > 0; }

    private:
        TaskManager* _pTaskManager;
        uint32       _serialThreshold;
    };

    /**
     * @class NetParallelScratch
     * @brief 스레드마다 하나씩 쓰는 작업 자리(목록 · 쓰기 버퍼)입니다. 틱마다 다시 쓰므로 용량이 남는다.
     */
    template <typename TScratch>
    class NetParallelScratch
    {
    public:
        NetParallelScratch()
            : _listSlot{}
        {
        }

        /** @brief 자리 수를 맞춥니다 — 나누기 전에 지금 스레드에서 부른다(나누는 중에는 크기를 바꾸지 않는다). */
        void prepare( const NetParallelFor& parallel )
        {
            const size_t slotCount = static_cast<size_t>( parallel.getSlotCount() );
            if ( _listSlot.size() < slotCount )
                _listSlot.resize( slotCount );
        }

        TScratch& acquire( const NetParallelFor& parallel ) { return _listSlot[static_cast<size_t>( parallel.getCurrentSlot() )]; }

        vector<TScratch>&       getSlots() { return _listSlot; }
        const vector<TScratch>& getSlots() const { return _listSlot; }

    private:
        vector<TScratch> _listSlot;
    };
} // namespace sw
