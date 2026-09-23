/**
 * @file Delegate.cpp
 * @brief 멀티캐스트 핸들 발급기입니다. 이 실체는 프로세스에 **하나만** 있어야 합니다.
 */
#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 다음에 발급할 핸들 ID 입니다. 0 은 무효값이라 1 부터 시작합니다.
         * @details 여러 스레드가 동시에 구독할 수 있으므로 원자 변수입니다. 순서는 상관없고 **겹치지 않기만** 하면 되므로
         *          `relaxed` 로 충분합니다. 이 값으로 다른 메모리를 동기화하지 않기 때문입니다.
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
