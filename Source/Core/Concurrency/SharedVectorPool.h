/**
 * @file SharedVectorPool.h
 * @brief 다른 스레드에 `shared_ptr` 로 발행하는 배열을 프레임마다 새로 잡지 않고, 아무도 쥐지 않은 것을 다시 쓰는 풀입니다.
 * @details 일반 컨테이너가 아니라 스레드 사이 넘기기(게임 → 렌더 발행) 정책이라 `LockedSwapQueue` · `SpscQueue` 옆 Concurrency 에 둡니다.
 *          쓰는 곳이 VFX(층 6)와 렌더러(층 7) 둘이라 렌더러 폴더에 두면 VFX 가 위층을 보게 됩니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class SharedVectorPool
     * @brief 게임 스레드가 채워 렌더 스레드에 넘기는 배열의 풀입니다. 한 스레드(만드는 쪽)에서만 부릅니다.
     * @details 소유 수로 판단하므로 패킷 링 깊이를 알 필요가 없다 — 패킷 · 스냅샷이 쥔 동안은 다시 쓰지 않고, 모두 놓은 배열만 비워서 돌려준다.
     *          풀이 아니면 쓰는 쪽이 놓는 순간 메모리가 돌아가므로 칸 수는 동시에 살아 있는 배열 수(패킷 깊이 + 쓰는 중 하나)로 머문다.
     *          칸마다 용량이 남는 것은 스크래치 벡터와 같은 맞바꿈이다(언리얼 렌더 스레드 자원 풀의 재사용과 같은 자리, 기억에 의존한 비교).
     */
    template <typename T>
    class SharedVectorPool
    {
    public:
        /** @brief 비어 있고 아무도 쥐지 않은 배열을 돌려줍니다. 없으면 새로 만듭니다. */
        [[nodiscard]] shared_ptr<vector<T>> acquire()
        {
            for ( const shared_ptr<vector<T>>& pList : _listSlot )
            {
                if ( pList.use_count() != 1 )
                    continue;
                // 풀만 쥐었다면 더 늘어날 수 없다(복사는 이 스레드가 돌려줄 때뿐이다). 다른 스레드가 놓기 전에 읽은 것과 순서를 맞춘다.
                std::atomic_thread_fence( std::memory_order_acquire );
                pList->clear();
                return pList;
            }
            _listSlot.push_back( sw::make_shared<vector<T>>() );
            return _listSlot.back();
        }

        /** @brief 만든 칸 수입니다(진단 · 테스트용). */
        uint32 getSlotCount() const { return static_cast<uint32>( _listSlot.size() ); }

    private:
        vector<shared_ptr<vector<T>>> _listSlot;
    };
} // namespace sw
