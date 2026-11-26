/**
 * @file ScopedAllocationCount.h
 * @brief 스코프 동안 이 스레드의 sw 할당 횟수를 세는 RAII 가드입니다(`Memory::getThreadAllocationCount` 의 앞뒤 차이).
 * @details 배포본(SW_SHIPPING)은 블록 헤더 길이 없어 `getCount()` 가 늘 0 이다 — 수를 보는 단언은 `kCountsAreActive` 로 가른다.
 *          할당 관찰자 칸을 빌리지 않는다 — 예전에는 관찰자를 덮어써 프로파일러(Tracy) 관찰자를 지웠다. 가드는 겹쳐 걸어도 된다.
 */
#pragma once
#include "Core/Foundation/BuildConfig.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::test
{
    /**
     * @class ScopedAllocationCount
     * @brief 살아 있는 동안 이 스레드의 할당을 셉니다.
     */
    class ScopedAllocationCount
    {
    public:
        static constexpr bool kCountsAreActive = !build::kShipping;

        ScopedAllocationCount()
            : _startCount{ Memory::getThreadAllocationCount() }
        {
        }
        ~ScopedAllocationCount()                                         = default;
        ScopedAllocationCount( const ScopedAllocationCount& )            = delete;
        ScopedAllocationCount& operator=( const ScopedAllocationCount& ) = delete;

        /** @brief 가드를 만든 뒤 이 스레드가 한 할당 횟수입니다. 만든 스레드에서 부릅니다. */
        uint64 getCount() const { return Memory::getThreadAllocationCount() - _startCount; }

    private:
        uint64 _startCount;
    };
} // namespace sw::test
