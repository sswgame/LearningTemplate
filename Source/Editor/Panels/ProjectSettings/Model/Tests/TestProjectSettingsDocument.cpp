#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsDocument.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/PlatformConfigOverride.h"

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
            string _basePath;
            string _overridePath;
            string _baseText;
        };

        static OverriddenProject makeProject( string_view caseName )
        {
            const string      root = FileUtil::normalizeSeparators( test::makeScratchPath( caseName ) );
            OverriddenProject project;
            project._basePath     = FileUtil::joinPath( root, "Config/Engine/EngineConfig.json" );
            project._overridePath = FileUtil::joinPath( root, PlatformConfigOverride::makeEngineConfigPath( "Test" ) );
            SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Engine/EngineConfig.json" ), project._baseText ) );
            SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( project._basePath ) && FileUtil::writeTextFile( project._basePath, project._baseText ) );
            SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( project._overridePath ) && FileUtil::writeTextFile( project._overridePath, kOverrideText ) );
            return project;
        }

        static EngineConfig readEngineConfig( const string& path )
        {
            string text;
            SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
            EngineConfig config;
            SW_REQUIRE_TRUE( ConfigManager::readConfigJSON( config, text, path.c_str() ) );
            return config;
        }
    };
} // namespace

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

    pConfig->_dataInheritanceMaxDepth = editedDepth;
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
