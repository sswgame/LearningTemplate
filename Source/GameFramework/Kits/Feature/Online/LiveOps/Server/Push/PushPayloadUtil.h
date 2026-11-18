/**
 * @file PushPayloadUtil.h
 * @brief 푸시 제공자들이 알림(`PushNotificationMessage`)을 JSON 본문으로 쓸 때 같이 쓰는 도우미입니다(FCM · APNs).
 * @details 제품마다 본문 모양은 다르지만(`message.data` · 최상위 사용자 키) 인자 수 세기 · 빈 값 건너뛰기 · 글 배열 쓰기, 앱이 읽는 사용자 키(`deep_link` · `title_key` · `body_key` · `arg<N>`),
 *          iOS 알림 사전의 보이는 알림 멤버(`alert` · `badge`)는 같다 — 제공자마다 따로 두면 규칙이 어긋나므로 한 자리에 둔다.
 *          제품 이름은 이 파일에 쓰지 않는다. 헤더만 있어 내보내는 기호가 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

namespace sw
{
    /** @struct PushPayloadUtil @brief 알림 본문을 JSON 으로 쓰는 공통 조각입니다. 상태가 없습니다. */
    struct PushPayloadUtil
    {
        /** @brief 문구(제목 · 본문 키)가 있어 보이는 알림이면 true 입니다. 없으면 조용한 알림(데이터만)입니다. */
        static bool hasAlert( const PushNotificationMessage& message ) { return message._titleKey.empty() == false || message._bodyKey.empty() == false; }

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

        /** @brief 앱이 읽는 사용자 키(`deep_link` · `title_key` · `body_key` · `arg0..`)를 지금 열린 객체에 씁니다. 빈 값은 쓰지 않습니다. */
        static void writeAppData( JSONWriter& writer, const PushNotificationMessage& message, int32 argumentCount )
        {
            writeOptionalText( writer, "deep_link", message._deepLink );
            writeOptionalText( writer, "title_key", message._titleKey );
            writeOptionalText( writer, "body_key", message._bodyKey );
            for ( int32 index = 0; index < argumentCount; ++index )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "arg%#", index );
                writer.key( textBuilder.view() );
                writer.valueString( message._arrArgument[index] );
            }
        }

        /** @brief iOS 알림 사전(`aps`)의 보이는 알림 멤버(`alert` 의 현지화 키 · 인자, 0 이상이면 `badge`)를 지금 열린 객체에 씁니다. iOS 로 바로 보내는 제공자와 중계하는 제공자가 같은 모양을 씁니다. */
        static void writeAlert( JSONWriter& writer, const PushNotificationMessage& message, int32 argumentCount )
        {
            writer.key( "alert" );
            writer.beginObject();
            writeOptionalText( writer, "title-loc-key", message._titleKey );
            writeOptionalText( writer, "loc-key", message._bodyKey );
            writeArguments( writer, "loc-args", message, argumentCount );
            writer.endObject();
            if ( message._badgeCount >= 0 )
            {
                writer.key( "badge" );
                writer.valueInt( message._badgeCount );
            }
        }
    };
} // namespace sw
