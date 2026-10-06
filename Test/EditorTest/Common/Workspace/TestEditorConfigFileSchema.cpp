#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Config/EditorConfig.h"
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
        template <typename T>
        static bool loadStrict( const string& absolutePath )
        {
            T config{};
            return ConfigManager::readConfigFile( config, absolutePath ) == ConfigReadResult::Loaded;
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
            bool ( *_pLoad )( const string& absolutePath ); ///< nullptr = 앱이 쓰는 로컬 상태 · 파이썬이 보는 파일
        };

        /** @brief 앞의 줄이 먼저 맞습니다. 짝은 `Scripts/common/ConfigCatalog.py` 다. */
        static constexpr ConfigKind kArrKind[] = {
            {        "EditorConfig.json",       &loadStrict<EditorConfig>},
            {  "editortooldefaults.json", &loadStrict<EditorToolDefaults>},
            { "TextureImportConfig.json",              &loadTextureImport},
            {   "ModelImportConfig.json",                &loadModelImport},
            {"AssetValidationRules.json",                         nullptr}, // Scripts/common/AssetValidation.py · CheckAssetRules
            {                    "*.ini",                         nullptr}, // ImGui 가 쓴다(로컬)
            {             "*Graph*.json",                         nullptr}, // 노드 에디터 캔버스 · 도구 문서 임시본(로컬)
            {          "SpriteClip.json",                         nullptr}, // 도구 문서 임시본(로컬)
            {                "Layouts/*",                         nullptr}, // 저장한 레이아웃(로컬)
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
 * @brief [EditorConfigFileSchemaTest] Config/Editor 의 사람이 쓰는 설정은 모두 실제 로더로 엄격하게 읽힌다 — 표에 없는 파일은 실패다
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
    SW_EXPECT_TRUE_MSG( loadedCount >= 3u, "읽은 에디터 설정 파일이 " + to_string( loadedCount ) + " 개뿐입니다" );
}
