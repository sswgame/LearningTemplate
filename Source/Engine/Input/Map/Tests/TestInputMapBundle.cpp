// InputMapBundle — 입력 맵 묶음: 차례대로 쌓기, 레이어 겹침, 차이(덮기 · 지우기), 재배치 우선순위, 파일별 저장 글, 저장소의 모든 게임 묶음(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapBundle.h"

using namespace sw;

namespace
{
    struct InputMapBundleTestUtil
    {
        /** @brief 시험 묶음 폴더의 파일 이름들로 읽는 차례를 만듭니다. */
        static vector<AssetID> makeList( std::initializer_list<const utf8*> listName )
        {
            vector<AssetID> listAsset;
            for ( const utf8* pName : listName )
            {
                listAsset.push_back( AssetID{ FileUtil::joinPath( "game/empty/input/bundletest", string( pName ) + ".input.xml" ) } );
            }
            return listAsset;
        }

        /** @brief 액션 첫 바인딩의 슬롯입니다. */
        static InputSlot firstSlot( const InputMap& map, const utf8* pAction )
        {
            const ActionBinding* pBinding = map.getBinding( hashed_string( pAction ), 0 );
            SW_REQUIRE_TRUE( pBinding != nullptr );
            return pBinding->_arrSlot[0];
        }
    };
    using Util = InputMapBundleTestUtil;
} // namespace

/**
 * @brief [InputMapBundleTest] 게임 설정 파일의 줄 차례대로 쌓인다 — 맵 전체 값은 기본 맵, 덮기는 레이어를 물려받고, 지운 액션은 없다
 */
SW_TEST_CASE( InputMapBundleTest, SettingsFileStacksMapsInOrder )
{
    InputMap map;
    SW_REQUIRE_TRUE( InputMapBundle::loadFromSettings( map, AssetID{ "game/empty/input/bundletest/data/gamesettings.xml" } ) );

    SW_EXPECT_EQUAL( uint32{ 3 }, map.getSourceCount() );
    SW_EXPECT_NEAR( 0.5f, map.getHoldThreshold(), 0.0001f );
    SW_EXPECT_TRUE( map.getDefaultLayerName() == hashed_string( "Gameplay" ) );
    SW_EXPECT_TRUE( map.hasAction( hashed_string( "Move" ) ) );
    SW_EXPECT_FALSE( map.hasAction( hashed_string( "Menu" ) ) );
    SW_EXPECT_TRUE( Util::firstSlot( map, "Use" ) == InputSlot::fromKey( Key::F ) );
    SW_EXPECT_TRUE( map.findBindingLayer( hashed_string( "Use" ), 0 ) == hashed_string( "Common" ) );
    SW_EXPECT_TRUE( map.isActionOverride( hashed_string( "Use" ) ) );
    SW_EXPECT_EQUAL( uint32{ 2 }, map.getActionSourceIndex( hashed_string( "Use" ) ) );
    SW_EXPECT_EQUAL( uint32{ 1 }, map.getActionSourceIndex( hashed_string( "Move" ) ) );
}

/**
 * @brief [InputMapBundleTest] 게임 설정 파일 판별은 팩 안의 그 자리만 본다 — 패널의 경로 칸이 맵 파일과 묶음을 가른다
 */
SW_TEST_CASE( InputMapBundleTest, SettingsFileIsRecognizedByItsPlace )
{
    SW_EXPECT_TRUE( InputMapBundle::isSettingsFile( "game/shooter3d/data/gamesettings.xml" ) );
    SW_EXPECT_FALSE( InputMapBundle::isSettingsFile( "game/shooter3d/data/shooter.input.xml" ) );
    SW_EXPECT_FALSE( InputMapBundle::isSettingsFile( "gamesettings.xml" ) );
}

/**
 * @brief [InputMapBundleTest] 다른 레이어면 같은 키를 함께 쓴다. 함께 켜져 있으면 재배치 겹침으로는 본다
 */
SW_TEST_CASE( InputMapBundleTest, DifferentLayersMayShareAKey )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "common", "otherlayer" } ) ) );
    SW_EXPECT_TRUE( Util::firstSlot( map, "Use" ) == Util::firstSlot( map, "Game.Fire" ) );

    const hashed_string gameplay( "Gameplay" );
    const hashed_string common( "Common" );
    SW_EXPECT_TRUE( map.canLayersFireTogether( gameplay, common ) );
    string conflict;
    SW_EXPECT_TRUE( map.hasBindingConflict( InputSlot::fromKey( Key::W ), gameplay, conflict ) ); // Move 의 W 가 다른 레이어에 있다
    map.setLayerEnabled( common, false );
    SW_EXPECT_FALSE( map.canLayersFireTogether( gameplay, common ) );
    SW_EXPECT_FALSE( map.hasBindingConflict( InputSlot::fromKey( Key::W ), gameplay, conflict ) );
}

/**
 * @brief [InputMapBundleTest] 같은 레이어의 같은 입력은 오류이고, 거절된 묶음은 맵을 비운다
 */
SW_TEST_CASE( InputMapBundleTest, SameLayerSameInputIsRejected )
{
    SW_EXPECT_LOG( "InputMap.InputMapActionUsesSame" );
    SW_EXPECT_LOG( "InputMap.BundleRejected" );
    InputMap map;
    SW_EXPECT_FALSE( map.loadBundle( Util::makeList( { "base", "common", "samelayer" } ) ) );
    SW_EXPECT_TRUE( map.getActionNames().empty() );
    SW_EXPECT_EQUAL( uint32{ 0 }, map.getSourceCount() );
}

/**
 * @brief [InputMapBundleTest] 겹침은 다 읽은 맵에서 본다 — 앞 파일끼리의 겹침을 뒤 차이가 풀면 맞는 묶음이다
 */
SW_TEST_CASE( InputMapBundleTest, LaterDiffCanResolveAnEarlierClash )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "common", "samelayer", "unsnarl" } ) ) );
    SW_EXPECT_TRUE( Util::firstSlot( map, "Use" ) == InputSlot::fromKey( Key::G ) );
    SW_EXPECT_TRUE( Util::firstSlot( map, "Game.Clash" ) == InputSlot::fromKey( Key::E ) );
}

/**
 * @brief [InputMapBundleTest] 한 액션을 두 파일이 덮으면 오류다 — 이기는 쪽이 읽는 차례에 달리지 않게
 */
SW_TEST_CASE( InputMapBundleTest, OneDiffPerAction )
{
    SW_EXPECT_LOG( "InputMap.DiffTwice" );
    SW_EXPECT_LOG( "InputMap.InputMapInvalidAttributeValues" );
    SW_EXPECT_LOG( "InputMap.BundleRejected" );
    InputMap map;
    SW_EXPECT_FALSE( map.loadBundle( Util::makeList( { "base", "common", "unsnarl", "secondoverride" } ) ) );
}

/**
 * @brief [InputMapBundleTest] 차이가 앞 파일에 없는 액션을 가리키면 오류다 — 공통 이름이 바뀌면 낡은 차이가 조용히 남지 않는다
 */
SW_TEST_CASE( InputMapBundleTest, DiffOfUnknownActionIsRejected )
{
    SW_EXPECT_LOG( "InputMap.DiffUnknownAction" );
    SW_EXPECT_LOG( "InputMap.InputMapInvalidAttributeValues" );
    SW_EXPECT_LOG( "InputMap.BundleRejected" );
    InputMap map;
    SW_EXPECT_FALSE( map.loadBundle( Util::makeList( { "base", "badtarget" } ) ) );
}

/**
 * @brief [InputMapBundleTest] 맵 전체 값은 기본 맵에서만 정한다
 */
SW_TEST_CASE( InputMapBundleTest, MapWideValuesBelongToTheBaseMap )
{
    SW_EXPECT_LOG( "InputMap.InputMapAppendedAnotherMap" );
    SW_EXPECT_LOG( "InputMap.BundleRejected" );
    InputMap map;
    SW_EXPECT_FALSE( map.loadBundle( Util::makeList( { "base", "mapwide" } ) ) );
}

/**
 * @brief [InputMapBundleTest] 뒤 파일의 가상 커서는 앞뒤 파일의 패드 바인딩과 함께 검사한다
 */
SW_TEST_CASE( InputMapBundleTest, VirtualCursorIsCheckedAgainstTheWholeBundle )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "cursor" } ) ) );
    SW_EXPECT_TRUE( map.getVirtualCursorStick() == InputMapVirtualCursor::LeftStick );

    SW_EXPECT_LOG( "InputMap.InputMapActionBindsVirtual" );
    SW_EXPECT_LOG( "InputMap.BundleRejected" );
    SW_EXPECT_FALSE( map.loadBundle( Util::makeList( { "base", "cursorclash", "cursor" } ) ) );
}

/**
 * @brief [InputMapBundleTest] 우선순위는 공통 < 게임 차이 < 플레이어 재배치다 — 재배치가 이기고, 기본으로 되돌리면 차이의 키다
 */
SW_TEST_CASE( InputMapBundleTest, PlayerRebindWinsOverDiffAndResetsToIt )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "common", "diff" } ) ) );
    const hashed_string action( "Use" );
    SW_REQUIRE_TRUE( map.rebindSlot( action, InputSlot::fromKey( Key::H ) ) );
    SW_EXPECT_TRUE( Util::firstSlot( map, "Use" ) == InputSlot::fromKey( Key::H ) );

    InputSlot defaultSlot;
    SW_REQUIRE_TRUE( map.findDefaultRebindSlot( action, 0, defaultSlot ) );
    SW_EXPECT_TRUE( defaultSlot == InputSlot::fromKey( Key::F ) );
    map.resetAllBindingsToDefault();
    SW_EXPECT_TRUE( Util::firstSlot( map, "Use" ) == InputSlot::fromKey( Key::F ) );
}

/**
 * @brief [InputMapBundleTest] 저장 글은 파일마다 자기 몫이다 — 공통 파일은 덮이고 지워진 정의를 잃지 않고, 차이 파일은 차이만 쓰며, 다시 읽으면 같은 글이다
 */
SW_TEST_CASE( InputMapBundleTest, SourceTextRoundTripsPerFile )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "common", "diff" } ) ) );

    string baseText;
    string commonText;
    string diffText;
    SW_REQUIRE_TRUE( map.writeSourceText( 0, baseText ) && map.writeSourceText( 1, commonText ) && map.writeSourceText( 2, diffText ) );
    SW_EXPECT_TRUE( baseText.find( "holdThreshold" ) != string::npos );
    SW_EXPECT_TRUE( commonText.find( "holdThreshold" ) == string::npos );
    SW_EXPECT_TRUE( commonText.find( "name=\"Menu\"" ) != string::npos ); // 뒤 파일이 지웠어도 정의는 이 파일의 것이다
    SW_EXPECT_TRUE( commonText.find( "code=\"E\"" ) != string::npos );    // 뒤 파일이 덮기 전의 키
    SW_EXPECT_TRUE( diffText.find( "<override" ) != string::npos );
    SW_EXPECT_TRUE( diffText.find( "<remove" ) != string::npos );
    SW_EXPECT_TRUE( diffText.find( "name=\"Move\"" ) == string::npos );

    InputMap reread;
    SW_REQUIRE_TRUE( reread.loadBundle( Util::makeList( { "base", "common", "diff" } ) ) );
    string rereadDiff;
    SW_REQUIRE_TRUE( reread.writeSourceText( 2, rereadDiff ) );
    SW_EXPECT_EQUAL( diffText, rereadDiff );
}

/**
 * @brief [InputMapBundleTest] 패널이 덮은 액션을 지우면 차이 파일에 지우기로 남는다 — 앞 파일의 정의가 다시 살아나지 않게
 */
SW_TEST_CASE( InputMapBundleTest, RemovingAnOverrideBecomesARemove )
{
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( Util::makeList( { "base", "common", "diff" } ) ) );
    SW_REQUIRE_TRUE( map.removeAction( hashed_string( "Use" ) ) );
    string diffText;
    SW_REQUIRE_TRUE( map.writeSourceText( 2, diffText ) );
    SW_EXPECT_TRUE( diffText.find( "<override" ) == string::npos );
    SW_EXPECT_TRUE( diffText.find( "<remove name=\"Use\"" ) != string::npos );
}

/**
 * @brief [InputMapBundleTest] 키트 맵은 등록 차례가 아니라 id 순이고, 같은 id 는 거절한다
 */
SW_TEST_CASE( InputMapBundleTest, ExtensionsAreOrderedByID )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    InputMapExtensionRegistry registry;
    const InputMapExtension   second{ "b.input", "common/input/kits/b.input.xml" };
    const InputMapExtension   first{ "a.input", "common/input/kits/a.input.xml" };
    const InputMapExtension   duplicate{ "a.input", "common/input/kits/other.input.xml" };
    SW_EXPECT_TRUE( registry.registerExtension( &second ) );
    SW_EXPECT_TRUE( registry.registerExtension( &first ) );
    SW_EXPECT_FALSE( registry.registerExtension( &duplicate ) );

    vector<AssetID> listAsset;
    registry.appendAssets( listAsset );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listAsset.size() );
    SW_EXPECT_EQUAL( string( first._pAssetPath ), listAsset[0].str() );
    SW_EXPECT_EQUAL( string( second._pAssetPath ), listAsset[1].str() );

    registry.unregisterExtension( &first );
    listAsset.clear();
    registry.appendAssets( listAsset );
    SW_EXPECT_EQUAL( size_t{ 1 }, listAsset.size() );
}

/**
 * @brief [InputMapBundleTest] 공용 이름 표가 공통 파일과 맞고, 공통 파일을 모두 쌓아도 겹치지 않는다
 */
SW_TEST_CASE( InputMapBundleTest, CommonActionTableMatchesTheCommonFiles )
{
    vector<AssetID> listAsset;
    for ( const utf8* pName : { "base", "move", "traversal", "look", "camera", "camerapan", "cameraorbit", "menu", "time", "fastforward", "interact" } )
    {
        listAsset.push_back( AssetID{ FileUtil::joinPath( "common/input", string( pName ) + ".input.xml" ) } );
    }
    InputMap map;
    SW_REQUIRE_TRUE( map.loadBundle( listAsset ) );
    for ( const utf8* pAction :
          { CommonInputActions::kMove, CommonInputActions::kJump, CommonInputActions::kSprint, CommonInputActions::kLook, CommonInputActions::kToggleMouseLock, CommonInputActions::kCameraPan, CommonInputActions::kCameraRotate,
            CommonInputActions::kCameraLook, CommonInputActions::kCameraLookHold, CommonInputActions::kCameraZoom, CommonInputActions::kInteract, CommonInputActions::kStatus, CommonInputActions::kPause, CommonInputActions::kSlower,
            CommonInputActions::kFaster, CommonInputActions::kFastForward } )
    {
        SW_EXPECT_TRUE_MSG( map.hasAction( hashed_string( pAction ) ), pAction );
    }
}

/**
 * @brief [InputMapBundleTest] 모든 게임 프리셋의 묶음이 읽히고 겹침이 없다 — 활성 게임만 보는 쿠킹 · 시나리오가 놓치는 다른 게임까지 커밋 전에 잡는다
 */
SW_TEST_CASE( InputMapBundleTest, EveryGamePackBundleLoads )
{
    const string   presetFolder = FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Game" );
    vector<string> listPreset;
    SW_REQUIRE_TRUE( FileUtil::collectFiles( presetFolder, ".json", listPreset, false ) );
    SW_REQUIRE_TRUE( listPreset.empty() == false );
    for ( const string& presetPath : listPreset )
    {
        GameConfig preset;
        SW_REQUIRE_TRUE_MSG( ConfigManager::readConfigFile( preset, presetPath ) == ConfigReadResult::Loaded, presetPath.c_str() );
        SW_EXPECT_TRUE_MSG( InputMapBundle::checkPack( preset._packRoot.get() ), presetPath.c_str() );
    }
}
