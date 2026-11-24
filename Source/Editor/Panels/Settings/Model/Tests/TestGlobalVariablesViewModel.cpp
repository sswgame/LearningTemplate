#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Panels/Settings/Model/GlobalVariablesViewModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 자기만의 매니저에 변수 넷(테스트용 하나 포함)을 둡니다 — 모듈 · 이름 순은 gv_beta, gv_zeta(ModA), gv_alpha(ModB)입니다. */
    struct GlobalVariablesViewModelTest
    {
        GlobalVariableManager    _manager;
        GlobalVariablesViewModel _model;
        int32                    _value;

        GlobalVariablesViewModelTest()
            : _manager{}
            , _model{}
            , _value{ 0 }
        {
            SW_EXPECT_TRUE( _manager.registerVariable( "gv_zeta", GlobalVariableType::Int32, &_value, int32{ 0 }, "last letter", "", "ModA" ) );
            SW_EXPECT_TRUE( _manager.registerVariable( "gv_alpha", GlobalVariableType::Int32, &_value, int32{ 0 }, "first letter", "", "ModB" ) );
            SW_EXPECT_TRUE( _manager.registerVariable( "gv_beta", GlobalVariableType::Int32, &_value, int32{ 0 }, "second letter", "", "ModA" ) );
            SW_EXPECT_TRUE( _manager.registerVariable( "gv_testOnly", GlobalVariableType::Int32, &_value, int32{ 0 }, "switch", "", "ModA", 4, true ) );
        }

        /** @brief 훑기가 끝날 때까지 폴링합니다(작업 시스템이 있으면 워커가 돈다). 시한 안에 끝나면 true 입니다. */
        bool waitForPresetScan( string_view folder )
        {
            return test::waitUntil( [this, folder]()
            { return _model.pollPresetFilesIn( folder ) == false; } );
        }
    };
} // namespace

SW_TEST_CASE_F( GlobalVariablesViewModelTest, ListSortsByModuleThenNameAndHidesTestOnly )
{
    const vector<GlobalVariableInfo*>& listVisible = _model.refreshVariableList( _manager, "" );

    SW_REQUIRE_EQUAL( size_t{ 3 }, listVisible.size() );
    SW_EXPECT_TRUE( listVisible[0]->_name == "gv_beta" );
    SW_EXPECT_TRUE( listVisible[1]->_name == "gv_zeta" );
    SW_EXPECT_TRUE( listVisible[2]->_name == "gv_alpha" );
    SW_EXPECT_EQUAL( 3u, _model.getTotalVariableCount() );
}

SW_TEST_CASE_F( GlobalVariablesViewModelTest, FilterMatchesNameDescriptionAndModule )
{
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.refreshVariableList( _manager, "ALPHA" ).size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.refreshVariableList( _manager, "last letter" ).size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, _model.refreshVariableList( _manager, "moda" ).size() );
    SW_EXPECT_TRUE( _model.refreshVariableList( _manager, "gv_testOnly" ).empty() );
    SW_EXPECT_TRUE( _model.refreshVariableList( _manager, "no such variable" ).empty() );
    SW_EXPECT_EQUAL( 3u, _model.getTotalVariableCount() ); // 수는 필터 전이다
}

SW_TEST_CASE_F( GlobalVariablesViewModelTest, ListRebuildsOnlyWhenFilterOrRegistrationChanges )
{
    (void)_model.refreshVariableList( _manager, "gv" );
    SW_EXPECT_EQUAL( 1u, _model.getListRebuildCount() );
    (void)_model.refreshVariableList( _manager, "gv" );
    SW_EXPECT_EQUAL( 1u, _model.getListRebuildCount() );

    (void)_model.refreshVariableList( _manager, "gv_" );
    SW_EXPECT_EQUAL( 2u, _model.getListRebuildCount() );

    _manager.unregisterVariablesByModule( "ModB" );
    SW_EXPECT_EQUAL( size_t{ 2 }, _model.refreshVariableList( _manager, "gv_" ).size() );
    SW_EXPECT_EQUAL( 3u, _model.getListRebuildCount() );
    SW_EXPECT_EQUAL( 2u, _model.getTotalVariableCount() );
}

SW_TEST_CASE_F( GlobalVariablesViewModelTest, PinToggleAppliesOnNextRefreshOnly )
{
    _model.requestTogglePin( "gv_beta" );
    SW_EXPECT_FALSE( _model.isPinned( "gv_beta" ) ); // 핀 섹션을 도는 중일 수 있어 미룬다

    (void)_model.refreshVariableList( _manager, "" );
    SW_EXPECT_TRUE( _model.isPinned( "gv_beta" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.getPinnedVariables().size() );

    (void)_model.refreshVariableList( _manager, "" ); // 요청은 한 번만 적용한다
    SW_EXPECT_TRUE( _model.isPinned( "gv_beta" ) );

    _model.requestTogglePin( "gv_beta" );
    (void)_model.refreshVariableList( _manager, "" );
    SW_EXPECT_FALSE( _model.isPinned( "gv_beta" ) );
}

SW_TEST_CASE( GlobalVariablesViewModelPresetTest, DisplayNameDropsFolderAndSuffix )
{
    SW_EXPECT_TRUE( GlobalVariablesViewModel::makePresetDisplayName( "D:/Saved/Presets/fast.gvpreset.xml" ) == "fast" );
    SW_EXPECT_TRUE( GlobalVariablesViewModel::makePresetDisplayName( "FAST.GVPRESET.XML" ) == "FAST" );
    SW_EXPECT_TRUE( GlobalVariablesViewModel::makePresetDisplayName( "D:/Saved/other.xml" ) == "other.xml" );
}

SW_TEST_CASE( GlobalVariablesViewModelPresetTest, EmptyNameIsNotSaved )
{
    GlobalVariablesViewModel model;
    SW_EXPECT_FALSE( model.savePreset( "" ) );
}

// 결함: 이름을 그대로 경로에 이어 "../x" 가 프리셋 폴더 밖에 썼다.
SW_TEST_CASE( GlobalVariablesViewModelPresetTest, NameWithPathCharacterIsRejected )
{
    SW_EXPECT_LOG( "GlobalVariablesPanel.PresetNameHasPathCharacter" );
    GlobalVariablesViewModel model;
    SW_EXPECT_FALSE( model.savePreset( "../outside" ) );
    SW_EXPECT_FALSE( model.savePreset( "sub\\inside" ) );
    SW_EXPECT_FALSE( model.savePreset( "C:rooted" ) );
}

SW_TEST_CASE_F( GlobalVariablesViewModelTest, ScanListsOnlyPresetFiles )
{
    const AbsolutePath folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "a.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "b.xml" ), "<Other/>" ) );

    SW_REQUIRE_TRUE( waitForPresetScan( folder.str() ) );

    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getPresetFiles().size() );
    SW_EXPECT_TRUE( GlobalVariablesViewModel::makePresetDisplayName( _model.getPresetFiles()[0] ) == "a" );
}

SW_TEST_CASE_F( GlobalVariablesViewModelTest, CleanListIsNotScannedAgain )
{
    const AbsolutePath folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( waitForPresetScan( folder.str() ) );
    SW_EXPECT_TRUE( _model.getPresetFiles().empty() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "late.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    SW_EXPECT_FALSE( _model.pollPresetFilesIn( folder.str() ) );
    SW_EXPECT_TRUE( _model.getPresetFiles().empty() );

    _model.markPresetListStale();
    SW_REQUIRE_TRUE( waitForPresetScan( folder.str() ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.getPresetFiles().size() );
}

// 결함: 훑기의 결과를 받을 때 다시 훑을 깃발을 내려, 훑는 사이에 저장한 프리셋이 다음 저장까지 목록에 없었다.
SW_TEST_CASE_F( GlobalVariablesViewModelTest, PresetSavedDuringScanIsListed )
{
    const AbsolutePath folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "first.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );

    SW_EXPECT_TRUE( _model.pollPresetFilesIn( folder.str() ) ); // 훑기를 건다 — 결과는 아직 받지 않았다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "second.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    _model.markPresetListStale(); // `savePreset` 이 쓴 뒤 하는 일

    SW_REQUIRE_TRUE( waitForPresetScan( folder.str() ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, _model.getPresetFiles().size() );
}

/**
 * @brief [GlobalVariablesViewModelUndoTest] 값 끌기 · 프리셋 불러오기 · Reset All 이 저마다 한 단계로 되돌아가고, 단계는 자기 변수만 움직인다
 */
SW_TEST_CASE( GlobalVariablesViewModelUndoTest, EditsPresetAndResetAllUndoAsOneStepEach )
{
    GlobalVariablesViewModel model;
    GlobalVariableInfo*      pCulling = GlobalVariablesViewModel::findVariable( "gv_shadowCascadeCulling" );
    GlobalVariableInfo*      pOther   = GlobalVariablesViewModel::findVariable( "gv_textureStreaming" );
    SW_REQUIRE_TRUE( pCulling != nullptr && pOther != nullptr );
    const bool bCulling = pCulling->getValueAsBool();
    const bool bOther   = pOther->getValueAsBool();

    model.beginEdit( pCulling->_name );
    pCulling->setValueAsBool( !bCulling );
    model.recordEdited( true );
    SW_EXPECT_FALSE( model.getCommandStack().undo() ); // 끄는 동안은 거절한다
    model.flushEdit( false );
    pOther->setValueAsBool( !bOther ); // 창 밖(콘솔)에서 고쳤다
    SW_EXPECT_TRUE( model.getCommandStack().undo() );
    SW_EXPECT_TRUE( pCulling->getValueAsBool() == bCulling );
    SW_EXPECT_TRUE( pOther->getValueAsBool() != bOther );
    SW_EXPECT_TRUE( model.takeUndoMoved() );
    SW_EXPECT_FALSE( model.takeUndoMoved() );

    const AbsolutePath presetPath = test::makeScratchPath( "undo.gvpreset.xml" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( presetPath ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( presetPath, bCulling ? R"(<GlobalVariablesPreset><Var name="gv_shadowCascadeCulling" value="false"/></GlobalVariablesPreset>)"
                                                                   : R"(<GlobalVariablesPreset><Var name="gv_shadowCascadeCulling" value="true"/></GlobalVariablesPreset>)" ) );
    SW_REQUIRE_TRUE( model.loadPreset( presetPath ) );
    SW_EXPECT_TRUE( pCulling->getValueAsBool() != bCulling );

    model.resetAllToDefault();
    SW_EXPECT_TRUE( pCulling->isAtDefault() );
    SW_EXPECT_TRUE( pOther->isAtDefault() );
    SW_EXPECT_TRUE( model.getCommandStack().undo() ); // Reset All
    SW_EXPECT_TRUE( pCulling->getValueAsBool() != bCulling );
    SW_EXPECT_TRUE( pOther->getValueAsBool() != bOther );
    SW_EXPECT_TRUE( model.getCommandStack().undo() ); // 프리셋
    SW_EXPECT_TRUE( pCulling->getValueAsBool() == bCulling );

    pOther->setValueAsBool( bOther );
}

/**
 * @brief [GlobalVariablesViewModelTest] 창은 관리자에 닿지 않고 ViewModel 이 에디터 범위의 관리자에서 변수를 찾는다(관리자 접근자는 비공개 — View 누수 단계 ①)
 */
SW_TEST_CASE( GlobalVariablesViewModelScopeTest, FindsVariablesThroughTheEditorScope )
{
    SW_EXPECT_TRUE( GlobalVariablesViewModel::findVariable( "gv_editorPanelTimes" ) != nullptr );
    SW_EXPECT_TRUE( GlobalVariablesViewModel::findVariable( "gv_noSuchVariableForTheScopeTest" ) == nullptr );
}
