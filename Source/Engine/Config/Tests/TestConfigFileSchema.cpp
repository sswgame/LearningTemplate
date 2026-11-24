// ConfigFileSchema — `Config/` 의 설정 파일이 모두 실제 로더로 엄격하게 읽히는지 본다(모르는 키 · 읽지 못한 값 · 범위 밖 값 · 기본값을 다시 적은 키 ·
// 바탕 층의 빠진 키). 기동은 틀린 설정에서 멈추는데(`ConfigManager::loadHostConfig`), 이 테스트는 그것을 모든 게임 프리셋에 대해 실행 없이 본다(nogpu).
#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/NetSendThrottleSettings.h"
#include "Engine/Config/OcclusionCullingConfig.h"
#include "Engine/Config/RenderScaleAdjusterConfig.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace
{
    struct ConfigFileSchemaInternal
    {
        /** @brief 설정 파일 한 종류 — 경로 앞부분(또는 전체)과 실제 로더입니다. 로더가 nullptr 이면 다른 검사가 맡거나 설정이 아닌 파일입니다(까닭을 적는다). */
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
            if ( sw::FileUtil::readTextFile( sw::FileUtil::toDiskPath( absolutePath ), text ) == false || sw::ConfigManager::collectDefaultEchoKeys<T>( text, listEchoKey ) == false )
                return false;
            for ( const sw::string& key : listEchoKey )
            {
                sw::fixed_string<sw::constant::buffer::kMax512> message;
                sw::formatstring( message.data(), message.capacity(), "%#: '%#' restates the default - write only values that differ", absolutePath, key );
                SW_EXPECT_TRUE_MSG( false, message );
            }
            return true;
        }

        /** @brief 바탕 층 파일에서 빠져도 되는 키와 이유입니다. 값의 출처가 다른 기준 하나다. */
        static constexpr const utf8* kArrBaseLayerExemptKey[] = {
            "_window._defaultRHI", // 쿠킹 표 `default_rhi_backend` 가 기준 — 파일에 적으면 두 곳이 된다
        };

        /** @brief @p shape 의 키마다(객체는 안으로 들어간다) @p file 에 없으면 경로를 담습니다. */
        static void collectMissingKeys( const sw::JSONValue& shape, const sw::JSONValue& file, const sw::string& prefix, sw::vector<sw::string>& outListKey )
        {
            for ( const sw::string& key : shape.getMemberNames() )
            {
                sw::StringBuilder<sw::constant::buffer::kMax256> pathBuilder;
                if ( prefix.empty() == false )
                    pathBuilder.appendFormat( "%#.", prefix );
                pathBuilder.append( key );
                const sw::string    path( pathBuilder.view() );
                const sw::JSONValue fileValue = file.get( key );
                if ( fileValue.isValid() == false )
                {
                    bool bExempt = false;
                    for ( const utf8* pExemptKey : kArrBaseLayerExemptKey )
                    {
                        bExempt = bExempt || path == pExemptKey;
                    }
                    if ( bExempt == false )
                        outListKey.push_back( path );
                    continue;
                }
                const sw::JSONValue shapeValue = shape.get( key );
                if ( shapeValue.isObject() )
                    collectMissingKeys( shapeValue, fileValue, path, outListKey );
            }
        }

        /**
         * @brief 바탕 층(값의 기준이 파일이고 헤더는 형식만인 설정 — `REFLECT( Meta = "ConfigBaseLayer" )`)을 엄격하게 읽고, 모든 키를 적었는지 봅니다.
         * @details 언리얼 `BaseEngine.ini` 와 같은 층이라 "기본값과 다른 값만" 규칙 대신 "빠짐없이" 규칙이다. 빠진 키는 헤더의 빈 형식(0)이 되어 버린다.
         */
        template <typename T>
        [[nodiscard]] static bool loadBaseLayer( const sw::string& absolutePath )
        {
            T config{};
            if ( sw::ConfigManager::readConfigFile( config, absolutePath ) != sw::ConfigReadResult::Loaded )
                return false;
            sw::string text;
            if ( sw::FileUtil::readTextFile( sw::FileUtil::toDiskPath( absolutePath ), text ) == false )
                return false;
            sw::JSONDocument fileDocument;
            sw::JSONDocument shapeDocument;
            const T          shape{};
            if ( fileDocument.parse( text ) == false || shapeDocument.parse( sw::JSONSerializer::serialize( &shape, *T::StaticType() ).value() ) == false )
                return false;
            sw::vector<sw::string> listMissingKey;
            collectMissingKeys( shapeDocument.getRoot(), fileDocument.getRoot(), sw::string(), listMissingKey );
            for ( const sw::string& key : listMissingKey )
            {
                sw::fixed_string<sw::constant::buffer::kMax512> message;
                sw::formatstring( message.data(), message.capacity(), "%#: '%#' is missing - this file is the base layer, the header has only the shape", absolutePath, key );
                SW_EXPECT_TRUE_MSG( false, message );
            }
            return listMissingKey.empty();
        }

        /** @brief 예산 파일을 실제 적용 함수로 읽습니다. 프로파일러는 켜지 않는다 — 켜면 전역 활성 프로파일러가 바뀐다(예산 칸만 쓴다). */
        [[nodiscard]] static bool loadMemoryBudget( const sw::string& absolutePath )
        {
            sw::string text;
            if ( sw::FileUtil::readTextFile( sw::FileUtil::toDiskPath( absolutePath ), text ) == false )
                return false;
            sw::MemoryProfiler profiler;
            const bool         bApplied = sw::MemoryBudgetMonitor::applyBudgetJSON( text, profiler ).has_value();
            SW_EXPECT_TRUE_MSG( bApplied, absolutePath );
            return bApplied;
        }

        /**
         * @brief 렌더 품질 단계 표를 실제 로드 길로 읽습니다 — 단계마다 커밋된 엔진 설정(같은 폴더의 `EngineConfig.json`) 위에 덮어(`ConfigSource::withRenderQuality`) 엄격하게 읽는다.
         * @details 단계는 `_render` 안쪽만 적으므로 단계 하나만으로는 형식이 서지 않는다 — 바탕 위에 덮은 글이 읽히는지가 이 파일의 검사다.
         */
        [[nodiscard]] static bool loadRenderQuality( const sw::string& absolutePath )
        {
            sw::string       text;
            sw::JSONDocument tiers;
            if ( sw::FileUtil::readTextFile( sw::FileUtil::toDiskPath( absolutePath ), text ) == false || tiers.parse( text ) == false || tiers.getRoot().isObject() == false )
                return false;
            const sw::string enginePath = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( absolutePath ), "EngineConfig.json" );
            bool             bAllRead   = tiers.getRoot().getMemberNames().empty() == false;
            for ( const sw::string& tier : tiers.getRoot().getMemberNames() )
            {
                sw::ConfigManager                               manager;
                const bool                                      bRead = manager.loadHostConfig<sw::EngineConfig>( sw::ConfigSource::disk( enginePath ).withRenderQuality( absolutePath, tier ) ) != nullptr;
                sw::fixed_string<sw::constant::buffer::kMax512> message;
                sw::formatstring( message.data(), message.capacity(), "%#: tier '%#' does not load over the engine config", absolutePath, tier );
                SW_EXPECT_TRUE_MSG( bRead, message );
                bAllRead = bAllRead && bRead;
            }
            return bAllRead;
        }

        /** @brief 앞의 줄이 먼저 맞습니다. */
        static constexpr ConfigKind kArrKind[] = {
            {                     "Config/Engine/EngineConfig.json",                                                                                     "",              &loadBaseLayer<sw::EngineConfig>},
            {                     "Config/Engine/MemoryBudget.json",                                                                                     "",                             &loadMemoryBudget},
            {                       "Config/Engine/PackConfig.json",                                                                                     "",                &loadBaseLayer<sw::PackConfig>},
            {                      "Config/Engine/RenderScale.json",                                                                                     "", &loadBaseLayer<sw::RenderScaleAdjusterConfig>},
            {                 "Config/Engine/OcclusionCulling.json",                                                                                     "",    &loadBaseLayer<sw::OcclusionCullingConfig>},
            {                  "Config/Engine/NetSendThrottle.json",                                                                                     "",   &loadBaseLayer<sw::NetSendThrottleSettings>},
            {                    "Config/Engine/RenderQuality.json",                                                                                     "",                            &loadRenderQuality},
            {                     "Config/Engine/CookContract.json",                                      "Scripts/generate/configure_files.py (configure)",                                       nullptr},
            {                 "Config/Engine/ProcessExitCodes.json",                                   "Scripts/generate/process_exit_codes.py (configure)",                                       nullptr},
            {                                        "Config/Game/",                                                                                     "",                   &loadStrict<sw::GameConfig>},
            {                 "Config/Server/chat_banned_words.txt",                                               "chat word filter (word list, not keys)",                                       nullptr},
            {           "Config/Server/chat_ignored_characters.txt",           "chat text normalizer (U+XXXX list, not keys - ChatModerationTest reads it)",                                       nullptr},
            {         "Config/Server/Online/Playground.online.json",                 "online kit settings: GameFramework types are not in the Engine image",                                       nullptr},
            {"Config/Server/Online/Playground.external.online.json",                 "online kit settings: GameFramework types are not in the Engine image",                                       nullptr},
            {                     "Config/Server/Online/Providers/", "provider lists: the kits read them (GameFramework types are not in the Engine image)",                                       nullptr},
            {                                      "Config/Server/",                                                                                     "",                 &loadStrict<sw::ServerConfig>},
            {                                 "Config/Environment/",                                         "Scripts/setup/environment.py - machine-local",                                       nullptr},
            {                                      "Config/Editor/",                                             "editor types are not in the Engine image",                                       nullptr},
            {                                         "Config/Dev/",          "DevTools types are not in the Engine image (DevTools reads them at startup)",                                       nullptr},
            {                                       "Config/Rules/",      "code rule tables: the build (layers) and Scripts/lint read them, not the engine",                                       nullptr},
            {                                       "Config/Build/",         "build policy data: CMake and the sanitizer runtime read them, not the engine",                                       nullptr},
        };

        static const ConfigKind* findKind( sw::string_view relativePath )
        {
            for ( const ConfigKind& kind : kArrKind )
            {
                if ( sw::StringUtil::startsWith( relativePath, sw::string_view( kind._pPathPrefix ) ) )
                    return &kind;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [ConfigFileSchemaTest] Config/ 의 설정 파일은 모두 실제 로더로 엄격하게 읽힌다 — 모르는 키 · 읽지 못한 값 · 범위 밖 값 · 기본값을 다시 적은 키가 하나도 없다
 * @details 표에 없는 파일도 실패다(새 설정 파일이 검사를 비켜 가지 않게).
 */
SW_TEST_CASE( ConfigFileSchemaTest, EveryConfigFileLoadsStrictly )
{
    const sw::AbsolutePath projectRoot = sw::ProcessRoots::get()._project;
    SW_REQUIRE_FALSE( projectRoot.empty() );
    const sw::AbsolutePath configRoot = projectRoot.join( "Config" );

    sw::vector<sw::string> listFilePath;
    SW_REQUIRE_TRUE( sw::FileUtil::collectFiles( configRoot, "", listFilePath, true ) );

    uint32 loadedCount{ 0 };
    for ( const sw::string& filePath : listFilePath )
    {
        const sw::string normalized = sw::FileUtil::normalizeSeparators( filePath );
        SW_REQUIRE_TRUE_MSG( sw::StringUtil::startsWith( normalized, projectRoot.str() ), normalized.c_str() );
        sw::string relative = normalized.substr( projectRoot.str().size() );
        while ( relative.empty() == false && relative.front() == '/' )
        {
            relative.erase( 0, 1 );
        }
        const ConfigFileSchemaInternal::ConfigKind*     pKind = ConfigFileSchemaInternal::findKind( relative );
        sw::fixed_string<sw::constant::buffer::kMax512> kindMessage;
        sw::formatstring( kindMessage.data(), kindMessage.capacity(), "the config file is not in the kind table: %#", relative );
        SW_EXPECT_TRUE_MSG( pKind != nullptr, kindMessage );
        if ( pKind == nullptr || pKind->_pLoad == nullptr )
            continue;

        sw::fixed_string<sw::constant::buffer::kMax512> loadMessage;
        sw::formatstring( loadMessage.data(), loadMessage.capacity(), "%# could not be read strictly (the reason is in the log above)", relative );
        SW_EXPECT_TRUE_MSG( pKind->_pLoad( filePath ), loadMessage );
        ++loadedCount;
    }
    // 엔진 설정 + 메모리 예산 + 팩 설정 + 게임 프리셋 — 경로를 못 찾아 0 이면 이 테스트는 아무것도 보지 않은 것이다.
    sw::fixed_string<sw::constant::buffer::kMax128> countMessage;
    sw::formatstring( countMessage.data(), countMessage.capacity(), "only %# config files were read", loadedCount );
    SW_EXPECT_TRUE_MSG( loadedCount >= 10u, countMessage );
}
