/**
 * @file PushPayloadUtil.h
 * @brief 푸시 제공자들이 알림(`PushNotificationMessage`)을 JSON 본문으로 쓸 때 같이 쓰는 도우미입니다(FCM · APNs).
 * @details 제품마다 본문 모양은 다르지만(`message.data` · 최상위 사용자 키) 인자 수 세기 · 빈 값 건너뛰기 · 글 배열 쓰기는 같다 — 제공자마다 따로 두면 규칙이 어긋나므로 한 자리에 둔다.
 *          제품 이름은 이 파일에 쓰지 않는다. 헤더만 있어 내보내는 기호가 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

namespace sw
{
    /** @struct PushPayloadUtil @brief 알림 본문을 JSON 으로 쓰는 공통 조각입니다. 상태가 없습니다. */
    struct PushPayloadUtil
    {
        /** @brief 글 바이트를 `vector<uint8>` 로 복사합니다(요청 본문). */
        static vector<uint8> toBytes( string_view text )
        {
            return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        }

        /** @brief 알림의 앞쪽에서 비지 않은 인자 수입니다(첫 빈 인자에서 멈춘다). */
        static int32 countArguments( const PushNotificationMessage& message )
        {
            int32 count = 0;
            while ( count < PushNotificationMessage::kArgumentCount && message._arrArgument[count].empty() == false )
            {
                ++count;
            }
            return count;
        }

        /** @brief 글이 비지 않았을 때만 멤버 하나를 씁니다. */
        static void writeOptionalText( JSONWriter& writer, string_view key, string_view text )
        {
            if ( text.empty() )
                return;
            writer.key( key );
            writer.valueString( text );
        }

        /** @brief 앞쪽 인자 @p count 개를 글 배열 멤버로 씁니다. 없으면 멤버를 쓰지 않습니다. */
        static void writeArguments( JSONWriter& writer, string_view key, const PushNotificationMessage& message, int32 count )
        {
            if ( count == 0 )
                return;
            writer.key( key );
            writer.beginArray();
            for ( int32 index = 0; index < count; ++index )
            {
                writer.valueString( message._arrArgument[index] );
            }
            writer.endArray();
        }
    };
} // namespace sw
