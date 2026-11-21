/**
 * @file StateBytesTestUtil.h
 * @brief 키트 시뮬레이션의 `writeState` 와 `readState` 테스트가 함께 쓰는 바이트 비교와 결정성 확인입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/Format/Archive.h"

#include <cstring>

namespace sw
{
    /**
     * @struct StateBytesTestUtil
     * @brief 왕복과 같은 바이트 확인을 한 줄로 씁니다.
     */
    struct StateBytesTestUtil
    {
        static bool isSame( const Archive& lhs, const Archive& rhs ) { return lhs.getSize() == rhs.getSize() && std::memcmp( lhs.getData(), rhs.getData(), lhs.getSize() ) == 0; }

        /** @brief @p source 의 바이트를 @p outRestored 로 읽고, 다시 쓴 바이트가 처음과 같으면 true 입니다. */
        template <typename TState>
        [[nodiscard]] static bool roundTrips( const TState& source, TState& outRestored )
        {
            Archive written;
            source.writeState( written );
            Archive reading( written.getData(), written.getSize() );
            if ( outRestored.readState( reading ) == false )
                return false;
            Archive rewritten;
            outRestored.writeState( rewritten );
            return isSame( written, rewritten );
        }

        /** @brief 두 상태의 바이트가 같으면 true 입니다. 같은 입력이 같은 판을 내는지 볼 때 씁니다. */
        template <typename TState>
        [[nodiscard]] static bool hasSameState( const TState& lhs, const TState& rhs )
        {
            Archive lhsBytes;
            lhs.writeState( lhsBytes );
            Archive rhsBytes;
            rhs.writeState( rhsBytes );
            return isSame( lhsBytes, rhsBytes );
        }

        /**
         * @brief 키트 규칙의 결정성을 봅니다. 규칙 객체에는 계약을 더하지 않고 초기화와 걸음 람다만 받습니다.
         * @details 다음 셋이 모두 맞으면 true 입니다.
         *          1. 새 상태 둘에 같은 걸음을 @p stepCount 번 돌린 바이트가 같다(난수 · 순서 의존이 없다).
         *          2. @p stepCount / 2 걸음 뒤 저장 → 새로 초기화한 상태로 읽기가 왕복한다(`roundTrips`).
         *          3. 읽은 쪽과 저장한 쪽이 남은 걸음을 이어 돈 바이트가 1 의 바이트와 같다(저장 밖에 숨은 상태가 없다).
         *          상태는 힙에 잡는다 — 월드형 규칙은 스택에 셋을 두기에 크다.
         * @param setup `bool( TState& )` — 같은 카탈로그 · 시드로 초기화합니다. false 면 실패입니다.
         * @param step  `void( TState&, int32 stepIndex )` — 한 걸음(입력 + 갱신)입니다. 입력은 걸음 번호로만 정합니다.
         */
        template <typename TState, typename TSetup, typename TStep>
        [[nodiscard]] static bool isDeterministic( TSetup&& setup, TStep&& step, int32 stepCount )
        {
            const int32 savedStep = stepCount / 2;
            auto        pFirst    = make_unique<TState>();
            auto        pSecond   = make_unique<TState>();
            auto        pSaved    = make_unique<TState>();
            auto        pRestored = make_unique<TState>();
            if ( setup( *pFirst ) == false || setup( *pSecond ) == false || setup( *pSaved ) == false || setup( *pRestored ) == false )
                return false;
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                step( *pFirst, stepIndex );
                step( *pSecond, stepIndex );
            }
            if ( hasSameState( *pFirst, *pSecond ) == false )
                return false;

            for ( int32 stepIndex = 0; stepIndex < savedStep; ++stepIndex )
                step( *pSaved, stepIndex );
            if ( roundTrips( *pSaved, *pRestored ) == false )
                return false;
            for ( int32 stepIndex = savedStep; stepIndex < stepCount; ++stepIndex )
            {
                step( *pSaved, stepIndex );
                step( *pRestored, stepIndex );
            }
            return hasSameState( *pSaved, *pRestored ) && hasSameState( *pRestored, *pFirst );
        }
    };
} // namespace sw
