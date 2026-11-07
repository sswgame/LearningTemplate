#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/SequenceModel.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <initializer_list>

using namespace sw;
using namespace sw::editor;

namespace
{
    struct SequenceModelTestUtil
    {
        /** @brief 지정한 프레임에 키가 있는 프레임 목록입니다(시각 순). */
        static vector<float32> makeKeyFrames( std::initializer_list<float32> listFrame )
        {
            SequenceKeyTrack track{};
            track._kind = SequenceTrackKind::Property;
            track.fitChannelsToKind();
            for ( const float32 frame : listFrame )
            {
                (void)track.setChannelKey( 0, frame, 1.0f );
            }
            vector<float32> listKeyFrame;
            track.collectKeyFrames( listKeyFrame );
            return listKeyFrame;
        }

        /** @brief 항목 종류가 요구하는 글 칸을 채웁니다. */
        static void fillRequiredFields( SequenceTrackItem& item )
        {
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            item._targetObject                = "Hero";
            if ( pInfo->_bSpawns )
                item._prefab = "a.prefab.xml";
            if ( pInfo->_bNeedsPreset )
                item._preset = "wet";
            if ( pInfo->_bPlaysScenario )
                item._scenario = "a.scenario.xml";
        }

        /** @brief 필수 칸을 채운 항목입니다(파일에서 온 항목처럼 문서에 바로 넣는다). */
        static SequenceTrackItem makeFilledItem( SequenceItemKind kind )
        {
            SequenceTrackItem item{};
            item._kind = kind;
            fillRequiredFields( item );
            return item;
        }
    };
} // namespace

// 패널이 칸을 하나씩 옮기던 때는 옮기지 않은 카메라 블렌드 · 프리롤이 저장 · 되돌리기마다 기본값으로 돌아갔다.
SW_TEST_CASE( SequenceModelTest, RestoreKeepsEveryAssetField )
{
    SequenceAsset asset;
    asset._cameraBlendInSeconds  = 0.5f;
    asset._cameraBlendOutSeconds = 0.25f;
    asset._cameraPrerollFrames   = 7;
    SequenceModel document;
    document.restoreAsset( asset );
    SW_EXPECT_NEAR( 0.5f, document._asset._cameraBlendInSeconds, 1e-5f );
    SW_EXPECT_NEAR( 0.25f, document._asset._cameraBlendOutSeconds, 1e-5f );
    SW_EXPECT_EQUAL( 7, document._asset._cameraPrerollFrames );
    SW_EXPECT_TRUE( document._asset.toXML() == asset.toXML() );
}

// 되돌리기가 항목을 줄이면 고른 번호가 없는 항목을 가리켜 타임라인이 빈 줄을 강조했다.
SW_TEST_CASE( SequenceModelTest, RestoreClampsTheSelection )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    document.addTrack( SequenceTrackKind::Transform, "" );
    document.addTrack( SequenceTrackKind::Transform, "" );
    document._selectedItem = 1;
    document._curveTrack   = 1;

    SequenceAsset smaller;
    smaller._listItem.push_back( SequenceTrackItem{} );
    smaller._listTrack.push_back( SequenceKeyTrack{} );
    document.restoreAsset( smaller );
    SW_EXPECT_EQUAL( 0, document._selectedItem );
    SW_EXPECT_EQUAL( 0, document._selectedTrack );
    SW_EXPECT_EQUAL( -1, document._curveTrack );

    document.restoreAsset( SequenceAsset{} );
    SW_EXPECT_EQUAL( -1, document._selectedItem );
    SW_EXPECT_EQUAL( -1, document._selectedTrack );
    SW_EXPECT_TRUE( document.findSelectedTrack() == nullptr );
}

SW_TEST_CASE( SequenceModelTest, ResetSelectionPicksTheFirstTrack )
{
    SequenceModel document;
    document.addTrack( SequenceTrackKind::Transform, "" );
    document.addTrack( SequenceTrackKind::Property, "" );
    document._selectedItem = 3;
    document._curveTrack   = 1;
    document.resetSelection();
    SW_EXPECT_EQUAL( -1, document._selectedItem );
    SW_EXPECT_EQUAL( 0, document._selectedTrack );
    SW_EXPECT_EQUAL( -1, document._curveTrack );
}

// 스폰 · 외형 · 시나리오는 필수 칸이 빈 채 문서에 들어가면 저장한 파일과 되돌리기 스냅숏이 읽기 오류가 됐다 - 칸을 채운 뒤에만 항목이 된다.
SW_TEST_CASE( SequenceModelTest, EveryKindCanBeCreatedAndSurvivesSaveAndLoad )
{
    SequenceModel document;
    for ( const SequenceItemKindInfo& info : kArrSequenceItemKindInfo )
    {
        if ( document.addItem( info._kind, "Kind" ) >= 0 )
            continue;
        SW_REQUIRE_TRUE( document.isDraftingItem() );
        SequenceModelTestUtil::fillRequiredFields( document._itemEdit );
        SW_EXPECT_TRUE( document.commitItemDraft() >= 0 );
    }
    SW_REQUIRE_EQUAL( SW_COUNT_OF( kArrSequenceItemKindInfo ), document._asset._listItem.size() );
    SequenceAsset readBack;
    SW_REQUIRE_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );
    const SequenceTrackItem& spawn = readBack._listItem[static_cast<size_t>( SequenceItemKind::Spawn )];
    SW_EXPECT_TRUE( spawn._kind == SequenceItemKind::Spawn );
    SW_EXPECT_TRUE( spawn._prefab == "a.prefab.xml" );
    SW_EXPECT_TRUE( readBack._listItem[static_cast<size_t>( SequenceItemKind::Appearance )]._preset == "wet" );
    SW_EXPECT_TRUE( readBack._listItem[static_cast<size_t>( SequenceItemKind::Scenario )]._scenario == "a.scenario.xml" );
}

SW_TEST_CASE( SequenceModelTest, KindsThatNeedFieldsOpenADraftInsteadOfAnItem )
{
    SequenceModel document;
    for ( const SequenceItemKind kind : { SequenceItemKind::Spawn, SequenceItemKind::Appearance, SequenceItemKind::Scenario } )
    {
        SW_EXPECT_EQUAL( -1, document.addItem( kind, "Kind" ) );
        SW_EXPECT_TRUE( document.isDraftingItem() );
        SW_EXPECT_TRUE( document._itemEdit._kind == kind );
    }
    SW_EXPECT_TRUE( document._asset._listItem.empty() );

    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Count, "Count" ) );
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset._listItem.empty() );
}

// 초안이 열려 있어도 문서는 언제나 저장 · 되돌리기 스냅숏으로 읽힌다 - 이 불변식이 설계의 전부다.
SW_TEST_CASE( SequenceModelTest, DraftWithEmptyFieldsNeverEntersTheDocument )
{
    SequenceModel document;
    document._asset._frameMin = 5;
    document._asset._frameMax = 8;
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Spawn, "Spawn" ) );
    const string snapshot = document._asset.toXML();

    SW_EXPECT_EQUAL( -1, document.commitItemDraft() );
    document._itemEdit._prefab = "a.prefab.xml";
    SW_EXPECT_EQUAL( -1, document.commitItemDraft() ); // 스폰은 이름도 필수다
    SW_EXPECT_TRUE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset.toXML() == snapshot );
    SequenceAsset readBack;
    SW_EXPECT_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );

    document._itemEdit._targetObject = "Spawned";
    SW_REQUIRE_EQUAL( 0, document.commitItemDraft() );
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_EQUAL( 0, document._selectedItem );
    const SequenceTrackItem& item = document._asset._listItem[0];
    SW_EXPECT_TRUE( item._name == "Spawn 0" );
    SW_EXPECT_TRUE( item._kind == SequenceItemKind::Spawn );
    SW_EXPECT_EQUAL( 5, item._start );
    SW_EXPECT_EQUAL( 8, item._end );
    SW_EXPECT_EQUAL( kArrSequenceItemKindInfo[static_cast<size_t>( SequenceItemKind::Spawn )]._defaultColor, item._color );
    SW_EXPECT_EQUAL( -1, document.commitItemDraft() ); // 닫힌 초안을 두 번 더하지 않는다
    SW_EXPECT_EQUAL( size_t{ 1 }, document._asset._listItem.size() );
}

SW_TEST_CASE( SequenceModelTest, DraftClosesOnCancelPlainAddAndNewDocument )
{
    SequenceModel document;
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Scenario, "Scenario" ) );
    document.cancelItemDraft();
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset._listItem.empty() );

    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Scenario, "Scenario" ) );
    SW_EXPECT_EQUAL( 0, document.addItem( SequenceItemKind::Clip, "Clip" ) );
    SW_EXPECT_FALSE( document.isDraftingItem() );

    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Scenario, "Scenario" ) );
    document.resetSelection();
    SW_EXPECT_FALSE( document.isDraftingItem() );
}

// 되돌리기는 문서만 바꾼다 - 치던 초안을 잃지 않고, 사본이 고른 항목으로 덮이지도 않는다.
SW_TEST_CASE( SequenceModelTest, UndoKeepsTheOpenDraft )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    document._selectedItem = 0;
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Scenario, "Scenario" ) );
    document._itemEdit._scenario = "a.scenario.xml";

    document.restoreAsset( document._asset );
    document.syncItemEdit();
    SW_EXPECT_TRUE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._itemEdit._kind == SequenceItemKind::Scenario );
    SW_EXPECT_TRUE( document._itemEdit._scenario == "a.scenario.xml" );
    SW_EXPECT_FALSE( document.commitItemEdit() ); // 초안은 고른 항목에 적용되지 않는다
    SW_EXPECT_TRUE( document._asset._listItem[0]._scenario.empty() );
}

// 결함: 파일에서 온 스폰 항목의 대상 칸을 패널에서 비우면 문서가 그대로 비어 저장 · 되돌리기 스냅숏이 읽기 오류가 됐다.
SW_TEST_CASE( SequenceModelTest, ClearingARequiredFieldIsRefusedAndRestored )
{
    SequenceModel document;
    document._asset._listItem.push_back( SequenceModelTestUtil::makeFilledItem( SequenceItemKind::Spawn ) );
    document._selectedItem = 0;
    document.syncItemEdit();

    document._itemEdit._targetObject.clear();
    SW_EXPECT_FALSE( document.commitItemEdit() );
    SW_EXPECT_TRUE( document._asset._listItem[0]._targetObject == "Hero" );
    SW_EXPECT_TRUE( document._itemEdit._targetObject == "Hero" ); // 칸이 앞 값으로 돌아온다

    document._itemEdit._prefab.clear();
    SW_EXPECT_FALSE( document.commitItemEdit() );
    SW_EXPECT_TRUE( document._asset._listItem[0]._prefab == "a.prefab.xml" );

    document._itemEdit._prefab = "b.prefab.xml";
    SW_EXPECT_TRUE( document.commitItemEdit() );
    SW_EXPECT_TRUE( document._asset._listItem[0]._prefab == "b.prefab.xml" );
    SequenceAsset readBack;
    SW_EXPECT_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );
}

// 사본은 글 칸만 적용한다 - 칸을 고치는 사이 타임라인이 옮긴 구간을 옛 값으로 덮지 않는다.
SW_TEST_CASE( SequenceModelTest, ItemEditKeepsTheTimelineSpan )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Event, "Event" );
    document._selectedItem = 0;
    document.syncItemEdit();
    document._asset._listItem[0]._start = 3;
    document._asset._listItem[0]._end   = 7;
    document._itemEdit._name            = "Cut";
    SW_EXPECT_TRUE( document.commitItemEdit() );
    SW_EXPECT_TRUE( document._asset._listItem[0]._name == "Cut" );
    SW_EXPECT_EQUAL( 3, document._asset._listItem[0]._start );
    SW_EXPECT_EQUAL( 7, document._asset._listItem[0]._end );
}

SW_TEST_CASE( SequenceModelTest, ItemEditFollowsTheSelection )
{
    SequenceModel document;
    SW_EXPECT_FALSE( document.hasEditedItem() );
    SW_EXPECT_FALSE( document.commitItemEdit() );

    (void)document.addItem( SequenceItemKind::Clip, "A" );
    (void)document.addItem( SequenceItemKind::Event, "B" );
    document._selectedItem = 1;
    document.syncItemEdit();
    SW_EXPECT_TRUE( document.hasEditedItem() );
    SW_EXPECT_TRUE( document._itemEdit._name == "B 1" );

    document._selectedItem = 5; // 위젯이 준 번호가 범위 밖이어도 없는 항목을 읽지 않는다
    SW_EXPECT_FALSE( document.hasEditedItem() );
    document.syncItemEdit();
    SW_EXPECT_FALSE( document.commitItemEdit() );
}

// 위젯이 주던 고정 길이 10 은 짧은 타임라인을 넘어 띠 밖으로 나갔다.
SW_TEST_CASE( SequenceModelTest, NewItemStaysInsideTheTimeline )
{
    SequenceModel document;
    document._asset._frameMin = 5;
    document._asset._frameMax = 8;
    const int32 itemIndex     = document.addItem( SequenceItemKind::Event, "Event" );
    SW_REQUIRE_EQUAL( 0, itemIndex );
    const SequenceTrackItem& item = document._asset._listItem[0];
    SW_EXPECT_EQUAL( 5, item._start );
    SW_EXPECT_EQUAL( 8, item._end );
    SW_EXPECT_TRUE( item._kind == SequenceItemKind::Event );
    SW_EXPECT_EQUAL( kArrSequenceItemKindInfo[static_cast<size_t>( SequenceItemKind::Event )]._defaultColor, item._color );
}

// 번호가 목록 길이뿐이면 지운 뒤 더한 이름이 남은 이름과 겹쳤다.
SW_TEST_CASE( SequenceModelTest, NewNamesDoNotCollideAfterRemoval )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Clip 1" );
    SW_EXPECT_TRUE( document.removeItem( 0 ) );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Clip 2" );

    document.addTrack( SequenceTrackKind::Transform, "" );
    document.addTrack( SequenceTrackKind::Transform, "" );
    document._selectedTrack = 0;
    SW_EXPECT_TRUE( document.removeSelectedTrack() );
    document.addTrack( SequenceTrackKind::Transform, "" );
    SW_EXPECT_TRUE( document._asset._listTrack[0]._name == "Transform 1" );
    SW_EXPECT_TRUE( document._asset._listTrack[1]._name == "Transform 2" );
}

// 앞 항목을 지워도 고른 번호가 그대로면 이름 칸이 옆 항목을 고쳤다.
SW_TEST_CASE( SequenceModelTest, RemoveItemKeepsTheSelectedItem )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Clip, "A" );
    (void)document.addItem( SequenceItemKind::Clip, "B" );
    (void)document.addItem( SequenceItemKind::Clip, "C" );
    document._selectedItem = 2;
    SW_EXPECT_TRUE( document.removeItem( 0 ) );
    SW_EXPECT_EQUAL( 1, document._selectedItem );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "C 2" );
    SW_EXPECT_TRUE( document.removeItem( 0 ) );
    SW_EXPECT_EQUAL( 0, document._selectedItem );
    SW_EXPECT_TRUE( document.removeItem( 0 ) );
    SW_EXPECT_EQUAL( -1, document._selectedItem );
    SW_EXPECT_FALSE( document.removeItem( 0 ) );
    SW_EXPECT_FALSE( document.removeItem( -1 ) );
}

SW_TEST_CASE( SequenceModelTest, DuplicateCopiesTheItemWithANewName )
{
    SequenceModel document;
    (void)document.addItem( SequenceItemKind::Event, "Event" );
    document._asset._listItem[0]._name  = "Cut";
    document._asset._listItem[0]._start = 3;
    SW_EXPECT_EQUAL( 1, document.duplicateItem( 0 ) );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Cut Copy" );
    SW_EXPECT_EQUAL( 3, document._asset._listItem[1]._start );
    SW_EXPECT_TRUE( document._asset._listItem[1]._kind == SequenceItemKind::Event );
    SW_EXPECT_EQUAL( 2, document.duplicateItem( 0 ) );
    SW_EXPECT_TRUE( document._asset._listItem[2]._name == "Cut Copy 2" );
    SW_EXPECT_EQUAL( -1, document.duplicateItem( 9 ) );
    SW_EXPECT_EQUAL( -1, document.duplicateItem( -1 ) );
}

SW_TEST_CASE( SequenceModelTest, AddTrackFitsChannelsAndSelects )
{
    SequenceModel document;
    document.addTrack( SequenceTrackKind::Transform, "Hero" );
    document.addTrack( SequenceTrackKind::RigSlot, "" );
    SW_EXPECT_EQUAL( 1, document._selectedTrack );
    const vector<SequenceKeyTrack>& listTrack = document._asset._listTrack;
    SW_EXPECT_EQUAL( kSequenceTransformChannelCount, static_cast<uint32>( listTrack[0]._listChannel.size() ) );
    SW_EXPECT_TRUE( listTrack[0]._targetObject == "Hero" );
    SW_EXPECT_TRUE( listTrack[0]._name == "Transform 0" );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listTrack[1]._listChannel.size() ) );
    SW_EXPECT_TRUE( listTrack[1]._name == "Rig Slot 1" );
}

SW_TEST_CASE( SequenceModelTest, RemoveSelectedTrackClosesTheCurve )
{
    SequenceModel document;
    SW_EXPECT_FALSE( document.removeSelectedTrack() );
    document.addTrack( SequenceTrackKind::Property, "" );
    document._curveTrack   = 0;
    document._curveChannel = 0;
    SW_EXPECT_TRUE( document.findCurveChannel() != nullptr );
    document._curveChannel = 1;
    SW_EXPECT_TRUE( document.findCurveChannel() == nullptr ); // 프로퍼티 트랙은 채널이 하나다
    document._curveChannel = 0;
    SW_EXPECT_TRUE( document.removeSelectedTrack() );
    SW_EXPECT_EQUAL( -1, document._selectedTrack );
    SW_EXPECT_EQUAL( -1, document._curveTrack );
    SW_EXPECT_TRUE( document.findCurveTrack() == nullptr );
    SW_EXPECT_TRUE( document.findCurveChannel() == nullptr );
}

SW_TEST_CASE( SequenceModelTest, KeyNavigationSkipsTheKeyAtTheCurrentFrame )
{
    SequenceModel         document;
    const vector<float32> listKeyFrame = SequenceModelTestUtil::makeKeyFrames( { 10.0f, 20.0f, 30.0f } );
    SW_EXPECT_EQUAL( 10, document.findPreviousKeyFrame( listKeyFrame, 20 ).value() );
    SW_EXPECT_EQUAL( 30, document.findNextKeyFrame( listKeyFrame, 20 ).value() );
    SW_EXPECT_EQUAL( 20, document.findNextKeyFrame( listKeyFrame, 15 ).value() );
    SW_EXPECT_FALSE( document.findPreviousKeyFrame( listKeyFrame, 10 ).has_value() );
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, 30 ).has_value() );
    SW_EXPECT_FALSE( document.findNextKeyFrame( vector<float32>{}, 0 ).has_value() );
}

// 커브 편집기로 소수 프레임에 놓은 키도 정수 프레임으로 가고, 이동은 지금 자리에 머물지 않는다.
SW_TEST_CASE( SequenceModelTest, KeyNavigationRoundsFractionalKeys )
{
    SequenceModel         document;
    const vector<float32> listKeyFrame{ 10.6f };
    const int32           nextFrame = document.findNextKeyFrame( listKeyFrame, 5 ).value();
    SW_EXPECT_EQUAL( 11, nextFrame );
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, nextFrame ).has_value() );
    SW_EXPECT_EQUAL( 11, document.findNextKeyFrame( listKeyFrame, 10 ).value() );
}

// 타임라인 밖의 키로 가면 시간 막대가 갈 수 없는 프레임에 머물렀다. 1e10 은 int32 로 바꾸기 전에 걸러야 한다.
SW_TEST_CASE( SequenceModelTest, KeyNavigationStaysInsideTheTimeline )
{
    SequenceModel document;
    document._asset._frameMin          = 0;
    document._asset._frameMax          = 100;
    const vector<float32> listKeyFrame = { -5.0f, 50.0f, 150.0f, 1e10f };
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, 60 ).has_value() );
    SW_EXPECT_FALSE( document.findPreviousKeyFrame( listKeyFrame, 40 ).has_value() );
    SW_EXPECT_EQUAL( 50, document.findPreviousKeyFrame( listKeyFrame, 100 ).value() );
    SW_EXPECT_EQUAL( 50, document.findNextKeyFrame( listKeyFrame, 0 ).value() );
}

SW_TEST_CASE( SequenceModelTest, RatioAndFrameAreInverse )
{
    SequenceModel document;
    document._asset._frameMin = 10;
    document._asset._frameMax = 30;
    SW_EXPECT_EQUAL( 10, document.findFrameAtRatio( 0.0f ) );
    SW_EXPECT_EQUAL( 20, document.findFrameAtRatio( 0.5f ) );
    SW_EXPECT_EQUAL( 30, document.findFrameAtRatio( 1.0f ) );
    SW_EXPECT_EQUAL( 10, document.findFrameAtRatio( -3.0f ) );
    SW_EXPECT_EQUAL( 30, document.findFrameAtRatio( 3.0f ) );
    SW_EXPECT_NEAR( 0.25f, document.computeRatioAtFrame( 15.0f ), 1e-5f );
    SW_EXPECT_NEAR( 1.5f, document.computeRatioAtFrame( 40.0f ), 1e-5f );

    // 빈 타임라인도 0 으로 나누지 않는다.
    document._asset._frameMax = 10;
    SW_EXPECT_NEAR( 0.0f, document.computeRatioAtFrame( 10.0f ), 1e-5f );
    SW_EXPECT_EQUAL( 10, document.findFrameAtRatio( 0.0f ) );
}

SW_TEST_CASE( SequenceModelTest, RotationChannelsShowDegreesAndStoreRadians )
{
    SequenceKeyTrack track{};
    track._kind = SequenceTrackKind::Transform;
    track.fitChannelsToKind();
    SW_EXPECT_FALSE( SequenceModel::isRotationChannel( track, 2 ) );
    SW_EXPECT_TRUE( SequenceModel::isRotationChannel( track, 3 ) );
    SW_EXPECT_TRUE( SequenceModel::isRotationChannel( track, 5 ) );
    SW_EXPECT_FALSE( SequenceModel::isRotationChannel( track, 6 ) );

    SW_EXPECT_TRUE( SequenceModel::setChannelDisplayValue( track, 3, 4.0f, 90.0f ) );
    SW_EXPECT_NEAR( MathUtil::toRadian( 90.0f ), track.evaluateChannel( 3, 4.0f ).value(), 1e-4f );
    SW_EXPECT_NEAR( 90.0f, SequenceModel::computeChannelDisplayValue( track, 3, 4.0f, 0.0f ), 1e-3f );
    SW_EXPECT_TRUE( SequenceModel::setChannelDisplayValue( track, 0, 4.0f, 90.0f ) );
    SW_EXPECT_NEAR( 90.0f, track.evaluateChannel( 0, 4.0f ).value(), 1e-4f );

    // 키가 없는 채널은 대상의 지금 값을 보인다.
    SW_EXPECT_NEAR( 7.0f, SequenceModel::computeChannelDisplayValue( track, 1, 4.0f, 7.0f ), 1e-5f );
    SW_EXPECT_NEAR( 180.0f, SequenceModel::computeChannelDisplayValue( track, 4, 4.0f, MathUtil::kPi ), 1e-3f );
    SW_EXPECT_FALSE( SequenceModel::setChannelDisplayValue( track, kSequenceTransformChannelCount, 4.0f, 1.0f ) );

    // 프로퍼티 트랙의 채널 3 은 없고, 있어도 회전이 아니다.
    SequenceKeyTrack propertyTrack{};
    propertyTrack._kind = SequenceTrackKind::Property;
    SW_EXPECT_FALSE( SequenceModel::isRotationChannel( propertyTrack, 3 ) );
}

SW_TEST_CASE( SequenceModelTest, ReadTrackValuesReadsTheTransformInChannelOrder )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "SequenceModelTestHero" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SceneComponent* pScene = pObject->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    pScene->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );
    pScene->setLocalRotation( float3{ 0.1f, 0.2f, 0.3f } );
    pScene->setLocalScale( float3{ 4.0f, 5.0f, 6.0f } );

    SequenceKeyTrack track{};
    track._kind         = SequenceTrackKind::Transform;
    track._targetObject = "SequenceModelTestHero";
    float32 arrValue[kSequenceTransformChannelCount]{};
    SW_REQUIRE_TRUE( SequenceModel::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );
    SW_EXPECT_NEAR( 1.0f, arrValue[0], 1e-5f );
    SW_EXPECT_NEAR( 3.0f, arrValue[2], 1e-5f );
    SW_EXPECT_NEAR( 0.2f, arrValue[4], 1e-5f );
    SW_EXPECT_NEAR( 6.0f, arrValue[8], 1e-5f );

    // 자리가 모자라면 넘겨 쓰지 않는다.
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, arrValue, 1 ) );
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, nullptr, kSequenceTransformChannelCount ) );
}

SW_TEST_CASE( SequenceModelTest, ReadTrackValuesFailsWithoutATargetOrComponent )
{
    GameObjectManager manager;
    (void)manager.createGameObject( hashed_string( "SequenceModelTestBare" ) );
    float32 arrValue[kSequenceTransformChannelCount]{};

    SequenceKeyTrack track{};
    track._kind         = SequenceTrackKind::Transform;
    track._targetObject = "SequenceModelTestMissing";
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._targetObject = "SequenceModelTestBare";
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind = SequenceTrackKind::RigSlot;
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind         = SequenceTrackKind::Property;
    track._propertyPath = "NoSuchComponent.value";
    SW_EXPECT_FALSE( SequenceModel::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );
}
