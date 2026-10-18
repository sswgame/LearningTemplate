#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Resource/Cook/CookRecord.h"
#include "Engine/Resource/ResourceIO.h"

#include "TestFramework/TestFramework.h"

// 쿠킹 작업 범위(`CookJob`)가 리소스 읽기를 입력으로 기록하고, 키 · 기록(`.cookrec`) · DDC 가 그 입력으로 판정하는지 본다.

namespace
{
    struct TestCookJobInternal
    {
        /** @brief 이 테스트 전용 임시 폴더의 파일 경로입니다. */
        static sw::string makeTempPath( const utf8* pFileName )
        {
            const sw::string folder = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_cookjob_test" );
            SW_ASSERT( sw::FileUtil::ensureDirectoryExists( folder ) );
            return sw::FileUtil::joinPath( folder, pFileName );
        }

        static void writeText( const sw::string& path, sw::string_view text )
        {
            SW_ASSERT( sw::FileUtil::writeTextFile( path, text ) );
            sw::ContentHash::clearCache(); // 같은 시각에 다시 쓴 파일도 새로 해시하게
        }

        static sw::CookJobDesc makeDesc( uint32 version )
        {
            sw::CookJobDesc desc;
            desc._cookerName    = "TestCooker";
            desc._cookerVersion = version;
            desc._target        = "Windows";
            desc._settingsHash  = 0;
            return desc;
        }

        /** @brief 작업 범위 안에서 @p path 를 읽고 그 작업의 키를 돌려줍니다. */
        static uint64 cookOnce( const sw::string& path, uint32 version )
        {
            sw::CookJob           job( makeDesc( version ) );
            sw::vector<uint8>     bytes;
            [[maybe_unused]] bool bRead = sw::ResourceIO::readBinaryResource( path, bytes );
            SW_ASSERT( bRead );
            SW_ASSERT( job.getInputs().size() == 1 );
            return job.computeKey();
        }
    };
} // namespace

/**
 * @brief [CookJobTest] 범위 안의 읽기가 입력으로 기록되고, 같은 입력은 같은 키, 입력 내용이나 쿠커 버전이 바뀌면 다른 키다
 */
SW_TEST_CASE( CookJobTest, KeyFollowsRecordedInputsAndCookerVersion )
{
    const sw::string input = TestCookJobInternal::makeTempPath( "input.txt" );
    TestCookJobInternal::writeText( input, "first" );
    const uint64 firstKey = TestCookJobInternal::cookOnce( input, 1 );
    SW_EXPECT_EQUAL( firstKey, TestCookJobInternal::cookOnce( input, 1 ) );
    SW_EXPECT_NOT_EQUAL( firstKey, TestCookJobInternal::cookOnce( input, 2 ) );

    TestCookJobInternal::writeText( input, "second, longer" );
    SW_EXPECT_NOT_EQUAL( firstKey, TestCookJobInternal::cookOnce( input, 1 ) );

    // 안쪽 작업의 입력은 바깥 작업에도 기록된다(씬이 프리팹을 읽고 프리팹이 머티리얼을 읽는다).
    sw::CookJob outer( TestCookJobInternal::makeDesc( 1 ) );
    {
        sw::CookJob       inner( TestCookJobInternal::makeDesc( 1 ) );
        sw::vector<uint8> bytes;
        SW_ASSERT_TRUE( sw::ResourceIO::readBinaryResource( input, bytes ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), outer.getInputs().size() );
}

/**
 * @brief [CookJobTest] 쿠킹 세션 안에서 작업 범위 밖의 읽기는 기록이 빠지므로 실패한다 — 세션 밖에서는 그대로 읽는다
 */
SW_TEST_CASE( CookJobTest, ReadOutsideAJobDuringCookingFails )
{
    const sw::string input = TestCookJobInternal::makeTempPath( "outside.txt" );
    TestCookJobInternal::writeText( input, "outside" );
    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( sw::ResourceIO::readBinaryResource( input, bytes ) );
    {
        sw::CookSession session;
        SW_TEST_DEFENSIVE_SCOPE( "a resource read outside a cook job during cooking" );
        SW_EXPECT_FALSE( sw::ResourceIO::readBinaryResource( input, bytes ) );
        sw::string text;
        SW_EXPECT_FALSE( sw::ResourceIO::readTextResource( input, text ) );
    }
    SW_EXPECT_TRUE( sw::ResourceIO::readBinaryResource( input, bytes ) );
}

/**
 * @brief [CookJobTest] 기록(`.cookrec`)은 글로 왕복하고, 입력 내용 · 쿠커 버전이 그대로일 때만 새것이다. DDC 는 키로 출력을 찾는다
 */
SW_TEST_CASE( CookJobTest, RecordIsFreshOnlyWhileInputsAndVersionHold )
{
    const sw::string input = TestCookJobInternal::makeTempPath( "recorded.txt" );
    TestCookJobInternal::writeText( input, "recorded" );
    const sw::vector<uint8> output{ 1, 2, 3 };

    sw::CookRecord record;
    {
        sw::CookJob       job( TestCookJobInternal::makeDesc( 1 ) );
        sw::vector<uint8> bytes;
        SW_ASSERT_TRUE( sw::ResourceIO::readBinaryResource( input, bytes ) );
        record = sw::CookRecord::make( job, output );
    }
    const sw::string recordPath = TestCookJobInternal::makeTempPath( "output.cookrec" );
    SW_ASSERT_TRUE( record.write( recordPath ) );
    sw::CookRecord readBack;
    SW_ASSERT_TRUE( sw::CookRecord::read( recordPath, readBack ) );
    SW_EXPECT_EQUAL( record._key, readBack._key );
    SW_EXPECT_EQUAL( record._outputHash, readBack._outputHash );
    SW_ASSERT_EQUAL( size_t( 1 ), readBack._listInput.size() );
    SW_EXPECT_TRUE( readBack._listInput[0] == record._listInput[0] );

    SW_EXPECT_TRUE( readBack.isFresh( TestCookJobInternal::makeDesc( 1 ) ) );
    SW_EXPECT_FALSE( readBack.isFresh( TestCookJobInternal::makeDesc( 2 ) ) ); // 쿠커 버전을 올리면 다시 굽는다
    TestCookJobInternal::writeText( input, "recorded, then edited" );
    SW_EXPECT_FALSE( readBack.isFresh( TestCookJobInternal::makeDesc( 1 ) ) );

    // 머리 줄이 다른(옛 형식) 기록은 읽지 않는다.
    TestCookJobInternal::writeText( recordPath, "SWCOOKREC 0\nkey 0000000000000001\noutput 0000000000000002\n" );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a cook record of another format" );
        SW_EXPECT_FALSE( sw::CookRecord::read( recordPath, readBack ) );
    }

    const sw::DerivedDataCache cache( TestCookJobInternal::makeTempPath( "ddc" ) );
    SW_ASSERT_TRUE( cache.store( record._key, output ) );
    sw::vector<uint8> found;
    SW_ASSERT_TRUE( cache.find( record._key, found ) );
    SW_EXPECT_TRUE( found == output );
    SW_EXPECT_FALSE( cache.find( record._key + 1, found ) );
}
