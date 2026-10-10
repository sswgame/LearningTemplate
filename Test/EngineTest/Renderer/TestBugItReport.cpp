#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Renderer/Capture/BugItReport.h"

#include "TestFramework/TestFramework.h"

// BugItReport — bugit 의 info.txt 읽고 쓰기 · 폴더 이름 · 시선 각도(렌더러 · 씬 없이).

/**
 * @brief [BugItReportTest] info.txt 를 쓰고 읽으면 같은 값이다(한글 메모 · 공백 · 소수 카메라 값)
 */
SW_TEST_CASE( BugItReportTest, InfoRoundTrips )
{
    const sw::string directory = test::makeTempDirectory( "bugit_roundtrip" );
    sw::BugItReport  report;
    report._note                  = "점프 뒤 벽에 끼임 - jump then stuck";
    report._scenePath             = "game/empty/maps/editortest.scene.xml";
    report._rhiBackend            = "Direct3D 12";
    report._buildConfiguration    = "Debug";
    report._game                  = "SW Empty";
    report._cameraPosition        = sw::float3{ 1.25f, -3.5f, 10.0625f };
    report._cameraRotationDegrees = sw::float3{ 20.5f, -135.25f, 0.0f };
    SW_ASSERT_TRUE( report.writeInfo( directory ) );

    sw::BugItReport read;
    SW_ASSERT_TRUE( sw::BugItReport::readInfo( directory, read ) );
    SW_EXPECT_TRUE( read._note == report._note );
    SW_EXPECT_TRUE( read._scenePath == report._scenePath );
    SW_EXPECT_TRUE( read._rhiBackend == report._rhiBackend );
    SW_EXPECT_TRUE( read._buildConfiguration == report._buildConfiguration );
    SW_EXPECT_TRUE( read._game == report._game );
    SW_EXPECT_NEAR_EQUAL( report._cameraPosition._x, read._cameraPosition._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( report._cameraPosition._y, read._cameraPosition._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( report._cameraPosition._z, read._cameraPosition._z, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( report._cameraRotationDegrees._y, read._cameraRotationDegrees._y, 0.0001f );
}

/**
 * @brief [BugItReportTest] 모르는 키는 건너뛰고 아는 키는 읽는다(필드가 늘어도 옛 폴더를 읽는다)
 */
SW_TEST_CASE( BugItReportTest, UnknownKeysAreSkipped )
{
    const sw::string directory = test::makeTempDirectory( "bugit_unknown" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( directory, sw::BugItReport::kInfoFileName ),
                                                 "future_key = 42\nnote = hello\nno separator line\ncamera_position = 1 2 3\n" ) );
    sw::BugItReport read;
    SW_ASSERT_TRUE( sw::BugItReport::readInfo( directory, read ) );
    SW_EXPECT_TRUE( read._note == "hello" );
    SW_EXPECT_NEAR_EQUAL( 3.0f, read._cameraPosition._z, 0.0001f );
    SW_EXPECT_TRUE( sw::BugItReport::readInfo( sw::FileUtil::joinPath( directory, "missing" ), read ) == false );
}

/**
 * @brief [BugItReportTest] 같은 초에 두 번 만들면 둘째 폴더는 -2 이고, 가장 최근 폴더는 이름 순 마지막이다
 */
SW_TEST_CASE( BugItReportTest, SameSecondGetsSuffixAndLatestIsFound )
{
    const sw::string        root = test::makeTempDirectory( "bugit_folders" );
    sw::ScreenshotLocalTime localTime{ 2026, 10, 6, 10, 15, 30 };
    const sw::string        first  = sw::BugItReport::makeUniqueDirectory( root, localTime );
    const sw::string        second = sw::BugItReport::makeUniqueDirectory( root, localTime );
    SW_ASSERT_TRUE( first.empty() == false && second.empty() == false );
    SW_EXPECT_TRUE( sw::FileUtil::isDirectory( first ) && sw::FileUtil::isDirectory( second ) );
    SW_EXPECT_TRUE( sw::FileUtil::getFileNamePart( first ) == "20261006-101530" );
    SW_EXPECT_TRUE( sw::FileUtil::getFileNamePart( second ) == "20261006-101530-2" );
    SW_EXPECT_TRUE( sw::FileUtil::getFileNamePart( sw::BugItReport::findLatestDirectory( root ) ) == "20261006-101530-2" );
}

/**
 * @brief [BugItReportTest] 시선 → pitch · yaw → 시선이 제자리로 돌아온다(에디터 카메라와 같은 규칙)
 */
SW_TEST_CASE( BugItReportTest, RotationDegreesRoundTripTheForward )
{
    const sw::float3 forward  = sw::float3{ 0.3f, -0.4f, -0.866f }.normalize();
    const sw::float3 rotation = sw::BugItReport::computeRotationDegrees( forward );
    const sw::float3 back     = sw::BugItReport::computeForward( rotation );
    SW_EXPECT_NEAR_EQUAL( forward._x, back._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( forward._y, back._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( forward._z, back._z, 0.0001f );
    SW_EXPECT_TRUE( sw::StringUtil::equals( sw::BugItReport::findBackendFlag( "Vulkan 1.3" ), "vk" ) );
    SW_EXPECT_TRUE( sw::StringUtil::equals( sw::BugItReport::findBackendFlag( "Direct3D 11" ), "dx11" ) );
}
