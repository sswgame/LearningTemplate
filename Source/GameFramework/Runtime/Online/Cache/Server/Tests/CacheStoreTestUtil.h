/**
 * @file CacheStoreTestUtil.h
 * @brief 테스트가 값 형식을 거치지 않고 휘발 저장소의 바이트를 쓰고 읽는 길입니다(저장소 구현 · 깨진 값 · 옛 빌드의 바이트를 본다). 배포 코드에는 이 형식이 없다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Cache/Server/CacheStore.h"

#include <utility>

namespace sw
{
    struct CacheStoreTestAccess
    {
        static CacheValue   makeValue( vector<uint8> bytes ) { return CacheValue{ std::move( bytes ) }; }
        static CacheValue   makeTextValue( string_view text ) { return CacheValue{ vector<uint8>( text.begin(), text.end() ) }; }
        static CacheRequest makePublish( string_view channel, vector<uint8> bytes ) { return CacheRequest::makePublish( channel, std::move( bytes ) ); }
        /** @brief 바이트가 든 Get 답입니다(옛 빌드 · 깨진 값을 값 형식에 읽힌다). */
        static CacheReply makeReply( vector<uint8> bytes )
        {
            CacheReply reply;
            reply._value = std::move( bytes );
            return reply;
        }
        static const vector<uint8>& getBytes( const CacheReply& reply ) { return reply._value; }
        static const vector<uint8>& getBytes( const CacheMessage& message ) { return message._bytes; }
        static const vector<uint8>& getBytes( const CacheValue& value ) { return value._bytes; }
    };
} // namespace sw
