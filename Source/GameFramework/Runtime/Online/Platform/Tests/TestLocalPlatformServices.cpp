// LocalPlatformServicesTest — SDK 없는 플랫폼: 업적 · 통계가 다음 실행에 이어지고, 다른 이름으로 읽힐 글 · 폴더 밖 파일을 거절하며, 읽지 못한 기록을 덮어쓰지 않는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

using namespace sw;

/** @brief [LocalPlatformServicesTest] 푼 업적과 통계는 `update` 뒤 다음 실행이 이어 읽는다. 이미 푼 업적을 다시 풀어도 하나다 */
SW_TEST_CASE( LocalPlatformServicesTest, ProgressSurvivesARestart )
{
    const AbsolutePath folder = test::makeScratchPath( "local_platform" );
    {
        LocalPlatformServices platform( folder );
        SW_EXPECT_TRUE( platform.unlockAchievement( "first_blood" ) );
        SW_EXPECT_TRUE( platform.unlockAchievement( "first_blood" ) );
        SW_EXPECT_TRUE( platform.unlockAchievement( "long run" ) );
        SW_EXPECT_TRUE( platform.setStat( "kills", 12 ) );
        SW_EXPECT_TRUE( platform.setStat( "kills", -3 ) );
        platform.update();
    }
    const LocalPlatformServices restarted( folder );
    SW_REQUIRE_EQUAL( size_t{ 2 }, restarted.getUnlockedAchievements().size() );
    SW_EXPECT_EQUAL( string( "first_blood" ), restarted.getUnlockedAchievements()[0] );
    SW_EXPECT_EQUAL( string( "long run" ), restarted.getUnlockedAchievements()[1] );
    SW_EXPECT_EQUAL( int64{ -3 }, restarted.getStat( "kills" ) );
    SW_EXPECT_EQUAL( int64{ 0 }, restarted.getStat( "deaths" ) );
}

/** @brief [LocalPlatformServicesTest] 다음 실행이 다른 이름 · 값으로 읽을 글(빈 이름 · 줄바꿈 · `=` · 앞뒤 공백)은 거절한다 */
SW_TEST_CASE( LocalPlatformServicesTest, RefusesTextThatReadsBackDifferently )
{
    LocalPlatformServices platform( test::makeScratchPath( "local_platform" ) );
    SW_EXPECT_FALSE( platform.unlockAchievement( "" ) );
    SW_EXPECT_FALSE( platform.unlockAchievement( "a\nb" ) );
    SW_EXPECT_FALSE( platform.unlockAchievement( " padded" ) );
    SW_EXPECT_FALSE( platform.setStat( "kills=99", 1 ) );
    SW_EXPECT_FALSE( platform.setStat( "kills\r", 1 ) );
    SW_EXPECT_FALSE( platform.setRichPresence( "status", "in town\nstats=1" ) );
    SW_EXPECT_FALSE( platform.setRichPresence( "", "in town" ) );
    SW_EXPECT_TRUE( platform.setRichPresence( "status", "level = 3" ) ); // 값의 `=` 는 첫 `=` 뒤라 그대로 읽힌다
    SW_EXPECT_TRUE( platform.getUnlockedAchievements().empty() );
}

/** @brief [LocalPlatformServicesTest] 클라우드 파일은 왕복하고, 폴더 밖 · 드라이브 · 대체 스트림을 가리키는 이름은 읽기 · 쓰기 모두 거절한다 */
SW_TEST_CASE( LocalPlatformServicesTest, CloudFilesStayInTheFolder )
{
    LocalPlatformServices platform( test::makeScratchPath( "local_platform" ) );
    const vector<uint8>   bytes{ 1, 2, 3 };
    vector<uint8>         read{ 9 };
    SW_EXPECT_FALSE( platform.readCloudFile( "save.bin", read ) );
    SW_EXPECT_TRUE( read.empty() );
    SW_REQUIRE_TRUE( platform.writeCloudFile( "save.bin", bytes ) );
    SW_REQUIRE_TRUE( platform.readCloudFile( "save.bin", read ) );
    SW_EXPECT_TRUE( read == bytes );
    const utf8* arrOutside[] = { "", "../save.bin", "a/b", "a\\b", "C:save.bin", "save.bin:stream", ".." };
    for ( const utf8* pName : arrOutside )
    {
        SW_EXPECT_FALSE_MSG( platform.writeCloudFile( pName, bytes ), pName );
        SW_EXPECT_FALSE_MSG( platform.readCloudFile( pName, read ), pName );
    }
    vector<uint8> ticket{ 1 };
    SW_EXPECT_FALSE( platform.requestAuthTicket( "server", ticket ) );
    SW_EXPECT_TRUE( ticket.empty() );
}

/** @brief [LocalPlatformServicesTest] 숫자가 아닌 통계는 0 이고, 이름 없는 줄 · 겹친 업적 줄은 건너뛴다 */
SW_TEST_CASE( LocalPlatformServicesTest, ReadsDamagedLinesWithoutGuessing )
{
    const AbsolutePath folder = test::makeScratchPath( "local_platform" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( LocalPlatformServices::kStatsFileName ), "kills=abc\n=5\nnoequals\nwins=7\r\n" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( LocalPlatformServices::kAchievementsFileName ), "one\n\n one \r\ntwo" ) );
    const LocalPlatformServices platform( folder );
    SW_EXPECT_EQUAL( int64{ 0 }, platform.getStat( "kills" ) );
    SW_EXPECT_EQUAL( int64{ 7 }, platform.getStat( "wins" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, platform.getUnlockedAchievements().size() );
}
