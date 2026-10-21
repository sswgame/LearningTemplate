#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/UUID/UUID.h"

#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Resource/Cook/CookRecord.h"
#include "Engine/Resource/Cook/CookStore.h"
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
            SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( folder ) );
            return sw::FileUtil::joinPath( folder, pFileName );
        }

        static void writeText( const sw::string& path, sw::string_view text )
        {
            SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( path, text ) );
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
            SW_EXPECT_TRUE( bRead );
            SW_EXPECT_TRUE( job.getInputs().size() == 1 );
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

/**
 * @brief [CookJobTest] 레지스트리 쿠킹은 도메인마다 기록을 남긴다 — `.meta` 를 고치거나 더하면 그 도메인만 다시 굽고, 쿠커 버전을 올리면 기록이 낡는다
 * @details 레지스트리는 폴더를 훑어 정해지므로 읽은 `.meta` 만으로는 새 `.meta` 를 알 수 없다 — `.meta` 목록이 입력(`CookInputKind::Listing`)이다.
 *          DDC 와 기록을 지우고 다시 구워도 같은 바이트다.
 */
SW_TEST_CASE( CookJobTest, RegistryCookFollowsTheMetaListing )
{
    const sw::string root      = test::makeTempDirectory( "registry_incremental_root" );
    const sw::string cooked    = test::makeTempDirectory( "registry_incremental_out" );
    const sw::string recordDir = test::makeTempDirectory( "registry_incremental_record" );
    const sw::string ddcDir    = test::makeTempDirectory( "registry_incremental_ddc" );
    const auto       writeMeta = []( const sw::string& path )
    {
        SW_EXPECT_TRUE( sw::FileUtil::ensureParentDirectoryExists( path ) );
        sw::AssetMeta meta;
        meta._guid       = sw::string( sw::UUID::generate().toString().c_str() );
        meta._sourcePath = path;
        TestCookJobInternal::writeText( path, meta.toXML() );
    };
    writeMeta( sw::FileUtil::joinPath( root, "engine/materials/a.material.meta" ) );
    writeMeta( sw::FileUtil::joinPath( root, "game/demo/prefabs/b.prefab.xml.meta" ) );

    const sw::CookStore store( recordDir, ddcDir );
    uint32              failedCount = 0;
    SW_EXPECT_EQUAL( 2u, sw::AssetDatabase::writeRegistryFiles( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 2u, store.getTally()._cookedCount );
    SW_EXPECT_EQUAL( 2u, sw::AssetDatabase::writeRegistryFiles( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 2u, store.getTally()._reusedCount );

    // 새 .meta 하나 — 읽은 파일은 그대로지만 목록이 바뀌어 그 도메인만 다시 굽는다.
    writeMeta( sw::FileUtil::joinPath( root, "game/demo/prefabs/c.prefab.xml.meta" ) );
    SW_EXPECT_EQUAL( 2u, sw::AssetDatabase::writeRegistryFiles( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 3u, store.getTally()._cookedCount );
    SW_EXPECT_EQUAL( 3u, store.getTally()._reusedCount );
    // 있던 .meta 를 고쳐도 그 도메인만 다시 굽는다.
    writeMeta( sw::FileUtil::joinPath( root, "engine/materials/a.material.meta" ) );
    SW_EXPECT_EQUAL( 2u, sw::AssetDatabase::writeRegistryFiles( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 4u, store.getTally()._cookedCount );
    SW_EXPECT_EQUAL( 4u, store.getTally()._reusedCount );
    SW_EXPECT_EQUAL( 0u, failedCount );

    sw::CookRecord record;
    SW_ASSERT_TRUE( sw::CookRecord::read( store.makeRecordPath( "game/demo/assetregistry.txt" ), record ) );
    sw::CookJobDesc desc = sw::AssetDatabase::makeCookJobDesc();
    SW_EXPECT_TRUE( record.isFresh( desc ) );
    desc._cookerVersion += 1;
    SW_EXPECT_FALSE( record.isFresh( desc ) );

    sw::vector<uint8> before;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( sw::FileUtil::joinPath( cooked, "game/demo/assetregistry.txt" ), before ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeDirectory( ddcDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeDirectory( recordDir ) );
    SW_EXPECT_EQUAL( 2u, sw::AssetDatabase::writeRegistryFiles( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 6u, store.getTally()._cookedCount );
    sw::vector<uint8> after;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( sw::FileUtil::joinPath( cooked, "game/demo/assetregistry.txt" ), after ) );
    SW_EXPECT_TRUE( before == after );
}
