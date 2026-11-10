#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/Pack/PackConfig.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"

// AppearanceCookTest — 팩 쿠킹의 항목 단위 카탈로그 자리가 런타임(아이템 경로 규칙 · 외형 폴더의 파일 표)이 읽는 자리와 같은지 본다(nogpu).

/**
 * @brief [AppearanceCookTest] 쿠킹의 항목 단위 카탈로그 자리(`_listEntryCatalog`)는 런타임이 읽는 자리다 — 어긋나면 고르기가 그 파일을 카탈로그로 보지 못해 항목을 가리지 못한다
 * @details 런타임의 자리는 둘이다: 종류의 경로 규칙(아이템)과 외형 폴더의 파일 표(`AppearanceDatabase::isDataFile`). 쿠킹은 게임 모듈을 올리기 전에 돌아 둘을 보지 못해 데이터로 적는다.
 */
SW_TEST_CASE( AppearanceCookTest, CookEntryCatalogsMatchTheRuntimeFiles )
{
    sw::PackConfig   config;
    const sw::string path = sw::FileUtil::joinPath( sw::ProcessRoots::get()._project.str(), sw::PackConfig::kConfigPath );
    SW_REQUIRE_TRUE( sw::ConfigManager::readConfigFile( config, path ) == sw::ConfigReadResult::Loaded );
    SW_REQUIRE_TRUE( config._cookRules._listEntryCatalog.empty() == false );

    sw::string itemPath;
    sw::string appearanceFolder;
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "items", itemPath ) );
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "appearance", appearanceFolder ) );
    for ( const sw::PackEntryCatalog& entry : config._cookRules._listEntryCatalog )
    {
        const sw::string      entryPath = sw::FileUtil::joinPath( "common", entry._path );
        const sw::string      prefix    = appearanceFolder + "/";
        const bool            bItems    = entryPath == itemPath;
        const bool            bFolder   = sw::StringUtil::startsWith( entryPath, prefix );
        const sw::string_view file      = bFolder ? sw::string_view( entryPath ).substr( prefix.size() ) : sw::string_view{};
        SW_EXPECT_TRUE_MSG( bItems || ( bFolder && sw::AppearanceDatabase::isDataFile( file ) ), "_listEntryCatalog row '" + entry._kind + "' (" + entry._path + ") is no file the runtime reads" );
    }
}
