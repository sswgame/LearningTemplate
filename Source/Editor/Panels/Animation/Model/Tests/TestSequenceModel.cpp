#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/SequenceModel.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

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

        /** @brief 바인딩 Hero(1) 를 고른 문서입니다 — 새 항목 · 트랙의 대상이다. */
        static SequenceModel makeDocument()
        {
            SequenceModel document;
            (void)document.addBinding( "Hero" );
            return document;
        }

        /** @brief 항목 종류가 요구하는 칸을 채웁니다(종류 표의 필수 칸). 스폰은 혼자 채울 바인딩 @p spawnBinding 을 받는다. */
        static void fillRequiredProperties( SequenceTrackItem& item, uint32 spawnBinding = 1 )
        {
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo->requiresField( SequenceItemField::Binding ) )
                item._binding = pInfo->_bSpawns ? spawnBinding : 1;
            if ( pInfo->requiresField( SequenceItemField::Prefab ) )
                item._prefab = "a.prefab.xml";
            if ( pInfo->requiresField( SequenceItemField::Preset ) )
                item._preset = "wet";
            if ( pInfo->requiresField( SequenceItemField::Scenario ) )
                item._scenario = "a.scenario.xml";
            if ( pInfo->requiresField( SequenceItemField::Clip ) )
                item._clip = "a.animclip";
            if ( pInfo->requiresField( SequenceItemField::Sound ) )
                item._sound = "Cinematic.Hit";
            if ( pInfo->requiresField( SequenceItemField::Text ) )
                item._text = "Cinematic.Line";
        }

        /** @brief 필수 칸을 채운 항목입니다(파일에서 온 항목처럼 문서에 바로 넣는다). */
        static SequenceTrackItem makeFilledItem( SequenceItemKind kind )
        {
            SequenceTrackItem item{};
            item._kind = kind;
            fillRequiredProperties( item );
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
    SequenceModel document       = SequenceModelTestUtil::makeDocument();
    document.restoreAsset( asset );
    SW_EXPECT_NEAR( 0.5f, document._asset._cameraBlendInSeconds, 1e-5f );
    SW_EXPECT_NEAR( 0.25f, document._asset._cameraBlendOutSeconds, 1e-5f );
    SW_EXPECT_EQUAL( 7, document._asset._cameraPrerollFrames );
    SW_EXPECT_TRUE( document._asset.toXML() == asset.toXML() );
}

// 되돌리기가 항목을 줄이면 고른 번호가 없는 항목을 가리켜 타임라인이 빈 줄을 강조했다.
SW_TEST_CASE( SequenceModelTest, RestoreClampsTheSelection )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    (void)document.addTrack( SequenceTrackKind::Transform );
    (void)document.addTrack( SequenceTrackKind::Transform );
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
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addTrack( SequenceTrackKind::Transform );
    (void)document.addTrack( SequenceTrackKind::Property );
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
    SequenceModel document     = SequenceModelTestUtil::makeDocument();
    const uint32  spawnBinding = document._asset.addBinding( "Prop" ); // 스폰은 바인딩 하나를 혼자 채운다
    for ( uint32 kindIndex = 0; kindIndex < SequenceAsset::getItemKindCount(); ++kindIndex )
    {
        const SequenceItemKind kind = static_cast<SequenceItemKind>( kindIndex );
        if ( document.addItem( kind, "Kind" ).has_value() )
            continue;
        SW_REQUIRE_TRUE( document.isDraftingItem() );
        SequenceModelTestUtil::fillRequiredProperties( document._itemEdit, spawnBinding );
        SW_EXPECT_TRUE( document.commitItemDraft().has_value() );
    }
    SW_REQUIRE_EQUAL( size_t{ SequenceAsset::getItemKindCount() }, document._asset._listItem.size() );
    SequenceAsset readBack;
    SW_REQUIRE_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );
    const SequenceTrackItem& spawn = readBack._listItem[static_cast<size_t>( SequenceItemKind::Spawn )];
    SW_EXPECT_TRUE( spawn._kind == SequenceItemKind::Spawn );
    SW_EXPECT_TRUE( spawn._prefab == "a.prefab.xml" );
    SW_EXPECT_TRUE( readBack._listItem[static_cast<size_t>( SequenceItemKind::Appearance )]._preset == "wet" );
    SW_EXPECT_TRUE( readBack._listItem[static_cast<size_t>( SequenceItemKind::Scenario )]._scenario == "a.scenario.xml" );
}

SW_TEST_CASE( SequenceModelTest, KindsThatNeedPropertiesOpenADraftInsteadOfAnItem )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    for ( const SequenceItemKind kind : { SequenceItemKind::Spawn, SequenceItemKind::Appearance, SequenceItemKind::Scenario } )
    {
        SW_EXPECT_FALSE( document.addItem( kind, "Kind" ).has_value() );
        SW_EXPECT_TRUE( document.isDraftingItem() );
        SW_EXPECT_TRUE( document._itemEdit._kind == kind );
    }
    SW_EXPECT_TRUE( document._asset._listItem.empty() );

    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Count, "Count" ).has_value() );
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset._listItem.empty() );
}

// 초안이 열려 있어도 문서는 언제나 저장 · 되돌리기 스냅숏으로 읽힌다 - 이 불변식이 설계의 전부다.
SW_TEST_CASE( SequenceModelTest, DraftWithEmptyPropertiesNeverEntersTheDocument )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMin = 5;
    document._asset._frameMax = 8;
    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Spawn, "Spawn" ).has_value() );
    const string snapshot = document._asset.toXML();

    SW_EXPECT_FALSE( document.commitItemDraft().has_value() );
    document._itemEdit._prefab  = "a.prefab.xml";
    document._itemEdit._binding = 9;
    SW_EXPECT_FALSE( document.commitItemDraft().has_value() ); // 시퀀스에 없는 바인딩이다
    SW_EXPECT_TRUE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset.toXML() == snapshot );
    SequenceAsset readBack;
    SW_EXPECT_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );

    document._itemEdit._binding = 1;
    SW_REQUIRE_TRUE( document.commitItemDraft() == 0 );
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_EQUAL( 0, document._selectedItem );
    const SequenceTrackItem& item = document._asset._listItem[0];
    SW_EXPECT_TRUE( item._name == "Spawn 0" );
    SW_EXPECT_TRUE( item._kind == SequenceItemKind::Spawn );
    SW_EXPECT_EQUAL( 5, item._start );
    SW_EXPECT_EQUAL( 8, item._end );
    SW_EXPECT_EQUAL( SequenceAsset::findItemKindInfo( SequenceItemKind::Spawn )->_defaultColor, item._color );
    SW_EXPECT_FALSE( document.commitItemDraft().has_value() ); // 닫힌 초안을 두 번 더하지 않는다
    SW_EXPECT_EQUAL( size_t{ 1 }, document._asset._listItem.size() );
}

SW_TEST_CASE( SequenceModelTest, DraftClosesOnCancelPlainAddAndNewDocument )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Scenario, "Scenario" ).has_value() );
    document.cancelItemDraft();
    SW_EXPECT_FALSE( document.isDraftingItem() );
    SW_EXPECT_TRUE( document._asset._listItem.empty() );

    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Scenario, "Scenario" ).has_value() );
    SW_EXPECT_TRUE( document.addItem( SequenceItemKind::Clip, "Clip" ) == 0 );
    SW_EXPECT_FALSE( document.isDraftingItem() );

    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Scenario, "Scenario" ).has_value() );
    document.resetSelection();
    SW_EXPECT_FALSE( document.isDraftingItem() );
}

// 되돌리기는 문서만 바꾼다 - 치던 초안을 잃지 않고, 사본이 고른 항목으로 덮이지도 않는다.
SW_TEST_CASE( SequenceModelTest, UndoKeepsTheOpenDraft )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    document._selectedItem = 0;
    SW_EXPECT_FALSE( document.addItem( SequenceItemKind::Scenario, "Scenario" ).has_value() );
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
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    document._asset._listItem.push_back( SequenceModelTestUtil::makeFilledItem( SequenceItemKind::Spawn ) );
    document._selectedItem = 0;
    document.syncItemEdit();

    document._itemEdit._binding = kSequenceNoBinding;
    SW_EXPECT_FALSE( document.commitItemEdit() );
    SW_EXPECT_EQUAL( 1u, document._asset._listItem[0]._binding );
    SW_EXPECT_EQUAL( 1u, document._itemEdit._binding ); // 칸이 앞 값으로 돌아온다

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
    SequenceModel document = SequenceModelTestUtil::makeDocument();
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
    SequenceModel document = SequenceModelTestUtil::makeDocument();
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
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMin = 5;
    document._asset._frameMax = 8;
    const int32 itemIndex     = document.addItem( SequenceItemKind::Event, "Event" ).value();
    SW_REQUIRE_EQUAL( 0, itemIndex );
    const SequenceTrackItem& item = document._asset._listItem[0];
    SW_EXPECT_EQUAL( 5, item._start );
    SW_EXPECT_EQUAL( 8, item._end );
    SW_EXPECT_TRUE( item._kind == SequenceItemKind::Event );
    SW_EXPECT_EQUAL( SequenceAsset::findItemKindInfo( SequenceItemKind::Event )->_defaultColor, item._color );
}

// 번호가 목록 길이뿐이면 지운 뒤 더한 이름이 남은 이름과 겹쳤다.
SW_TEST_CASE( SequenceModelTest, NewNamesDoNotCollideAfterRemoval )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Clip 1" );
    SW_EXPECT_TRUE( document.removeItem( 0 ) );
    (void)document.addItem( SequenceItemKind::Clip, "Clip" );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Clip 2" );

    (void)document.addTrack( SequenceTrackKind::Transform );
    (void)document.addTrack( SequenceTrackKind::Transform );
    document._selectedTrack = 0;
    SW_EXPECT_TRUE( document.removeSelectedTrack() );
    (void)document.addTrack( SequenceTrackKind::Transform );
    SW_EXPECT_TRUE( document._asset._listTrack[0]._name == "Transform 1" );
    SW_EXPECT_TRUE( document._asset._listTrack[1]._name == "Transform 2" );
}

// 앞 항목을 지워도 고른 번호가 그대로면 이름 칸이 옆 항목을 고쳤다.
SW_TEST_CASE( SequenceModelTest, RemoveItemKeepsTheSelectedItem )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
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
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addItem( SequenceItemKind::Event, "Event" );
    document._asset._listItem[0]._name  = "Cut";
    document._asset._listItem[0]._start = 3;
    SW_EXPECT_TRUE( document.duplicateItem( 0 ) == 1 );
    SW_EXPECT_TRUE( document._asset._listItem[1]._name == "Cut Copy" );
    SW_EXPECT_EQUAL( 3, document._asset._listItem[1]._start );
    SW_EXPECT_TRUE( document._asset._listItem[1]._kind == SequenceItemKind::Event );
    SW_EXPECT_TRUE( document.duplicateItem( 0 ) == 2 );
    SW_EXPECT_TRUE( document._asset._listItem[2]._name == "Cut Copy 2" );
    SW_EXPECT_FALSE( document.duplicateItem( 9 ).has_value() );
    SW_EXPECT_FALSE( document.duplicateItem( -1 ).has_value() );
}

SW_TEST_CASE( SequenceModelTest, AddTrackFitsChannelsAndSelects )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    (void)document.addTrack( SequenceTrackKind::Transform );
    (void)document.addTrack( SequenceTrackKind::RigSlot );
    SW_EXPECT_EQUAL( 1, document._selectedTrack );
    const vector<SequenceKeyTrack>& listTrack = document._asset._listTrack;
    SW_EXPECT_EQUAL( kSequenceTransformChannelCount, static_cast<uint32>( listTrack[0]._listChannel.size() ) );
    SW_EXPECT_EQUAL( 1u, listTrack[0]._binding );
    SW_EXPECT_TRUE( listTrack[0]._name == "Transform 0" );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listTrack[1]._listChannel.size() ) );
    SW_EXPECT_TRUE( listTrack[1]._name == "Rig Slot 1" );
}

SW_TEST_CASE( SequenceModelTest, RemoveSelectedTrackClosesTheCurve )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    SW_EXPECT_FALSE( document.removeSelectedTrack() );
    (void)document.addTrack( SequenceTrackKind::Property );
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
    SequenceModel         document     = SequenceModelTestUtil::makeDocument();
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
    SequenceModel         document = SequenceModelTestUtil::makeDocument();
    const vector<float32> listKeyFrame{ 10.6f };
    const int32           nextFrame = document.findNextKeyFrame( listKeyFrame, 5 ).value();
    SW_EXPECT_EQUAL( 11, nextFrame );
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, nextFrame ).has_value() );
    SW_EXPECT_EQUAL( 11, document.findNextKeyFrame( listKeyFrame, 10 ).value() );
}

// 타임라인 밖의 키로 가면 시간 막대가 갈 수 없는 프레임에 머물렀다. 1e10 은 int32 로 바꾸기 전에 걸러야 한다.
SW_TEST_CASE( SequenceModelTest, KeyNavigationStaysInsideTheTimeline )
{
    SequenceModel document             = SequenceModelTestUtil::makeDocument();
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
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
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
    track._kind = SequenceTrackKind::Transform;
    float32 arrValue[kSequenceTransformChannelCount]{};
    SW_REQUIRE_TRUE( SequenceTimelineUtil::readTrackValues( pObject, track, arrValue, kSequenceTransformChannelCount ) );
    SW_EXPECT_NEAR( 1.0f, arrValue[0], 1e-5f );
    SW_EXPECT_NEAR( 3.0f, arrValue[2], 1e-5f );
    SW_EXPECT_NEAR( 0.2f, arrValue[4], 1e-5f );
    SW_EXPECT_NEAR( 6.0f, arrValue[8], 1e-5f );

    // 자리가 모자라면 넘겨 쓰지 않는다.
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pObject, track, arrValue, 1 ) );
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pObject, track, nullptr, kSequenceTransformChannelCount ) );
}

SW_TEST_CASE( SequenceModelTest, ReadTrackValuesFailsWithoutATargetOrComponent )
{
    GameObjectManager manager;
    GameObject*       pBare = manager.createGameObject( hashed_string( "SequenceModelTestBare" ) );
    float32           arrValue[kSequenceTransformChannelCount]{};

    SequenceKeyTrack track{};
    track._kind = SequenceTrackKind::Transform;
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( nullptr, track, arrValue, kSequenceTransformChannelCount ) );
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pBare, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind = SequenceTrackKind::RigSlot;
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pBare, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind = SequenceTrackKind::CameraLens;
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pBare, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind         = SequenceTrackKind::Property;
    track._propertyPath = "NoSuchComponent.value";
    SW_EXPECT_FALSE( SequenceTimelineUtil::readTrackValues( pBare, track, arrValue, kSequenceTransformChannelCount ) );
}

// 바인딩을 쓰는 항목 · 트랙이 남은 채 바인딩을 지우면 파일이 읽히지 않는다 - 쓰는 곳이 없을 때만 지운다. id 는 다시 쓰지 않는다.
SW_TEST_CASE( SequenceModelTest, BindingsAreRemovedOnlyWhenUnused )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    SW_EXPECT_EQUAL( 1u, document._selectedBinding );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Transform ) );
    SW_EXPECT_FALSE( document.removeBinding( 1 ) );
    SW_EXPECT_TRUE( document.renameBinding( 1, "Knight" ) );
    SW_EXPECT_FALSE( document.renameBinding( 1, "" ) );
    SW_EXPECT_TRUE( document._asset.findBinding( 1 )->_name == "Knight" );

    const uint32 extraID = document.addBinding( "Extra" );
    SW_EXPECT_EQUAL( 2u, extraID );
    SW_EXPECT_EQUAL( extraID, document._selectedBinding );
    SW_EXPECT_TRUE( document.removeBinding( extraID ) );
    SW_EXPECT_EQUAL( 1u, document._selectedBinding );
    SW_EXPECT_EQUAL( 3u, document.addBinding( "Again" ) ); // 지운 id 2 는 다시 주지 않는다

    SequenceModel empty;
    SW_EXPECT_FALSE( empty.addTrack( SequenceTrackKind::Transform ) ); // 고른 바인딩이 없으면 트랙을 더할 대상이 없다
    SW_EXPECT_FALSE( empty.addItem( SequenceItemKind::Clip, "Clip" ).has_value() );
    SW_EXPECT_TRUE( empty.isDraftingItem() );
}

// 컷 차선: 컷을 더하면 앞 컷은 그 프레임 앞에서 끝나고 새 컷은 다음 컷 앞까지다(언리얼 Camera Cut 트랙).
SW_TEST_CASE( SequenceModelTest, AddCameraCutSplitsTheLane )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMin = 0;
    document._asset._frameMax = 100;
    SW_REQUIRE_TRUE( document.addCameraCut( 0 ) == 0 );
    SW_EXPECT_EQUAL( 100, document._asset._listItem[0]._end );
    const uint32 secondCamera = document.addBinding( "CamB" );
    SW_REQUIRE_TRUE( document.addCameraCut( 40 ) == 1 );
    SW_EXPECT_EQUAL( 39, document._asset._listItem[0]._end );
    SW_EXPECT_EQUAL( 100, document._asset._listItem[1]._end );
    SW_EXPECT_EQUAL( secondCamera, document._asset._listItem[1]._binding );
    SW_EXPECT_EQUAL( 1, document._selectedItem );
    SW_REQUIRE_TRUE( document.addCameraCut( 20 ) == 2 );
    SW_EXPECT_EQUAL( 19, document._asset._listItem[0]._end );
    SW_EXPECT_EQUAL( 39, document._asset._listItem[2]._end );   // 다음 컷 앞까지
    SW_EXPECT_FALSE( document.addCameraCut( 40 ).has_value() ); // 같은 프레임에서 두 컷이 시작하지 않는다
    SW_EXPECT_FALSE( document.addCameraCut( 200 ).has_value() );

    vector<int32> listLane;
    document.collectCutLane( listLane );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listLane.size() );
    SW_EXPECT_EQUAL( 0, listLane[0] );
    SW_EXPECT_EQUAL( 2, listLane[1] );
    SW_EXPECT_EQUAL( 1, listLane[2] );
    SequenceAsset readBack;
    SW_EXPECT_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );
}

// 키 고르기: 누르면 그 키만, Ctrl 은 더하기 · 빼기, 고른 키를 다시 누르면 다중 선택을 지킨다(끌기 시작).
SW_TEST_CASE( SequenceModelTest, KeySelectionTogglesAndKeeps )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    document.selectKey( 0, 10.0f, false );
    document.selectKey( 0, 20.0f, true );
    document.selectKey( 1, 10.0f, true );
    SW_EXPECT_EQUAL( size_t{ 3 }, document.getSelectedKeys().size() );
    document.selectKey( 0, 20.0f, false ); // 고른 키 — 선택을 지킨다
    SW_EXPECT_EQUAL( size_t{ 3 }, document.getSelectedKeys().size() );
    document.selectKey( 1, 10.0f, true ); // 빼기
    SW_EXPECT_FALSE( document.isKeySelected( 1, 10.0f ) );
    document.selectKey( 0, 30.0f, false ); // 고르지 않은 키 — 그 키만
    SW_EXPECT_EQUAL( size_t{ 1 }, document.getSelectedKeys().size() );
    SW_EXPECT_TRUE( document.isKeySelected( 0, 30.2f ) );
}

// 키 끌기: 고른 키 여럿을 한 번에 옮기고, 같은 트랙의 앞 키가 아직 옮기지 않은 고른 키를 덮지 않는다. 타임라인 밖으로는 옮기지 않는다.
SW_TEST_CASE( SequenceModelTest, MoveSelectedKeysKeepsEveryKey )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMax = 100;
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SequenceKeyTrack& first  = document._asset._listTrack[0];
    SequenceKeyTrack& second = document._asset._listTrack[1];
    (void)first.setChannelKey( 0, 10.0f, 1.0f );
    (void)first.setChannelKey( 0, 20.0f, 2.0f );
    (void)second.setChannelKey( 0, 10.0f, 3.0f );
    document.selectKey( 0, 10.0f, false );
    document.selectKey( 0, 20.0f, true );
    document.selectKey( 1, 10.0f, true );

    SW_EXPECT_TRUE( document.moveSelectedKeys( 10 ) );
    SW_EXPECT_NEAR( 1.0f, first.evaluateChannel( 0, 20.0f ).value(), 1e-5f ); // 10 → 20, 20 → 30 이 서로 덮지 않았다
    SW_EXPECT_NEAR( 2.0f, first.evaluateChannel( 0, 30.0f ).value(), 1e-5f );
    SW_EXPECT_EQUAL( size_t{ 2 }, first._listChannel[0]._listKey.size() );
    SW_EXPECT_TRUE( second.hasKeyAt( 20.0f ) );
    SW_EXPECT_TRUE( document.isKeySelected( 0, 30.0f ) );

    SW_EXPECT_FALSE( document.moveSelectedKeys( 90 ) ); // 30 + 90 은 타임라인 밖이다
    SW_EXPECT_TRUE( first.hasKeyAt( 30.0f ) );
    SW_EXPECT_EQUAL( 2, document.computeFrameDelta( 20.0f, 1000.0f ) ); // 1000 px 에 100 프레임 — 20 px 는 2 프레임

    SW_EXPECT_EQUAL( 3u, document.removeSelectedKeys() );
    SW_EXPECT_TRUE( first._listChannel[0]._listKey.empty() );
    SW_EXPECT_TRUE( document.getSelectedKeys().empty() );
}

// 렌즈 트랙의 화각은 도로 보이고 라디안으로 저장한다(인스펙터의 화각과 같은 규칙).
SW_TEST_CASE( SequenceModelTest, LensFieldOfViewShowsDegrees )
{
    SequenceKeyTrack lens{};
    lens._kind = SequenceTrackKind::CameraLens;
    lens.fitChannelsToKind();
    SW_EXPECT_TRUE( SequenceModel::isRotationChannel( lens, 0 ) );
    SW_EXPECT_FALSE( SequenceModel::isRotationChannel( lens, 1 ) );
    SW_EXPECT_TRUE( SequenceModel::setChannelDisplayValue( lens, 0, 0.0f, 45.0f ) );
    SW_EXPECT_NEAR( MathUtil::toRadian( 45.0f ), lens.evaluateChannel( 0, 0.0f ).value(), 1e-4f );
}

// 트랙 재정렬: 고른 트랙 · 커브 편집기 · 고른 키가 옮긴 뒤에도 같은 트랙을 가리킨다.
SW_TEST_CASE( SequenceModelTest, MoveTrackKeepsSelectionOnTheSameTrack )
{
    SequenceModel document = SequenceModelTestUtil::makeDocument();
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Transform ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::CameraLens ) );
    document._selectedTrack = 2;
    document._curveTrack    = 0;
    document.selectKey( 1, 5.0f, false );

    SW_EXPECT_TRUE( document.moveTrack( 2, 0 ) );
    SW_EXPECT_TRUE( document._asset._listTrack[0]._kind == SequenceTrackKind::CameraLens );
    SW_EXPECT_TRUE( document._asset._listTrack[1]._kind == SequenceTrackKind::Transform );
    SW_EXPECT_EQUAL( 0, document._selectedTrack );
    SW_EXPECT_EQUAL( 1, document._curveTrack );
    SW_EXPECT_TRUE( document.isKeySelected( 2, 5.0f ) );

    SW_EXPECT_TRUE( document.moveTrack( 0, 2 ) );
    SW_EXPECT_TRUE( document._asset._listTrack[2]._kind == SequenceTrackKind::CameraLens );
    SW_EXPECT_EQUAL( 2, document._selectedTrack );
    SW_EXPECT_FALSE( document.moveTrack( 1, 1 ) );
    SW_EXPECT_FALSE( document.moveTrack( 0, 3 ) );
}

// 키 복사 · 붙여넣기: 여러 트랙의 키를 맨 위 트랙 · 맨 앞 프레임 기준 거리로 붙이고, 채널 수가 다른 트랙에는 붙이지 않는다.
SW_TEST_CASE( SequenceModelTest, PasteKeysKeepsTrackAndFrameOffsets )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMax = 100;
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Transform ) );
    (void)document._asset._listTrack[0].setChannelKey( 0, 10.0f, 1.0f );
    (void)document._asset._listTrack[1].setChannelKey( 0, 14.0f, 2.0f );
    document.selectKey( 0, 10.0f, false );
    document.selectKey( 1, 14.0f, true );
    SW_REQUIRE_EQUAL( 2u, document.copySelectedKeys() );
    SW_EXPECT_TRUE( document.hasCopiedKeys() );

    document._selectedTrack = 0;
    SW_EXPECT_EQUAL( 2u, document.pasteKeys( 50 ) );
    SW_EXPECT_NEAR( 1.0f, document._asset._listTrack[0].evaluateChannel( 0, 50.0f ).value(), 1e-5f );
    SW_EXPECT_NEAR( 2.0f, document._asset._listTrack[1].evaluateChannel( 0, 54.0f ).value(), 1e-5f );
    SW_EXPECT_EQUAL( size_t{ 2 }, document.getSelectedKeys().size() );

    // 둘째 키는 트랜스폼 트랙(채널 9)으로 가므로 건너뛴다. 타임라인 밖도 건너뛴다.
    document._selectedTrack = 1;
    SW_EXPECT_EQUAL( 1u, document.pasteKeys( 20 ) );
    SW_EXPECT_TRUE( document._asset._listTrack[2]._listChannel[0]._listKey.empty() );
    document._selectedTrack = 0;
    SW_EXPECT_EQUAL( 0u, document.pasteKeys( 200 ) );
}

// 항목 복사 · 붙여넣기: 길이를 지키고 새 이름으로 붙인다. 스폰은 바인딩을 둘이 채울 수 없어 붙이지 않는다.
SW_TEST_CASE( SequenceModelTest, PasteItemShiftsTheSpanAndRenames )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMax = 100;
    SequenceTrackItem clip    = SequenceModelTestUtil::makeFilledItem( SequenceItemKind::Clip );
    clip._name                = "Walk";
    clip._start               = 0;
    clip._end                 = 20;
    document._asset._listItem.push_back( clip );
    SW_EXPECT_FALSE( document.copySelectedItem() );
    document._selectedItem = 0;
    SW_REQUIRE_TRUE( document.copySelectedItem() );
    SW_REQUIRE_TRUE( document.pasteItem( 90 ) == 1 );
    const SequenceTrackItem& copy = document._asset._listItem[1];
    SW_EXPECT_EQUAL( 90, copy._start );
    SW_EXPECT_EQUAL( 100, copy._end ); // 끝은 타임라인 안으로 자른다
    SW_EXPECT_TRUE( copy._name != "Walk" );
    SW_EXPECT_EQUAL( 1, document._selectedItem );
    SW_EXPECT_FALSE( document.pasteItem( 150 ).has_value() );

    document._asset._listItem.push_back( SequenceModelTestUtil::makeFilledItem( SequenceItemKind::Spawn ) );
    document._selectedItem = 2;
    SW_REQUIRE_TRUE( document.copySelectedItem() );
    SW_EXPECT_FALSE( document.pasteItem( 10 ).has_value() );
}

// 보기 확대 · 스크롤: 포인터 자리의 프레임이 남고, 타임라인 밖을 보이지 않으며, 전체에 닿으면 맞춘 상태로 돌아간다.
SW_TEST_CASE( SequenceModelTest, ZoomKeepsTheAnchorFrameAndStaysInside )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMin = 0;
    document._asset._frameMax = 100;
    SW_EXPECT_TRUE( document.isViewFitted() );
    document.zoomView( 0.5f, 0.5f );
    SW_EXPECT_FALSE( document.isViewFitted() );
    SW_EXPECT_EQUAL( 50, document.getViewFrameSpan() );
    SW_EXPECT_EQUAL( 25, document.getViewFirstFrame() );
    SW_EXPECT_EQUAL( 50, document.findFrameAtRatio( 0.5f ) ); // 가운데 프레임이 그대로
    SW_EXPECT_NEAR( 0.5f, document.computeRatioAtFrame( 50.0f ), 1e-5f );
    SW_EXPECT_EQUAL( 1, document.computeFrameDelta( 10.0f, 500.0f ) ); // 500 px 에 50 프레임

    document.scrollView( 1000 );
    SW_EXPECT_EQUAL( 50, document.getViewFirstFrame() ); // 끝에서 멈춘다
    document.scrollView( -1000 );
    SW_EXPECT_EQUAL( 0, document.getViewFirstFrame() );

    document.zoomView( 0.01f, 0.0f );
    SW_EXPECT_EQUAL( SequenceModel::kMinViewFrameSpan, document.getViewFrameSpan() );

    document.zoomView( 1000.0f, 0.5f );
    SW_EXPECT_TRUE( document.isViewFitted() );
    SW_EXPECT_EQUAL( 0, document.getViewFirstFrame() );

    // 타임라인이 줄면 보이는 범위가 따라 줄어든다.
    document.zoomView( 0.5f, 1.0f );
    document._asset._frameMax = 10;
    SW_EXPECT_TRUE( document.getViewFirstFrame() + document.getViewFrameSpan() <= 10 );
}

// 오류 배지: 파일이 거절하는 것(바인딩 · 채널 수)부터, 그다음 재생이 닿지 않는 것(빈 대상 경로 · 타임라인 밖 키)을 알린다.
SW_TEST_CASE( SequenceModelTest, TrackIssuesFollowTheKindTable )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMax = 100;
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Transform ) );
    SW_REQUIRE_TRUE( document.addTrack( SequenceTrackKind::Property ) );
    SW_EXPECT_TRUE( document.findTrackIssue( 0 ) == SequenceTrackIssue::None );
    SW_EXPECT_TRUE( document.findTrackIssue( 1 ) == SequenceTrackIssue::MissingPropertyPath );
    document._asset._listTrack[1]._propertyPath = "LightComponent._intensity";
    SW_EXPECT_TRUE( document.findTrackIssue( 1 ) == SequenceTrackIssue::None );
    SW_EXPECT_EQUAL( 0u, document.countTrackIssues() );

    (void)document._asset._listTrack[0].setChannelKey( 0, 150.0f, 1.0f );
    SW_EXPECT_TRUE( document.findTrackIssue( 0 ) == SequenceTrackIssue::KeysOutsideTimeline );
    document._asset._listTrack[0]._listChannel.pop_back();
    SW_EXPECT_TRUE( document.findTrackIssue( 0 ) == SequenceTrackIssue::ChannelCount );
    document._asset._listTrack[1]._binding = 99;
    SW_EXPECT_TRUE( document.findTrackIssue( 1 ) == SequenceTrackIssue::MissingBinding );
    SW_EXPECT_EQUAL( 2u, document.countTrackIssues() );
    SW_EXPECT_TRUE( document.findTrackIssue( 5 ) == SequenceTrackIssue::None );

    // 종류 표의 줄마다 채널 수가 `getChannelCount` 와 같다(새 종류는 표 한 줄).
    for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( SequenceTrackKind::Count ); ++kindIndex )
    {
        const SequenceTrackKindInfo* pInfo = SequenceAsset::findTrackKindInfo( static_cast<SequenceTrackKind>( kindIndex ) );
        SW_REQUIRE_TRUE( pInfo != nullptr );
        SW_EXPECT_EQUAL( pInfo->_channelCount, SequenceKeyTrack::getChannelCount( pInfo->_kind ) );
    }
    SW_EXPECT_TRUE( SequenceAsset::findTrackKindInfo( SequenceTrackKind::Count ) == nullptr );
}

// 쇼트 구성: 컷을 나누면 같은 카메라의 두 조각, 지우면 앞 컷이 빈 자리를 덮는다, 카메라를 바꿀 수 있다.
SW_TEST_CASE( SequenceModelTest, ShotToolsSplitRemoveAndRebindCuts )
{
    SequenceModel document    = SequenceModelTestUtil::makeDocument();
    document._asset._frameMin = 0;
    document._asset._frameMax = 100;
    SW_REQUIRE_TRUE( document.addCameraCut( 0 ) == 0 );
    const uint32 secondCamera = document.addBinding( "CamB" );
    SW_REQUIRE_TRUE( document.addCameraCut( 60 ) == 1 );

    SW_REQUIRE_TRUE( document.splitCameraCut( 30 ) == 2 );
    SW_EXPECT_EQUAL( 29, document._asset._listItem[0]._end );
    SW_EXPECT_EQUAL( 30, document._asset._listItem[2]._start );
    SW_EXPECT_EQUAL( 59, document._asset._listItem[2]._end );
    SW_EXPECT_EQUAL( document._asset._listItem[0]._binding, document._asset._listItem[2]._binding );
    SW_EXPECT_FALSE( document.splitCameraCut( 30 ).has_value() ); // 컷의 첫 프레임 — 나눌 것이 없다

    SW_EXPECT_TRUE( document.setCutCamera( 2, secondCamera ) );
    SW_EXPECT_EQUAL( secondCamera, document._asset._listItem[2]._binding );
    SW_EXPECT_FALSE( document.setCutCamera( 2, 99 ) );

    SW_EXPECT_TRUE( document.removeCameraCut( 2 ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, document._asset._listItem.size() );
    SW_EXPECT_EQUAL( 59, document._asset._listItem[0]._end ); // 앞 컷이 빈 자리를 덮는다

    document._asset._listItem.push_back( SequenceModelTestUtil::makeFilledItem( SequenceItemKind::Event ) );
    SW_EXPECT_FALSE( document.removeCameraCut( 2 ) ); // 컷이 아니다
    SW_EXPECT_FALSE( document.setCutCamera( 2, secondCamera ) );
    SequenceAsset readBack;
    SW_EXPECT_TRUE( readBack.loadFromXMLText( document._asset.toXML() ) );
}
