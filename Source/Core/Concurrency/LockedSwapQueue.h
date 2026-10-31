/**
 * @file LockedSwapQueue.h
 * @brief 여러 스레드가 쌓고 한 스레드가 통째로 가져가는 잠금 큐입니다(명령 큐 · 완료 큐의 몸통).
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @class LockedSwapQueue
     * @brief 쌓기(`push`)는 어느 스레드에서나 하고, 가져가기(`takeAll`)는 빈 목록과 저장소를 맞바꿔 잠금을 짧게 쥡니다.
     * @details 가져가는 쪽이 같은 목록을 비워 다시 쓰면 두 저장소가 번갈아 돌아 프레임마다 할당하지 않는다(언리얼 렌더 명령 큐의 이중 버퍼와 같은 모양).
     *          잠금과 목록을 한 곳에 묶어, 목록을 잠금 없이 만지는 모양을 쓸 수 없게 한다 — 예전에는 쓰는 쪽마다 `mutex` 와 `vector` 를 따로 들었다.
     */
    template <typename T>
    class LockedSwapQueue
    {
    public:
        void push( T&& item )
        {
            ScopedLock<mutex> lock{ _mutex };
            _listPending.push_back( std::move( item ) );
        }
        /**
         * @brief 잠금 안에서 @p stamp( item ) 을 부르고 쌓은 뒤 그 반환값을 돌려줍니다.
         * @details 순번처럼 큐 순서와 같아야 하는 값을 매길 때 쓴다 — 잠금 밖에서 매기면 두 스레드의 순번과 큐 순서가 엇갈린다.
         */
        template <typename TStamp>
        auto pushStamped( T&& item, TStamp&& stamp )
        {
            ScopedLock<mutex> lock{ _mutex };
            auto              result = stamp( item );
            _listPending.push_back( std::move( item ) );
            return result;
        }
        /** @brief 쌓인 것을 비어 있는 @p outList 와 맞바꿉니다. @p outList 가 비어 있지 않으면 어서션입니다(그 항목을 잃는다). */
        void takeAll( vector<T>& outList )
        {
            SW_ASSERT( outList.empty() && "takeAll swaps storage - drain the previous batch first" );
            ScopedLock<mutex> lock{ _mutex };
            outList.swap( _listPending );
        }
        void clear()
        {
            ScopedLock<mutex> lock{ _mutex };
            _listPending.clear();
        }

    private:
        vector<T> _listPending SW_GUARDED_BY( _mutex );
        mutex                  _mutex;
    };
} // namespace sw
