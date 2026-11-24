#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsDocument.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PlatformConfigOverride.h"
#include "Engine/Reflection/TypeInfo.h"

using namespace sw;
using namespace sw::editor;

// ProjectSettingsDocumentTest — 플랫폼 덮어쓰기(A-23): 설정 창은 바탕 파일 글을 직접 읽고 그 파일에만 쓴다. 합친 값(바탕 + 덮어쓰기)을 읽어 저장하면 덮어쓰기 값이 바탕으로 샌다.

namespace
{
    struct ProjectSettingsDocumentTestUtil
    {
        static constexpr const utf8* kOverrideText = "{\n    \"_maxFixedStepPerFrame\": 2\n}\n";

        /** @brief 케이스 폴더에 저장소의 엔진 설정을 바탕으로, 고정 스텝 수만 바꾸는 덮어쓰기를 그 옆에 둔 프로젝트입니다. */
        struct OverriddenProject
        {
            AbsolutePath _basePath;
            AbsolutePath _overridePath;
            string       _baseText;
        };

        static OverriddenProject makeProject( string_view caseName )
        {
            const AbsolutePath root = test::makeScratchPath( caseName );
            OverriddenProject  project;
            project._basePath     = root.join( "Config/Engine/EngineConfig.json" );
            project._overridePath = root.join( PlatformConfigOverride::makeEngineConfigPath( "Test" ) );
            SW_EXPECT_TRUE( FileUtil::readTextFile( ProcessRoots::get()._project.join( "Config/Engine/EngineConfig.json" ), project._baseText ) );
            SW_EXPECT_TRUE( FileUtil::ensureParentDirectoryExists( project._basePath ) && FileUtil::writeTextFile( project._basePath, project._baseText ) );
            SW_EXPECT_TRUE( FileUtil::ensureParentDirectoryExists( project._overridePath ) && FileUtil::writeTextFile( project._overridePath, kOverrideText ) );
            return project;
        }

        static EngineConfig readEngineConfig( const AbsolutePath& path )
        {
            string text;
            SW_EXPECT_TRUE( FileUtil::readTextFile( path, text ) );
            EngineConfig config;
            SW_EXPECT_TRUE( ConfigManager::readConfigJSON( config, text, path.c_str() ) );
            return config;
        }
    };
} // namespace

/**
 * @brief [ProjectSettingsDocumentTest] 저장소의 게임 설정(`Config/Game` 의 JSON)마다 열어 고치고 저장하면 엄격한 읽기로 고친 값이 돌아오고, 고치지 않은 칸은 그대로다(손 확인 "게임 설정 열어 저장" 을 대신한다 — 파일은 사본)
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, EveryGamePresetOpensEditsAndSaves )
{
    vector<AbsolutePath> listPath;
    SW_REQUIRE_TRUE( FileUtil::collectFiles( ProcessRoots::get()._project.join( "Config/Game" ), ".json", listPath, false ) );
    SW_REQUIRE_FALSE( listPath.empty() );
    const AbsolutePath root = test::makeScratchPath( "settings_game_presets" );
    for ( const AbsolutePath& sourcePath : listPath )
    {
        string text;
        SW_REQUIRE_TRUE( FileUtil::readTextFile( sourcePath, text ) );
        const AbsolutePath copyPath = root.join( FileUtil::getFileNamePart( sourcePath.str() ) );
        SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( copyPath ) && FileUtil::writeTextFile( copyPath, text ) );

        ProjectSettingsDocument document;
        SW_REQUIRE_TRUE( document.open( *GameConfig::StaticType(), copyPath ) == ConfigReadResult::Loaded );
        SW_EXPECT_FALSE( document.isModified() );
        GameConfig* const pConfig    = static_cast<GameConfig*>( document.getInstance() );
        const DataPath    packRoot   = pConfig->_packRoot;
        const size_t      fieldCount = pConfig->_cookRules._listEntryIDField.size();
        // 데이터 경로 칸은 읽기만 값을 넣는다(DataPath) — 되돌리기 글로 고친 값을 넣는다.
        SW_REQUIRE_TRUE( document.applyText( StringUtil::replace( document.captureText(), packRoot.get(), "game/savedpack" ) ) );
        SW_EXPECT_EQUAL( string{ "game/savedpack" }, static_cast<GameConfig*>( document.getInstance() )->_packRoot.get() );
        SW_EXPECT_TRUE( document.isModified() );
        SW_REQUIRE_TRUE( document.saveDocument() );
        SW_EXPECT_FALSE( document.isModified() );

        ProjectSettingsDocument reopened;
        SW_REQUIRE_TRUE( reopened.open( *GameConfig::StaticType(), copyPath ) == ConfigReadResult::Loaded );
        GameConfig* const pSaved = static_cast<GameConfig*>( reopened.getInstance() );
        SW_EXPECT_EQUAL( string{ "game/savedpack" }, pSaved->_packRoot.get() );
        SW_EXPECT_EQUAL( fieldCount, pSaved->_cookRules._listEntryIDField.size() );
        SW_EXPECT_TRUE( packRoot.get() != pSaved->_packRoot.get() );
    }
}

/**
 * @brief [ProjectSettingsDocumentTest] 되돌리기 단계의 글을 적용하면 값이 그대로 돌아오고 저장한 글 기준의 미저장 판정도 따라온다
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, UndoTextRestoresTheEditedInstance )
{
    const ProjectSettingsDocumentTestUtil::OverriddenProject project = ProjectSettingsDocumentTestUtil::makeProject( "ProjectSettingsUndoText" );
    ProjectSettingsDocument                                  document;
    SW_REQUIRE_TRUE( document.open( *EngineConfig::StaticType(), project._basePath ) == ConfigReadResult::Loaded );
    const string before = document.captureText();
    SW_EXPECT_FALSE( before.empty() );

    static_cast<EngineConfig*>( document.getInstance() )->_maxFixedStepPerFrame += 1;
    const string after = document.captureText();
    SW_EXPECT_TRUE( before != after );
    SW_EXPECT_TRUE( document.isModified() );

    SW_REQUIRE_TRUE( document.applyText( before ) );
    SW_EXPECT_EQUAL( before, document.captureText() );
    SW_EXPECT_FALSE( document.isModified() );
    SW_REQUIRE_TRUE( document.applyText( after ) );
    SW_EXPECT_TRUE( document.isModified() );
}

/**
 * @brief [ProjectSettingsDocumentTest] 덮어쓰기가 있는 엔진 설정을 열면 바탕 값을 보이고, 고쳐 저장하면 바탕 파일에는 고친 키만 바뀌며 덮어쓰기 값은 가지 않고 덮어쓰기 파일은 그대로인지 검증
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavingTheBaseDoesNotLeakThePlatformOverride )
{
    using Util                                = ProjectSettingsDocumentTestUtil;
    const Util::OverriddenProject project     = Util::makeProject( "settings_override_typed" );
    const EngineConfig            baseBefore  = Util::readEngineConfig( project._basePath );
    const uint32                  baseStep    = baseBefore._maxFixedStepPerFrame;
    const uint32                  editedDepth = baseBefore._dataInheritanceMaxDepth + 1;
    SW_REQUIRE_TRUE( baseStep != 2u ); // 덮어쓰기와 같으면 샜는지 가릴 수 없다

    ProjectSettingsDocument document;
    SW_REQUIRE_TRUE( document.open( *EngineConfig::StaticType(), project._basePath ) == ConfigReadResult::Loaded );
    EngineConfig* const pConfig = static_cast<EngineConfig*>( document.getInstance() );
    SW_REQUIRE_TRUE( pConfig != nullptr );
    SW_EXPECT_EQUAL( baseStep, pConfig->_maxFixedStepPerFrame ); // 합친 값이 아니라 바탕 파일 값

    const EngineConfig* const pDefault = static_cast<const EngineConfig*>( document.getDefaultInstance() );
    SW_REQUIRE_TRUE( pDefault != nullptr );
    SW_EXPECT_EQUAL( EngineConfig{}._dataInheritanceMaxDepth, pDefault->_dataInheritanceMaxDepth ); // 기본값은 파일 값이 아니라 타입의 초기값

    pConfig->_dataInheritanceMaxDepth = editedDepth;
    SW_EXPECT_EQUAL( EngineConfig{}._dataInheritanceMaxDepth, pDefault->_dataInheritanceMaxDepth ); // 편집이 기본값에 닿지 않는다
    SW_EXPECT_TRUE( document.isModified() );
    SW_REQUIRE_TRUE( document.saveDocument() );

    const EngineConfig baseAfter = Util::readEngineConfig( project._basePath );
    SW_EXPECT_EQUAL( editedDepth, baseAfter._dataInheritanceMaxDepth );
    SW_EXPECT_EQUAL( baseStep, baseAfter._maxFixedStepPerFrame );
    string overrideAfter;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( project._overridePath, overrideAfter ) );
    SW_EXPECT_EQUAL( string( Util::kOverrideText ), overrideAfter );
}

/**
 * @brief [ProjectSettingsDocumentTest] 덮어쓰기 파일을 표 페이지로 고쳐 저장하면 그 파일만 바뀌고 바탕 파일은 바이트까지 그대로인지 검증
 */
SW_TEST_CASE( ProjectSettingsDocumentTest, SavingTheOverrideLeavesTheBaseUntouched )
{
    using Util                            = ProjectSettingsDocumentTestUtil;
    const Util::OverriddenProject project = Util::makeProject( "settings_override_table" );

    ProjectSettingsJSONDocument table;
    SW_REQUIRE_TRUE( table.open( project._overridePath ).has_value() );
    SW_REQUIRE_TRUE( table.isReadOnly() == false );
    const ProjectSettingsJSONNode& root = table.getNode( ProjectSettingsJSONDocument::kRootNode );
    SW_REQUIRE_EQUAL( root._listChild.size(), size_t{ 1 } );
    SW_REQUIRE_TRUE( table.setLeafText( root._listChild[0], "3" ) );
    SW_REQUIRE_TRUE( table.saveDocument() );

    string overrideAfter;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( project._overridePath, overrideAfter ) );
    SW_EXPECT_EQUAL( string( "{\n    \"_maxFixedStepPerFrame\": 3\n}\n" ), overrideAfter );
    string baseAfter;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( project._basePath, baseAfter ) );
    SW_EXPECT_TRUE( baseAfter == project._baseText );
}
