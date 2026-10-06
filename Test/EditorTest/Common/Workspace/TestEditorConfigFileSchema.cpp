#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Config/ConfigManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorConfigFileSchemaInternal
    {
        /** @brief 엄격하게 읽고, 기본값과 같은 값을 다시 적은 키가 없는지도 봅니다(설정 파일에는 기본값과 다른 값만 적는다). */
        template <typename T>
        static bool loadStrict( const string& absolutePath )
        {
            T config{};
            if ( ConfigManager::readConfigFile( config, absolutePath ) != ConfigReadResult::Loaded )
                return false;
            string         text;
            vector<string> listEchoKey;
            if ( FileUtil::readTextFile( absolutePath, text ) == false || ConfigManager::collectDefaultEchoKeys<T>( text, listEchoKey ) == false )
                return false;
            for ( const string& key : listEchoKey )
                SW_EXPECT_TRUE_MSG( false, absolutePath + ": '" + key + "' restates the default - write only values that differ (docs/Config lists the defaults)" );
            return true;
        }

        static bool loadTextureImport( const string& absolutePath )
        {
            TextureImportConfig config;
            return config.loadFromFile( absolutePath );
        }

        static bool loadModelImport( const string& absolutePath )
        {
            ModelImportConfig config;
            return config.loadFromFile( absolutePath );
        }

        /** @brief `Config/Editor/` 아래 파일 한 종류입니다. */
        struct ConfigKind
        {
            const utf8* _pFileName;                         ///< `Config/Editor/` 아래 이름(`*` 는 아무 글)
            bool ( *_pLoad )( const string& absolutePath ); ///< nullptr = 파이썬이 보는 파일
        };

        /** @brief 앞의 줄이 먼저 맞습니다. 짝은 `Scripts/common/ConfigCatalog.py` 다. */
        static constexpr ConfigKind kArrKind[] = {
            {  "editortooldefaults.json", &loadStrict<EditorToolDefaults>},
            { "TextureImportConfig.json",              &loadTextureImport},
            {   "ModelImportConfig.json",                &loadModelImport},
            {"AssetValidationRules.json",                         nullptr}, // Scripts/common/AssetValidation.py · CheckAssetRules
        };

        static const ConfigKind* findKind( string_view relativePath )
        {
            for ( const ConfigKind& kind : kArrKind )
            {
                if ( AssetImportPathFilter::matchesWildcard( kind._pFileName, relativePath ) )
                    return &kind;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [EditorConfigFileSchemaTest] Config/Editor 의 설정은 모두 실제 로더로 엄격하게 읽힌다 — 표에 없는 파일은 실패다(앱이 쓰는 상태는 Saved/Editor 에 둔다)
 */
SW_TEST_CASE( EditorConfigFileSchemaTest, EveryEditorConfigFileLoadsStrictly )
{
    const string   editorConfigDir = EditorUtil::resolveProjectRelativePath( "Config/Editor" );
    vector<string> listFilePath;
    SW_ASSERT_TRUE( FileUtil::collectFiles( editorConfigDir, "", listFilePath, true ) );

    uint32 loadedCount{ 0 };
    for ( const string& filePath : listFilePath )
    {
        string relative = FileUtil::normalizeSeparators( string_view( filePath ).substr( editorConfigDir.size() ) );
        while ( relative.empty() == false && relative.front() == '/' )
            relative.erase( 0, 1 );
        const EditorConfigFileSchemaInternal::ConfigKind* pKind = EditorConfigFileSchemaInternal::findKind( relative );
        SW_EXPECT_TRUE_MSG( pKind != nullptr, "종류 표에 없는 에디터 설정 파일입니다: " + relative );
        if ( pKind == nullptr || pKind->_pLoad == nullptr )
            continue;
        test::ScopedLogCollector logs;
        SW_EXPECT_TRUE_MSG( pKind->_pLoad( filePath ), relative + " 를 엄격하게 읽지 못했습니다:" + logs.joined() );
        ++loadedCount;
    }
    // 임포트 설정 둘(editortooldefaults 는 기본값과 다른 값이 있을 때만 생긴다). 앱이 쓰는 상태는 Saved/Editor 에 있다.
    SW_EXPECT_TRUE_MSG( loadedCount >= 2u, "읽은 에디터 설정 파일이 " + to_string( loadedCount ) + " 개뿐입니다" );
}
