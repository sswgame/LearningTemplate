/**
 * @file Delegate.cpp
 * @brief 멀티캐스트 핸들 발급기 — 이 실체가 프로세스에 **하나여야** 한다.
 */
#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 발급된 마지막 핸들 ID. 0 은 무효값이라 1 부터 센다.
         * @details 여러 스레드가 동시에 구독할 수 있으므로 원자다. 순서는 상관없고 **겹치지 않기만**
         *          하면 되므로 `relaxed` 로 충분하다 — 이 값으로 다른 메모리를 동기화하지 않는다.
         */
        atomic<uint64> s_nextHandleId{ 1 };
    } // namespace
} // namespace sw

namespace sw
{
    DelegateHandle DelegateHandle::allocate()
    {
        DelegateHandle handle{};
        handle._id = s_nextHandleId.fetch_add( 1, std::memory_order_relaxed );
        return handle;
    }
} // namespace sw
