/**
 * @file EphemeralStoreTestUtil.h
 * @brief 테스트가 값 형식을 거치지 않고 휘발 저장소의 바이트를 쓰고 읽는 길입니다(저장소 구현 · 깨진 값 · 옛 빌드의 바이트를 본다). 배포 코드에는 이 형식이 없다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"

#include <utility>

namespace sw
{
    struct EphemeralStoreTestAccess
    {
        static EphemeralValue   makeValue( vector<uint8> bytes ) { return EphemeralValue{ std::move( bytes ) }; }
        static EphemeralValue   makeTextValue( string_view text ) { return EphemeralValue{ vector<uint8>( text.begin(), text.end() ) }; }
        static EphemeralRequest makePublish( string_view channel, vector<uint8> bytes ) { return EphemeralRequest::makePublish( channel, std::move( bytes ) ); }
        /** @brief 바이트가 든 Get 답입니다(옛 빌드 · 깨진 값을 값 형식에 읽힌다). */
        static EphemeralReply makeReply( vector<uint8> bytes )
        {
            EphemeralReply reply;
            reply._value = std::move( bytes );
            return reply;
        }
        static const vector<uint8>& getBytes( const EphemeralReply& reply ) { return reply._value; }
        static const vector<uint8>& getBytes( const EphemeralMessage& message ) { return message._bytes; }
        static const vector<uint8>& getBytes( const EphemeralValue& value ) { return value._bytes; }
    };
} // namespace sw
