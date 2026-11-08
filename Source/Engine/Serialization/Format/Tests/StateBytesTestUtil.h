/**
 * @file StateBytesTestUtil.h
 * @brief 키트 시뮬레이션의 `writeState` 와 `readState` 테스트가 함께 쓰는 바이트 비교입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

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
    };
} // namespace sw
