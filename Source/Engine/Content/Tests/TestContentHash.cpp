#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/ContentHash.h"

/**
 * @file TestContentHash.cpp
 * @brief 쿠킹 판정이 쓰는 내용 해시(`ContentHash::computeBytes`)의 값과 영속 해시 캐시(`ContentHashCache::load` · `save`)를 굳히는 테스트입니다.
 * @details 해시는 원시 바이트의 FNV-1a 64 입니다. 값이 바뀌면 모든 쿠킹 기록(`.cookrec`)이 낡아 한 번에 다시 굽게 되므로 고정 값으로 단언한다.
 *          캐시 테스트는 서비스의 캐시가 아니라 따로 지은 캐시 둘(앞 프로세스 · 뒤 프로세스 자리)로 한다.
 */
namespace sw
{
    namespace
    {
        vector<uint8> makeBytes( const utf8* pText )
        {
            vector<uint8> bytes;
            for ( const utf8* pChar = pText; *pChar != '\0'; ++pChar )
            {
                bytes.push_back( static_cast<uint8>( *pChar ) );
            }
            return bytes;
        }

        /** @brief @p path 에 @p pText 를 쓰고 쓰기 시각을 한 시간 전으로 돌립니다(바깥 도구가 오래전에 쓴 파일 — 시간 창 밖이라 캐시된다). */
        void writeHourOldFile( const string& path, const utf8* pText )
        {
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), pText ) );
            const int64 hourAgo = FileUtil::getCurrentFileWriteTime() - 3600 * FileUtil::kFileTimeTicksPerSecond;
            SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( FileUtil::toDiskPath( path ), hourAgo ) );
        }

        AbsolutePath makeCachePath()
        {
            return AbsolutePath::tryMake( test::makeScratchPath( "ContentHashCache.bin" ) ).value();
        }
    } // namespace
} // namespace sw

using namespace sw;

/** @brief [ContentHashTest] 알려진 입력의 해시 값이 변하지 않았는가(원시 바이트 — CR 도 섞인다). */
SW_TEST_CASE( ContentHashTest, KnownInputsKeepTheirHash )
{
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "" ) ), static_cast<uint64>( 0xcbf29ce484222325ull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "abc" ) ), static_cast<uint64>( 0xe71fa2190541574bull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "line one\r\nline two\r\n\r\nend" ) ), static_cast<uint64>( 0xa3c494099a916911ull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "line one\nline two\n\nend" ) ), static_cast<uint64>( 0xd0db6e813abda55cull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "\r\r\r" ) ), static_cast<uint64>( 0xf33f44187abe6c26ull ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( makeBytes( "\xED\x95\x9C\xEA\xB8\x80 content\r\n" ) ), static_cast<uint64>( 0x761b05afe4890423ull ) );
}

/** @brief [ContentHashTest] 0x0D 의 자리만 다른 두 바이너리는 다른 해시다 — 예전에는 CR 을 건너뛰어 같은 해시였다(바뀐 입력을 쿠킹이 놓쳤다). */
SW_TEST_CASE( ContentHashTest, BinaryWithMovedCRHashesDifferently )
{
    const vector<uint8> first{ 0x01, 0x0D, 0x02, 0x03 };
    const vector<uint8> second{ 0x01, 0x02, 0x0D, 0x03 };
    SW_EXPECT_NOT_EQUAL( ContentHash::computeBytes( first ), ContentHash::computeBytes( second ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( first ), ContentHash::computeBytes( first.data(), first.size() ) );
}

/** @brief [ContentHashCacheTest] 앞 프로세스가 저장한 캐시를 실은 뒤 프로세스는 도장이 같은 파일을 한 바이트도 읽지 않는다. */
SW_TEST_CASE( ContentHashCacheTest, SecondProcessReadsNoBytes )
{
    const string       path      = test::makeScratchPath( "aged.txt" );
    const AbsolutePath cachePath = makeCachePath();
    writeHourOldFile( path, "aged content" );
    const uint64 expected = ContentHash::computeBytes( makeBytes( "aged content" ) );
    {
        ContentHashCache first;
        SW_REQUIRE_TRUE( first.load( cachePath ).has_value() ); // 파일이 아직 없다 — 빈 캐시로 성공
        SW_EXPECT_EQUAL( first.computeFile( path ), expected );
        SW_EXPECT_NOT_EQUAL( first.getHashedByteCount(), uint64( 0 ) );
        SW_REQUIRE_TRUE( first.save().has_value() );
    }
    ContentHashCache second;
    SW_REQUIRE_TRUE( second.load( cachePath ).has_value() );
    SW_EXPECT_EQUAL( second.computeFile( path ), expected );
    SW_EXPECT_EQUAL( second.getHashedByteCount(), uint64( 0 ) );
}

/**
 * @brief [ContentHashCacheTest] 저장 시각의 시간 창 안에 고쳐진 도장은 싣지 않는다 — 저장 뒤 같은 틱에 같은 크기로 다시 쓴 파일을 뒤 프로세스가 다시 해시한다.
 * @details 앞 캐시는 창 안의 도장을 남기지 않지만(자기 쓰기도), 저장 쪽도 같은 창으로 거른다 — 그 항목이 저장되면 같은 도장의 다른 내용을 뒤 프로세스가 옛 해시로 믿는다.
 */
SW_TEST_CASE( ContentHashCacheTest, StampInsideTheWriteWindowIsHashedAgain )
{
    const string       path      = test::makeScratchPath( "fresh.txt" );
    const AbsolutePath cachePath = makeCachePath();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), "aaaa" ) );
    int64 writeTicks = 0;
    SW_REQUIRE_TRUE( FileUtil::getFileWriteTime( FileUtil::toDiskPath( path ), writeTicks ) );
    {
        ContentHashCache first;
        SW_REQUIRE_TRUE( first.load( cachePath ).has_value() );
        first.recordOwnWrite( path );
        SW_EXPECT_EQUAL( first.computeFile( path ), ContentHash::computeBytes( makeBytes( "aaaa" ) ) );
        SW_REQUIRE_TRUE( first.save().has_value() );
    }
    // 바깥 도구가 같은 틱에 같은 크기로 다시 썼다 — 도장이 같다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), "bbbb" ) );
    SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( FileUtil::toDiskPath( path ), writeTicks ) );
    ContentHashCache second;
    SW_REQUIRE_TRUE( second.load( cachePath ).has_value() );
    SW_EXPECT_EQUAL( second.computeFile( path ), ContentHash::computeBytes( makeBytes( "bbbb" ) ) );
    SW_EXPECT_EQUAL( second.getHashedByteCount(), uint64( 4 ) );
}

/** @brief [ContentHashCacheTest] 자기 쓰기 뒤 같은 틱에 바깥 도구가 같은 크기로 다시 쓴 파일도 새 해시다 — 자기 쓰기도 시간 창이 지나야 캐시에 남는다. */
SW_TEST_CASE( ContentHashCacheTest, ExternalRewriteInTheOwnWriteTickIsSeen )
{
    const string path = test::makeScratchPath( "ownrace.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), "aaaa" ) );
    int64 writeTicks = 0;
    SW_REQUIRE_TRUE( FileUtil::getFileWriteTime( FileUtil::toDiskPath( path ), writeTicks ) );
    ContentHashCache cache;
    cache.recordOwnWrite( path );
    SW_EXPECT_EQUAL( cache.computeFile( path ), ContentHash::computeBytes( makeBytes( "aaaa" ) ) );
    // 바깥 도구가 같은 틱에 같은 크기로 다시 썼다 — 도장이 같고 이 캐시에는 알림이 없다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), "bbbb" ) );
    SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( FileUtil::toDiskPath( path ), writeTicks ) );
    SW_EXPECT_EQUAL( cache.computeFile( path ), ContentHash::computeBytes( makeBytes( "bbbb" ) ) );
}

/** @brief [ContentHashCacheTest] 다른 판의 캐시 파일은 빈 캐시로 읽는다 — 항목을 믿지 않고 다시 해시한다(오류가 아니다). */
SW_TEST_CASE( ContentHashCacheTest, CacheFromAnotherVersionIsDropped )
{
    const string       path      = test::makeScratchPath( "versioned.txt" );
    const AbsolutePath cachePath = makeCachePath();
    writeHourOldFile( path, "versioned" );
    {
        ContentHashCache first;
        SW_REQUIRE_TRUE( first.load( cachePath ).has_value() );
        SW_EXPECT_NOT_EQUAL( first.computeFile( path ), uint64( 0 ) );
        SW_REQUIRE_TRUE( first.save().has_value() );
    }
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( cachePath, bytes ) );
    SW_REQUIRE_TRUE( bytes.size() >= 8 );
    const uint32 otherVersion = ContentHashCache::kCacheFormatVersion + 1; // 머리: 표지(4 B) 뒤의 판(4 B, 리틀 엔디언)
    for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
    {
        bytes[4 + byteIndex] = static_cast<uint8>( otherVersion >> ( 8 * byteIndex ) );
    }
    SW_REQUIRE_TRUE( FileUtil::writeFile( cachePath, bytes.data(), bytes.size() ) );
    ContentHashCache second;
    SW_REQUIRE_TRUE( second.load( cachePath ).has_value() );
    SW_EXPECT_EQUAL( second.computeFile( path ), ContentHash::computeBytes( makeBytes( "versioned" ) ) );
    SW_EXPECT_EQUAL( second.getHashedByteCount(), uint64( 9 ) );
}

/**
 * @brief [ContentHashTest] 이 프로세스가 같은 크기로 곧바로 다시 쓴 파일도 새 해시다 — 쓰기 경로가 캐시에 알리므로 시간 창 · 캐시 비우기를 기다리지 않는다.
 * @details 같은 틱 안의 같은 크기 다시 쓰기는 (크기, 쓰기 시각) 도장이 같다. 예전에는 시간 창이 막 쓴 파일을 캐시하지 않아 가렸고, 그 전에는 옛 해시가 돌아왔다.
 *          지운 파일은 0(읽지 못함)이다.
 */
SW_TEST_CASE( ContentHashTest, OwnRewriteOfTheSameSizeIsSeenAtOnce )
{
    const string path = test::makeScratchPath( "rewrite.txt" );
    for ( const utf8* pText : { "aaaa", "bbbb", "cccc", "aaaa" } )
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), pText ) );
        const uint64 expected = ContentHash::computeBytes( makeBytes( pText ) );
        SW_EXPECT_EQUAL( ContentHash::computeFile( path ), expected );
        SW_EXPECT_EQUAL( ContentHash::computeFile( path ), expected ); // 두 번째는 캐시에서 — 같은 값이어야 한다
    }
    SW_REQUIRE_TRUE( FileUtil::removeFile( FileUtil::toDiskPath( path ) ) );
    // 지운 파일은 못 읽음(0)이다. 스트림 해시는 크기를 조용히 묻고(tryGetFileSize) 없음을 값(NotFound)으로 돌려주므로 로그는 없다 — 판단은 부른 쪽 몫이다.
    SW_EXPECT_EQUAL( ContentHash::computeFile( path ), uint64( 0 ) );
}
