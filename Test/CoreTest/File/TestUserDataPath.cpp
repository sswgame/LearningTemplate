#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"

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
