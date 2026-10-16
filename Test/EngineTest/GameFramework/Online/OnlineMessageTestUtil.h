/**
 * @file OnlineMessageTestUtil.h
 * @brief 키트 메시지(`XxxProtocol::MessageTypes`)의 왕복과 거절을 보는 테스트 도우미입니다.
 * @details 스위트 파일이 아닙니다. 키트 테스트 파일이 include 해 키트마다 왕복 테스트 하나를 둡니다.
 *          왕복은 쓰고 읽은 값이 `FixedLayout::equals` 로 같은지 봅니다. 잘림은 바이트를 하나씩 줄인 모든 앞부분이 `Ok` 가 아닌지 봅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace test
{
    struct OnlineMessageTestUtil
    {
        /** @brief @p message 를 쓰고 같은 타입으로 읽어 같으면 true 입니다. 쓰기가 실패해도 false 입니다. */
        template <typename TMessageTypes, typename T>
        static bool roundTrips( const T& message )
        {
            sw::BitWriter writer;
            if ( TMessageTypes::write( writer, message ) == false )
                return false;
            sw::BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            T             decoded{};
            return TMessageTypes::read( reader, decoded ) == sw::FixedReadResult::Ok && sw::FixedLayout::equals( decoded, message );
        }

        /** @brief @p message 를 쓴 바이트에서 하나씩 줄인 모든 앞부분을 읽어, 하나라도 `Ok` 가 되면 false 입니다. 읽기 실패 뒤 대상이 그대로인지도 봅니다. */
        template <typename TMessageTypes, typename T>
        static bool refusesEveryPrefix( const T& message )
        {
            sw::BitWriter writer;
            if ( TMessageTypes::write( writer, message ) == false )
                return false;
            const T untouched{};
            for ( int32 byteCount = 0; byteCount < writer.getByteCount(); ++byteCount )
            {
                sw::BitReader reader( writer.getBytes().data(), byteCount );
                T             decoded{};
                if ( TMessageTypes::read( reader, decoded ) == sw::FixedReadResult::Ok )
                    return false;
                if ( sw::FixedLayout::equals( decoded, untouched ) == false )
                    return false;
            }
            return true;
        }

        /** @brief @p message 를 쓴 바이트를 다른 메시지 타입 @p TOther 로 읽으면 스키마 해시가 달라 `SchemaMismatch` 인지 봅니다. 메서드 번호가 어긋난 빌드를 흉내 냅니다. */
        template <typename TMessageTypes, typename TOther, typename T>
        static bool refusesAsOtherType( const T& message )
        {
            sw::BitWriter writer;
            if ( TMessageTypes::write( writer, message ) == false )
                return false;
            sw::BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            TOther        other{};
            return TMessageTypes::read( reader, other ) == sw::FixedReadResult::SchemaMismatch;
        }

        /** @brief @p message 를 쓰지 못하면(범위 밖 값) true 입니다. */
        template <typename TMessageTypes, typename T>
        static bool refusesToWrite( const T& message )
        {
            sw::BitWriter writer;
            return TMessageTypes::write( writer, message ) == false;
        }
    };
} // namespace test
