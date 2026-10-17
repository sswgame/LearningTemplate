/**
 * @file OnlineMessageTestUtil.h
 * @brief 키트 메시지(`XxxProtocol::MessageTypes`)의 왕복과 거절을 보는 테스트 도우미입니다.
 * @details 스위트 파일이 아닙니다. 키트 테스트 파일이 include 해 키트마다 왕복 테스트 하나를 둡니다.
 *          왕복은 쓰고 읽은 값이 `FixedLayout::equals` 로 같은지 봅니다. 잘림은 바이트를 하나씩 줄인 모든 앞부분이 `Ok` 가 아닌지 봅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
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

        /**
         * @brief @p T 의 @p fieldName 필드에 선언된 `MaxCount` 입니다. 고정 배치형 코덱의 정규 문자열(`FixedCodec::_pCanonical`)에서 읽으며, 코덱이나 그 필드가 없으면 0 입니다.
         * @details 애노테이션은 정수 리터럴만 받으므로 같은 상한을 상수로도 두는 곳은 둘을 이 값으로 묶는 테스트를 둡니다.
         */
        template <typename T>
        static uint32 declaredMaxCount( const sw::string_view fieldName )
        {
            sw::string            reason;
            const sw::FixedCodec* pCodec = sw::FixedLayout::findCodec<T>( reason );
            if ( pCodec == nullptr || pCodec->_pCanonical == nullptr )
                return 0;
            const sw::string_view canonical( pCodec->_pCanonical );
            const sw::string      fieldKey = sw::string( "|F=" ) + sw::string( fieldName ) + ":";
            const size_t          begin    = canonical.find( fieldKey );
            if ( begin == sw::string_view::npos )
                return 0;
            const sw::string_view field    = canonical.substr( begin + 1, canonical.find( '|', begin + 1 ) - begin - 1 );
            const sw::string_view countKey = ":maxcount=";
            const size_t          at       = field.find( countKey );
            if ( at == sw::string_view::npos )
                return 0;
            uint32 value = 0;
            for ( size_t index = at + countKey.size(); index < field.size() && '0' <= field[index] && field[index] <= '9'; ++index )
            {
                value = value * 10 + static_cast<uint32>( field[index] - '0' );
            }
            return value;
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
