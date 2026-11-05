#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Settings/Model/GlobalVariablesModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 자기만의 매니저에 변수 넷(테스트용 하나 포함)을 둡니다 — 모듈 · 이름 순은 gv_beta, gv_zeta(ModA), gv_alpha(ModB)입니다. */
    struct GlobalVariablesModelTest
    {
        GlobalVariableManager _manager;
        GlobalVariablesModel  _model;
        int32                 _value;

        GlobalVariablesModelTest()
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
            for ( int32 attempt = 0; attempt < 5000; ++attempt )
            {
                if ( _model.pollPresetFilesIn( folder ) == false )
                    return true;
                ThreadUtil::sleepForMilliseconds( 1 );
            }
            return false;
        }
    };
} // namespace

SW_TEST_CASE_F( GlobalVariablesModelTest, ListSortsByModuleThenNameAndHidesTestOnly )
{
    const vector<GlobalVariableInfo*>& listVisible = _model.refreshVariableList( _manager, "" );

    SW_REQUIRE_EQUAL( size_t{ 3 }, listVisible.size() );
    SW_EXPECT_TRUE( listVisible[0]->_name == "gv_beta" );
    SW_EXPECT_TRUE( listVisible[1]->_name == "gv_zeta" );
    SW_EXPECT_TRUE( listVisible[2]->_name == "gv_alpha" );
    SW_EXPECT_EQUAL( 3u, _model.getTotalVariableCount() );
}

SW_TEST_CASE_F( GlobalVariablesModelTest, FilterMatchesNameDescriptionAndModule )
{
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.refreshVariableList( _manager, "ALPHA" ).size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.refreshVariableList( _manager, "last letter" ).size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, _model.refreshVariableList( _manager, "moda" ).size() );
    SW_EXPECT_TRUE( _model.refreshVariableList( _manager, "gv_testOnly" ).empty() );
    SW_EXPECT_TRUE( _model.refreshVariableList( _manager, "no such variable" ).empty() );
    SW_EXPECT_EQUAL( 3u, _model.getTotalVariableCount() ); // 수는 필터 전이다
}

SW_TEST_CASE_F( GlobalVariablesModelTest, ListRebuildsOnlyWhenFilterOrRegistrationChanges )
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

SW_TEST_CASE_F( GlobalVariablesModelTest, PinToggleAppliesOnNextRefreshOnly )
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

SW_TEST_CASE( GlobalVariablesModelPresetTest, DisplayNameDropsFolderAndSuffix )
{
    SW_EXPECT_TRUE( GlobalVariablesModel::makePresetDisplayName( "D:/Saved/Presets/fast.gvpreset.xml" ) == "fast" );
    SW_EXPECT_TRUE( GlobalVariablesModel::makePresetDisplayName( "FAST.GVPRESET.XML" ) == "FAST" );
    SW_EXPECT_TRUE( GlobalVariablesModel::makePresetDisplayName( "D:/Saved/other.xml" ) == "other.xml" );
}

SW_TEST_CASE( GlobalVariablesModelPresetTest, EmptyNameIsNotSaved )
{
    GlobalVariablesModel model;
    SW_EXPECT_FALSE( model.savePreset( "" ) );
}

// 결함: 이름을 그대로 경로에 이어 "../x" 가 프리셋 폴더 밖에 썼다.
SW_TEST_CASE( GlobalVariablesModelPresetTest, NameWithPathCharacterIsRejected )
{
    SW_EXPECT_LOG( "GlobalVariablesPanel.PresetNameHasPathCharacter" );
    GlobalVariablesModel model;
    SW_EXPECT_FALSE( model.savePreset( "../outside" ) );
    SW_EXPECT_FALSE( model.savePreset( "sub\\inside" ) );
    SW_EXPECT_FALSE( model.savePreset( "C:rooted" ) );
}

SW_TEST_CASE_F( GlobalVariablesModelTest, ScanListsOnlyPresetFiles )
{
    const string folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "a.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "b.xml" ), "<Other/>" ) );

    SW_REQUIRE_TRUE( waitForPresetScan( folder ) );

    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getPresetFiles().size() );
    SW_EXPECT_TRUE( GlobalVariablesModel::makePresetDisplayName( _model.getPresetFiles()[0] ) == "a" );
}

SW_TEST_CASE_F( GlobalVariablesModelTest, CleanListIsNotScannedAgain )
{
    const string folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( waitForPresetScan( folder ) );
    SW_EXPECT_TRUE( _model.getPresetFiles().empty() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "late.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    SW_EXPECT_FALSE( _model.pollPresetFilesIn( folder ) );
    SW_EXPECT_TRUE( _model.getPresetFiles().empty() );

    _model.markPresetListStale();
    SW_REQUIRE_TRUE( waitForPresetScan( folder ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, _model.getPresetFiles().size() );
}

// 결함: 훑기의 결과를 받을 때 다시 훑을 깃발을 내려, 훑는 사이에 저장한 프리셋이 다음 저장까지 목록에 없었다.
SW_TEST_CASE_F( GlobalVariablesModelTest, PresetSavedDuringScanIsListed )
{
    const string folder = test::makeScratchPath( "presets" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "first.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );

    SW_EXPECT_TRUE( _model.pollPresetFilesIn( folder ) ); // 훑기를 건다 — 결과는 아직 받지 않았다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "second.gvpreset.xml" ), "<GlobalVariablesPreset/>" ) );
    _model.markPresetListStale(); // `savePreset` 이 쓴 뒤 하는 일

    SW_REQUIRE_TRUE( waitForPresetScan( folder ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, _model.getPresetFiles().size() );
}
