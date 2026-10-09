#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct ConfigFileSchemaInternal
    {
        /** @brief 설정 파일 한 종류 — 경로 앞부분(또는 전체)과 실제 로더입니다. 로더가 nullptr 이면 다른 검사가 맡는 파일입니다(그 이름을 적는다). */
        struct ConfigKind
        {
            const utf8* _pPathPrefix;
            const utf8* _pCheckedBy;
            bool ( *_pLoad )( const sw::string& absolutePath );
        };

        /** @brief 엄격하게 읽고, 기본값과 같은 값을 다시 적은 키가 없는지도 봅니다(설정 파일에는 기본값과 다른 값만 적는다). */
        template <typename T>
        [[nodiscard]] static bool loadStrict( const sw::string& absolutePath )
        {
            T config{};
            if ( sw::ConfigManager::readConfigFile( config, absolutePath ) != sw::ConfigReadResult::Loaded )
                return false;
            sw::string             text;
            sw::vector<sw::string> listEchoKey;
            if ( sw::FileUtil::readTextFile( absolutePath, text ) == false || sw::ConfigManager::collectDefaultEchoKeys<T>( text, listEchoKey ) == false )
                return false;
            for ( const sw::string& key : listEchoKey )
            {
                SW_EXPECT_TRUE_MSG( false, absolutePath + ": '" + key + "' restates the default - write only values that differ (docs/Config lists the defaults)" );
            }
            return true;
        }

        [[nodiscard]] static bool loadMemoryBudget( const sw::string& absolutePath )
        {
            sw::string text;
            if ( sw::FileUtil::readTextFile( absolutePath, text ) == false )
                return false;
            sw::MemoryProfiler profiler;
            profiler.initialize();
            sw::string error;
            const bool bApplied = sw::MemoryBudgetMonitor::applyBudgetJson( text, profiler, error );
            profiler.shutdown();
            SW_EXPECT_TRUE_MSG( bApplied, error.c_str() );
            return bApplied;
        }

        /** @brief 앞의 줄이 먼저 맞습니다. `Config/Editor/` 는 EditorTest(`EditorConfigFileSchemaTest`)가 본다. 짝은 `Scripts/common/ConfigCatalog.py` 다. */
        static constexpr ConfigKind kArrKind[] = {
            {    "Config/Engine/EngineConfig.json",                                          "", &loadStrict<sw::EngineConfig>},
            {    "Config/Engine/MemoryBudget.json",                                          "",             &loadMemoryBudget},
            {                       "Config/Game/",                                          "",   &loadStrict<sw::GameConfig>},
            {"Config/Server/chat_banned_words.txt",  "ChatWordFilterTest (word list, not keys)",                       nullptr},
            {                     "Config/Server/",                                          "", &loadStrict<sw::ServerConfig>},
            {    "Config/Engine/CookContract.json",                "CheckCookContract (python)",                       nullptr},
            {      "Config/Engine/PackFormat.json",            "GeneratePackFormat (configure)",                       nullptr},
            {      "Config/Engine/PackConfig.json",                       "CookAssets (python)",                       nullptr},
            {                "Config/Environment/", "SetupEnvironment (python) - machine-local",                       nullptr},
            {                     "Config/Editor/",   "EditorConfigFileSchemaTest (EditorTest)",                       nullptr},
            {                   "Config/README.md",                              "not a config",                       nullptr},
        };

        static const ConfigKind* findKind( sw::string_view relativePath )
        {
            for ( const ConfigKind& kind : kArrKind )
            {
                const sw::string_view prefix( kind._pPathPrefix );
                if ( sw::StringUtil::startsWith( relativePath, prefix ) )
                    return &kind;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [ConfigFileSchemaTest] Config/ 의 설정 파일은 모두 실제 로더로 엄격하게 읽힌다 — 모르는 키 · 읽지 못한 값 · 범위 밖 값 · 기본값을 다시 적은 키가 하나도 없다
 * @details 기동은 틀린 설정에서 멈춘다(`ConfigManager::ensureConfig`). 이 시험은 그것을 실행 없이 — 게임 프리셋 모두 — 본다.
 *          표에 없는 파일도 실패다(새 설정 파일이 검사를 비켜 가지 않게). 표의 짝은 `Scripts/common/ConfigCatalog.py`(문서용 목록)다.
 */
SW_TEST_CASE( ConfigFileSchemaTest, EveryConfigFileLoadsStrictly )
{
    const sw::string& projectRoot = sw::ResourceUtil::getProjectFolderPath();
    SW_ASSERT_FALSE( projectRoot.empty() );
    const sw::string configRoot = sw::FileUtil::joinPath( projectRoot, "Config" );

    sw::vector<sw::string> listFilePath;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( configRoot, "", listFilePath, true ) );

    uint32 loadedCount{ 0 };
    for ( const sw::string& filePath : listFilePath )
    {
        sw::string relative = sw::FileUtil::normalizeSeparators( sw::string_view( filePath ).substr( projectRoot.size() ) );
        while ( relative.empty() == false && relative.front() == '/' )
        {
            relative.erase( 0, 1 );
        }
        const ConfigFileSchemaInternal::ConfigKind* pKind = ConfigFileSchemaInternal::findKind( relative );
        SW_EXPECT_TRUE_MSG( pKind != nullptr, "종류 표에 없는 설정 파일입니다: " + relative );
        if ( pKind == nullptr || pKind->_pLoad == nullptr )
            continue;

        test::ScopedLogCollector logs;
        SW_EXPECT_TRUE_MSG( pKind->_pLoad( filePath ), relative + " 를 엄격하게 읽지 못했습니다:" + logs.joined() );
        ++loadedCount;
    }
    // 엔진 설정 + 메모리 예산 + 게임 프리셋 — 경로를 못 찾아 0 이면 이 시험은 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( loadedCount >= 10u, "읽은 설정 파일이 " + sw::to_string( loadedCount ) + " 개뿐입니다" );
}
