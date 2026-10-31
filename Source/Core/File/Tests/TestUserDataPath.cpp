#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Testing/TestFramework.h"

// 사용자 데이터 경로 — 게임 이름은 소문자 폴더. 로컬 저장 자리(`LocalStoreLocation`)는 종류를 만드는 쪽이 고르고, 데이터 폴더 아래 자리는 상대 경로만 받는다.

using namespace sw;

SW_TEST_CASE( UserDataPathTest, GameFolderIsLowercaseAndRelativePathsResolveUnderData )
{
    const string dataDirectory   = UserDataPath::getDataDirectory( "MeadowVillage" );
    const string configDirectory = UserDataPath::getConfigDirectory( "MeadowVillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( dataDirectory ) == "meadowvillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( configDirectory ) == "meadowvillage" );
    SW_EXPECT_TRUE( FileUtil::getFileNamePart( UserDataPath::getDataDirectory( "" ) ) == "default" );
}

/**
 * @brief [UserDataPathTest] 데이터 폴더 아래 자리는 상대 경로만 받고(절대 꼴 · 드라이브 상대 · `..` · 빈 글은 거절), 절대 자리는 그대로, 메모리 자리는 디스크가 없다
 * @details 변이: 거절 검사를 `AbsolutePath::isStrictAbsolute` 로 바꾸면 `C:foo` 가 통과해 진다. `..` 마디 검사를 지우면 `../escape` 가 통과해 진다.
 */
SW_TEST_CASE( UserDataPathTest, LocalStoreLocationKeepsTheChosenKind )
{
    const std::optional<LocalStoreLocation> saves = LocalStoreLocation::tryUnderUserData( "saves" );
    SW_REQUIRE_TRUE( saves.has_value() );
    SW_EXPECT_TRUE( saves->getKind() == LocalStoreLocation::Kind::UnderUserData );
    SW_EXPECT_TRUE( saves->resolveDiskPath( "MeadowVillage" ) == FileUtil::joinPath( UserDataPath::getDataDirectory( "MeadowVillage" ), "saves" ) );

    SW_EXPECT_FALSE( LocalStoreLocation::tryUnderUserData( "" ).has_value() );
    SW_EXPECT_FALSE( LocalStoreLocation::tryUnderUserData( "C:foo" ).has_value() );
    SW_EXPECT_FALSE( LocalStoreLocation::tryUnderUserData( "../escape" ).has_value() );
    SW_EXPECT_FALSE( LocalStoreLocation::tryUnderUserData( "saves/../../escape" ).has_value() );
    SW_EXPECT_FALSE( LocalStoreLocation::tryUnderUserData( FileUtil::getCurrentPath() ).has_value() );
    SW_EXPECT_TRUE( LocalStoreLocation::tryUnderUserData( "saves/..hidden" ).has_value() ); // `..` 로 시작할 뿐인 이름은 마디 `..` 가 아니다

    const std::optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( FileUtil::joinPath( FileUtil::getCurrentPath(), "elsewhere/saves" ) );
    SW_REQUIRE_TRUE( absolutePath.has_value() );
    const LocalStoreLocation absolute = LocalStoreLocation::absolute( *absolutePath );
    SW_EXPECT_TRUE( absolute.getKind() == LocalStoreLocation::Kind::Absolute );
    SW_EXPECT_TRUE( absolute.resolveDiskPath( "MeadowVillage" ) == absolutePath->str() );
    SW_EXPECT_TRUE( LocalStoreLocation::memory().getKind() == LocalStoreLocation::Kind::Memory );
}

/**
 * @brief [UserDataPathTest] 환경 변수 `SW_USER_DATA_ROOT` 가 있으면 데이터 · 설정 폴더가 모두 그 아래 `<game>` 으로 옮겨가고, 지우면 OS 규칙으로 돌아온다
 * @details 변이: 덮어쓰기를 설정 폴더에만 걸면 데이터 단언이 지고, 지운 뒤에도 남기면 마지막 단언이 진다.
 */
SW_TEST_CASE( UserDataPathTest, RootOverrideVariableMovesDataAndConfigTogether )
{
    string     previous;
    const bool bHadValue = ProcessEnvironment::tryRead( kUserDataRootVariable, previous );
    SW_REQUIRE_TRUE( ProcessEnvironment::tryUnset( kUserDataRootVariable ) );
    const string osRuleDirectory = UserDataPath::getConfigDirectory( "MeadowVillage" );

    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kUserDataRootVariable, "sw_override_root" ) );
    const string expected = FileUtil::joinPath( "sw_override_root", "meadowvillage" );
    SW_EXPECT_TRUE( UserDataPath::getDataDirectory( "MeadowVillage" ) == expected );
    SW_EXPECT_TRUE( UserDataPath::getConfigDirectory( "MeadowVillage" ) == expected );

    SW_REQUIRE_TRUE( ProcessEnvironment::tryUnset( kUserDataRootVariable ) );
    SW_EXPECT_TRUE( UserDataPath::getConfigDirectory( "MeadowVillage" ) == osRuleDirectory );
    if ( bHadValue )
        SW_EXPECT_TRUE( ProcessEnvironment::trySet( kUserDataRootVariable, previous ) );
}
