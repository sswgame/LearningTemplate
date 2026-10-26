#include "pch.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespCodec.h"

// 퍼징 대상(키트 CacheStore 서버) — 캐시 서버가 돌려준 RESP2 바이트(`RespParser`). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsCacheStoreInternal
    {
        /** @brief 반으로 나눠 넣고 값이 나오는 동안 꺼냅니다(증분 경계 · 다시 읽기를 지난다). */
        static void runResp( const uint8* pData, size_t size )
        {
            if ( pData == nullptr || size == 0 )
                return;
            RespParser   parser;
            RespValue    value;
            const size_t half = size / 2;
            parser.append( pData, half );
            while ( parser.next( value ) == RespParseResult::Complete ) // 퍼즈 대상 — 오류도 정상이다
            {
            }
            parser.append( pData + half, size - half );
            while ( parser.next( value ) == RespParseResult::Complete )
            {
            }
        }

        static void appendTextSeed( string_view text, vector<vector<uint8>>& outListSeed )
        {
            outListSeed.emplace_back( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        }

        static void seedResp( vector<vector<uint8>>& outListSeed )
        {
            appendTextSeed( "+OK\r\n-ERR bad thing\r\n:-42\r\n$5\r\nhe\r\no\r\n$-1\r\n*-1\r\n*3\r\n*1\r\n:1\r\n$0\r\n\r\n+QUEUED\r\n*0\r\n", outListSeed );
            appendTextSeed( "*1048576\r\n+OK\r\n", outListSeed ); // 큰 개수 머리 — 바이트가 오기 전에 원소를 잡지 않는다
            vector<uint8> listCommand;
            RespCommand{ "ZADD" }.addText( "board" ).addInteger( 15 ).addText( "player" ).appendTo( listCommand );
            outListSeed.push_back( std::move( listCommand ) );
        }
    };

    const test::LoaderFuzzTargetRegistrar s_respFuzzTarget{
        test::LoaderFuzzTarget{ "Resp", &KitFuzzTargetsCacheStoreInternal::runResp, &KitFuzzTargetsCacheStoreInternal::seedResp, true }
    };
} // namespace
