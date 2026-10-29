#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/ContentHash.h"

/**
 * @file TestContentHash.cpp
 * @brief 쿠킹 판정이 쓰는 내용 해시(`ContentHash::computeBytes`)의 값을 굳히는 테스트입니다.
 * @details 해시는 CR 을 뺀 바이트의 FNV-1a 64 입니다. 구현은 CR 을 뺀 사본을 만들지 않고 한 번에 훑는데, 값이 바뀌면 모든 쿠킹 기록(`.cookrec`)이 낡아 한 번에 다시 굽게 되므로
 *          값이 옛 구현(사본을 `computeStringHash64( ..., bIgnoreCase = false )` 로 섞음)과 같다는 것을 고정 값과 옛 구현 사본으로 단언한다.
 */
namespace sw
{
    namespace
    {
        /** @brief 옛 구현입니다 — CR 을 뺀 사본을 만들어 문자열 해시로 섞습니다. 새 구현과 값이 같아야 합니다. */
        uint64 computeReferenceHash( const vector<uint8>& bytes )
        {
            vector<uint8> listNormalized;
            for ( const uint8 byte : bytes )
            {
                if ( byte != static_cast<uint8>( '\r' ) )
                    listNormalized.push_back( byte );
            }
            return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( listNormalized.data() ), listNormalized.size(), false );
        }

        vector<uint8> makeBytes( const utf8* pText )
        {
            vector<uint8> bytes;
            for ( const utf8* pChar = pText; *pChar != '\0'; ++pChar )
            {
                bytes.push_back( static_cast<uint8>( *pChar ) );
            }
            return bytes;
        }
    } // namespace
} // namespace sw

using namespace sw;

/** @brief [ContentHashTest] 알려진 입력의 해시 값이 변하지 않았는가(CR 은 해시에서 빠진다). */
SW_TEST_CASE( ContentHashTest, KnownInputsKeepTheirHash )
{
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "" ) ), static_cast<uint64>( 0xcbf29ce484222325ull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "abc" ) ), static_cast<uint64>( 0xe71fa2190541574bull ) );
    // CRLF 와 LF 는 같은 해시이고, 값은 CR 을 뺀 "line one\nline two\n\nend" 의 FNV-1a 64 다.
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "line one\r\nline two\r\n\r\nend" ) ), static_cast<uint64>( 0xd0db6e813abda55cull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "line one\nline two\n\nend" ) ), static_cast<uint64>( 0xd0db6e813abda55cull ) );
    // CR 만 있으면 빈 입력과 같다.
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "\r\r\r" ) ), static_cast<uint64>( 0xcbf29ce484222325ull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "\xED\x95\x9C\xEA\xB8\x80 content\r\n" ) ), static_cast<uint64>( 0x3fe7bde973df645aull ) );
}

/** @brief [ContentHashTest] 큰 바이트열(CR 이 섞인 난수)에서 옛 구현과 같은 값을 내는가 — 포인터 · 벡터 두 진입점 모두. */
SW_TEST_CASE( ContentHashTest, MatchesTheCopyingReferenceOnLargeBuffers )
{
    uint64        state = 0x9E3779B97F4A7C15ull;
    vector<uint8> bytes( 3 * constant::kBytesPerMegabyte + 5 );
    for ( size_t index = 0; index < bytes.size(); ++index )
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        bytes[index] = ( index % 7 == 0 ) ? static_cast<uint8>( '\r' ) : static_cast<uint8>( state );
    }
    const uint64 reference = computeReferenceHash( bytes );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( bytes ), reference );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( bytes.data(), bytes.size() ), reference );
}
