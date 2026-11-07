#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Diagnostics/EditorModuleOverrides.h"
#include "Editor/Panels/Settings/Model/ModulesModel.h"

#include "sw/config/ConfigConstants.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /**
     * @brief 빈 폴더의 카탈로그를 읽습니다. 프로젝트 + 켜진 키트 둘(`GF_Child` 가 `GF_Base` 에 의존) + 기본 꺼짐 + 다른 플랫폼 전용입니다.
     * @details 프로젝트 매니페스트 쓰기는 `applyPendingTo` 로 케이스 폴더에만 한다. `applyPending` 은 소스 트리를 고치므로 부르지 않는다.
     */
    struct ModulesModelTest
    {
        string       _folder;
        ModulesModel _model;

        ModulesModelTest()
            : _folder{ test::makeScratchPath( "Modules" ) }
            , _model{}
        {
            SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( _folder ) );
            const string excludeHere = string( R"(, "_listExcludedPlatform": [ ")" ) + build::kPlatformName + "\" ]";
            writeManifest( config::kTargetGameModule, "Game", "" );
            writeManifest( "GF_Base", "Kit", "" );
            writeManifest( "GF_Child", "Kit", R"(, "_listDependency": [ { "_name": "GF_Base" } ])" );
            writeManifest( "GF_Off", "Kit", R"(, "_bEnabledByDefault": false)" );
            writeManifest( "GF_OtherPlatform", "Kit", excludeHere.c_str() );
            _model.reloadFrom( _folder );
            SW_EXPECT_TRUE_MSG( _model.getLoadError().empty(), _model.getLoadError().c_str() );
        }

        static string makeManifestText( const utf8* pName, const utf8* pKind, const utf8* pExtra )
        {
            return string( "{ \"_name\": \"" ) + pName + "\", \"_version\": \"1.0.0\", \"_kind\": \"" + pKind + "\"," +
                   " \"_listConfiguration\": [ \"Dev\" ], \"_listTarget\": [ \"Client\", \"Server\" ]" + pExtra + " }";
        }

        void writeManifest( const utf8* pName, const utf8* pKind, const utf8* pExtra ) const
        {
            const string path = FileUtil::joinPath( _folder, string( pName ) + ModuleCatalog::kManifestExtension );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, makeManifestText( pName, pKind, pExtra ) ) );
        }
    };
} // namespace

SW_TEST_CASE_F( ModulesModelTest, ReloadResolvesTheCatalog )
{
    SW_EXPECT_EQUAL( size_t{ 5 }, _model.getCatalog().getManifestCount() );
    SW_EXPECT_TRUE( _model.getResolution().isActive( "GF_Base" ) );
    SW_EXPECT_TRUE( _model.getResolution().isActive( "GF_Child" ) );
    SW_EXPECT_FALSE( _model.getResolution().isActive( "GF_Off" ) );
}

SW_TEST_CASE_F( ModulesModelTest, InactiveReasonExplainsOffModulesOnly )
{
    SW_EXPECT_TRUE( _model.findInactiveReason( "GF_Off" ) == "disabled by default" );
    SW_EXPECT_TRUE( _model.findInactiveReason( "GF_Base" ).empty() );
    SW_EXPECT_TRUE( _model.findInactiveReason( "NoSuchModule" ).empty() );
}

// 결함: 창이 기본 조건(Windows · 양쪽 대상)으로 풀어, 다른 플랫폼에서 실제로 올린 모듈과 다른 켜짐을 보였다(Linux 빌드에서 잡힌다).
SW_TEST_CASE_F( ModulesModelTest, ResolvesForTheRunningPlatform )
{
    SW_EXPECT_FALSE( _model.getResolution().isActive( "GF_OtherPlatform" ) );
    SW_EXPECT_TRUE( _model.findInactiveReason( "GF_OtherPlatform" ).find( "not available" ) != string::npos );
}

SW_TEST_CASE_F( ModulesModelTest, FiltersMatchKindAndState )
{
    SW_EXPECT_TRUE( _model.isKindVisible( ModuleKind::Game ) );
    _model.setKindFilter( static_cast<int32>( ModuleKind::Kit ) );
    SW_EXPECT_TRUE( _model.isKindVisible( ModuleKind::Kit ) );
    SW_EXPECT_FALSE( _model.isKindVisible( ModuleKind::Game ) );

    SW_EXPECT_TRUE( _model.isStateVisible( "GF_Base" ) );
    SW_EXPECT_TRUE( _model.isStateVisible( "GF_Off" ) );
    _model.setStateFilter( ModuleStateFilter::On );
    SW_EXPECT_TRUE( _model.isStateVisible( "GF_Base" ) );
    SW_EXPECT_FALSE( _model.isStateVisible( "GF_Off" ) );
    _model.setStateFilter( ModuleStateFilter::Off );
    SW_EXPECT_FALSE( _model.isStateVisible( "GF_Base" ) );
    SW_EXPECT_TRUE( _model.isStateVisible( "GF_Off" ) );
}

SW_TEST_CASE_F( ModulesModelTest, SelectedDetailListsDependentsAndDropsGoneModule )
{
    EditorModuleDetail detail;
    SW_EXPECT_FALSE( _model.makeSelectedDetail( detail ) ); // 고른 것이 없다

    _model.selectModule( "GF_Base" );
    SW_REQUIRE_TRUE( _model.makeSelectedDetail( detail ) );
    SW_EXPECT_TRUE( detail._name == "GF_Base" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, detail._listDependent.size() );
    SW_EXPECT_TRUE( detail._listDependent[0] == "GF_Child" );

    _model.selectModule( "NoSuchModule" );
    SW_EXPECT_FALSE( _model.makeSelectedDetail( detail ) );
    SW_EXPECT_TRUE( _model.getSelectedModule().empty() );
}

SW_TEST_CASE_F( ModulesModelTest, TogglePreviewsWhatChangesAndCancelCloses )
{
    SW_REQUIRE_TRUE( _model.requestToggle( "GF_Child", false ) );
    SW_EXPECT_TRUE( _model.hasPending() );
    SW_EXPECT_TRUE( _model.getPendingModule() == "GF_Child" );
    SW_EXPECT_FALSE( _model.isPendingEnabled() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getNewlyInactive().size() );
    SW_EXPECT_TRUE( _model.getNewlyInactive()[0]._name == "GF_Child" );
    SW_EXPECT_TRUE( _model.getNewlyActive().empty() );

    _model.cancelPending();
    SW_EXPECT_FALSE( _model.hasPending() );
    SW_EXPECT_FALSE( _model.hasChangedSinceStart() );

    SW_REQUIRE_TRUE( _model.requestToggle( "GF_Off", true ) );
    SW_EXPECT_TRUE( _model.isPendingEnabled() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getNewlyActive().size() );
    SW_EXPECT_TRUE( _model.getNewlyActive()[0] == "GF_Off" );
    SW_EXPECT_TRUE( _model.getNewlyInactive().empty() );
}

SW_TEST_CASE_F( ModulesModelTest, ToggleThatBreaksTheGraphDoesNotAsk )
{
    SW_EXPECT_LOG( "ModulesPanel.ModulesTurningBreaksModule" );
    SW_EXPECT_FALSE( _model.requestToggle( "GF_Base", false ) ); // 켜진 GF_Child 가 의존한다
    SW_EXPECT_FALSE( _model.hasPending() );
}

SW_TEST_CASE_F( ModulesModelTest, ApplyWritesOnlyNonDefaultOverrides )
{
    const string path = test::makeScratchPath( "Project.module.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, makeManifestText( "Project", "Game", "" ) ) );

    SW_REQUIRE_TRUE( _model.requestToggle( "GF_Child", false ) );
    _model.applyPendingTo( path );
    SW_EXPECT_FALSE( _model.hasPending() );
    SW_EXPECT_TRUE( _model.hasChangedSinceStart() );

    string         text;
    string         error;
    ModuleManifest manifest;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
    SW_REQUIRE_TRUE_MSG( ModuleCatalog::parseManifest( text, {}, manifest, error ), error.c_str() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, manifest._listModuleOverride.size() );
    SW_EXPECT_TRUE( manifest._listModuleOverride[0]._name == "GF_Child" );
    SW_EXPECT_FALSE( manifest._listModuleOverride[0]._bEnabled );

    SW_REQUIRE_TRUE( _model.requestToggle( "GF_Child", true ) ); // 기본과 같아지면 줄을 지운다
    _model.applyPendingTo( path );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
    SW_REQUIRE_TRUE_MSG( ModuleCatalog::parseManifest( text, {}, manifest, error ), error.c_str() );
    SW_EXPECT_TRUE( manifest._listModuleOverride.empty() );
}

SW_TEST_CASE_F( ModulesModelTest, ApplyToUnreadableManifestLeavesNoChangeMark )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "ModulesPanel.ModulesProjectManifestCould" );
    SW_REQUIRE_TRUE( _model.requestToggle( "GF_Child", false ) );
    _model.applyPendingTo( test::makeScratchPath( "Missing.module.json" ) );
    SW_EXPECT_FALSE( _model.hasPending() ); // 성공 여부와 관계없이 미리보기를 닫는다
    SW_EXPECT_FALSE( _model.hasChangedSinceStart() );
}

// 결함: 읽기에 실패해도 옛 카탈로그의 해석이 남아, 없는 모듈이 켜진 것으로 보였다.
SW_TEST_CASE_F( ModulesModelTest, FailedReloadDropsTheOldResolution )
{
    SW_EXPECT_LOG( "ModulesPanel.ModulesCatalogCouldNot" );
    SW_EXPECT_TRUE( _model.getResolution().isActive( "GF_Base" ) );

    _model.reloadFrom( test::makeScratchPath( "NoSuchFolder" ) );

    SW_EXPECT_FALSE( _model.getLoadError().empty() );
    SW_EXPECT_FALSE( _model.getResolution().isActive( "GF_Base" ) );
    SW_EXPECT_TRUE( _model.getResolution()._listInactive.empty() );
    SW_EXPECT_EQUAL( size_t{ 0 }, _model.getCatalog().getManifestCount() );
}

// 결함: "읽었는가" 를 매니페스트 수로 가려, 빈 폴더는 그리기마다 폴더를 다시 읽었다.
SW_TEST_CASE( ModulesModelLoadTest, EmptyFolderIsNotReadAgain )
{
    const string folder = test::makeScratchPath( "Empty" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    ModulesModel model;
    model.reloadFrom( folder );
    SW_EXPECT_TRUE( model.getLoadError().empty() );

    model.ensureLoaded(); // 다시 읽으면 실행 파일 옆의 진짜 카탈로그를 읽는다
    SW_EXPECT_EQUAL( size_t{ 0 }, model.getCatalog().getManifestCount() );
}

SW_TEST_CASE( ModulesModelLoadTest, EveryListedKindHasANameAndOnlyEditorAndGameAreLocked )
{
    for ( const ModuleKind kind : ModulesModel::kArrKindOrder )
    {
        SW_EXPECT_FALSE( string_view( ModulesModel::getKindName( kind ) ).empty() );
        SW_EXPECT_TRUE( ( kind == ModuleKind::Editor || kind == ModuleKind::Game ) == ModulesModel::isLocked( kind ) );
    }
}
