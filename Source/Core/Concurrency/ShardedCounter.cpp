#include "pch.h"

#include "Core/Concurrency/ShardedCounter.h"

#include "Core/Common/Defines.h"

namespace sw
{
    namespace
    {
        /** @brief 다음 스레드에 줄 칸 차례입니다. 칸 수로 접어 쓰므로 넘쳐도 된다. */
        atomic<uint32> s_nextThreadShard{ 0 };
        /** @brief 이 스레드의 칸 번호입니다. 아직 받지 않았으면 `invalid_index::kUint32` 입니다. */
        thread_local uint32 t_threadShard = invalid_index::kUint32;
    } // namespace
} // namespace sw

namespace sw
{
    uint32 ShardedCounterUtil::getCurrentThreadShard()
    {
        if ( t_threadShard == invalid_index::kUint32 )
            t_threadShard = s_nextThreadShard.fetch_add( 1, std::memory_order_relaxed ) & kShardMask;
        return t_threadShard;
    }
} // namespace sw
