#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/Process/ProcessEnvironment.h"

#include "TestFramework/TestFramework.h"

// 사용자 데이터 경로 — 게임 이름은 소문자 폴더, 상대 경로는 데이터 폴더 아래, 절대 경로 · `:memory:` 는 그대로.

using namespace sw;

SW_TEST_CASE( UserDataPathTest, GameFolderIsLowercaseAndRelativePathsResolveUnderData )
{
    const string dataDirectory   = UserDataPath::getDataDirectory( "MeadowVillage" );
    const string configDirectory = UserDataPath::getConfigDirectory( "MeadowVillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( dataDirectory ) == "meadowvillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( configDirectory ) == "meadowvillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( UserDataPath::getDataDirectory( "" ) ) == "default" );

    SW_EXPECT_TRUE( UserDataPath::resolve( "MeadowVillage", "saves" ) == FileUtil::joinPath( dataDirectory, "saves" ) );
    SW_EXPECT_TRUE( UserDataPath::resolve( "MeadowVillage", ":memory:" ) == ":memory:" );
    const string absolutePath = FileUtil::joinPath( FileUtil::getCurrentPath(), "elsewhere/saves" );
    SW_EXPECT_TRUE( UserDataPath::resolve( "MeadowVillage", absolutePath ) == absolutePath );
}

/**
 * @brief [UserDataPathTest] 환경 변수 `SW_USER_DATA_ROOT` 가 있으면 데이터 · 설정 폴더가 모두 그 아래 `<game>` 으로 옮겨가고, 지우면 OS 규칙으로 돌아온다
 * @details 변이: 덮어쓰기를 설정 폴더에만 걸면 데이터 단언이 지고, 지운 뒤에도 남기면 마지막 단언이 진다.
 */
SW_TEST_CASE( UserDataPathTest, RootOverrideVariableMovesDataAndConfigTogether )
{
    string     previous;
    const bool bHadValue = ProcessEnvironment::tryRead( kUserDataRootVariable, previous );
    SW_ASSERT_TRUE( ProcessEnvironment::tryUnset( kUserDataRootVariable ) );
    const string osRuleDirectory = UserDataPath::getConfigDirectory( "MeadowVillage" );

    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kUserDataRootVariable, "sw_override_root" ) );
    const string expected = FileUtil::joinPath( "sw_override_root", "meadowvillage" );
    SW_EXPECT_TRUE( UserDataPath::getDataDirectory( "MeadowVillage" ) == expected );
    SW_EXPECT_TRUE( UserDataPath::getConfigDirectory( "MeadowVillage" ) == expected );

    SW_ASSERT_TRUE( ProcessEnvironment::tryUnset( kUserDataRootVariable ) );
    SW_EXPECT_TRUE( UserDataPath::getConfigDirectory( "MeadowVillage" ) == osRuleDirectory );
    if ( bHadValue )
        SW_EXPECT_TRUE( ProcessEnvironment::trySet( kUserDataRootVariable, previous ) );
}
