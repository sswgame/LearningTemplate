#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/SequenceDocument.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <initializer_list>

using namespace sw;
using namespace sw::editor;

namespace
{
    struct SequenceDocumentTestUtil
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
    };
} // namespace

// 패널이 칸을 하나씩 옮기던 때는 옮기지 않은 카메라 블렌드 · 프리롤이 저장 · 되돌리기마다 기본값으로 돌아갔다.
SW_TEST_CASE( SequenceDocumentTest, RestoreKeepsEveryAssetField )
{
    SequenceAsset asset;
    asset._cameraBlendInSeconds  = 0.5f;
    asset._cameraBlendOutSeconds = 0.25f;
    asset._cameraPrerollFrames   = 7;
    SequenceDocument document;
    document.restoreAsset( asset );
    SW_EXPECT_NEAR( 0.5f, document._asset._cameraBlendInSeconds, 1e-5f );
    SW_EXPECT_NEAR( 0.25f, document._asset._cameraBlendOutSeconds, 1e-5f );
    SW_EXPECT_EQUAL( 7, document._asset._cameraPrerollFrames );
    SW_EXPECT_TRUE( document._asset.toXML() == asset.toXML() );
}

// 되돌리기가 항목을 줄이면 고른 번호가 없는 항목을 가리켜 타임라인이 빈 줄을 강조했다.
SW_TEST_CASE( SequenceDocumentTest, RestoreClampsTheSelection )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, ResetSelectionPicksTheFirstTrack )
{
    SequenceDocument document;
    document.addTrack( SequenceTrackKind::Transform, "" );
    document.addTrack( SequenceTrackKind::Property, "" );
    document._selectedItem = 3;
    document._curveTrack   = 1;
    document.resetSelection();
    SW_EXPECT_EQUAL( -1, document._selectedItem );
    SW_EXPECT_EQUAL( 0, document._selectedTrack );
    SW_EXPECT_EQUAL( -1, document._curveTrack );
}

// 스폰 · 외형 · 시나리오는 패널에 없는 칸이 필수라, 만들면 저장한 파일과 되돌리기 스냅숏이 읽기 오류가 됐다.
SW_TEST_CASE( SequenceDocumentTest, EveryAddableKindSurvivesSaveAndLoad )
{
    SequenceDocument document;
    const uint32     addableCount = SequenceDocument::getAddableItemKindCount();
    SW_REQUIRE_TRUE( addableCount > 0 );
    for ( uint32 addableIndex = 0; addableIndex < addableCount; ++addableIndex )
    {
        SW_EXPECT_TRUE( document.addItem( SequenceDocument::getAddableItemKind( addableIndex ), "Kind" ) >= 0 );
    }
    SW_EXPECT_TRUE( document.canReadBack() );
    SW_EXPECT_TRUE( SequenceDocument::getAddableItemKind( addableCount ) == SequenceItemKind::Count );
}

SW_TEST_CASE( SequenceDocumentTest, KindsThatNeedMissingFieldsAreNotAdded )
{
    SequenceDocument document;
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Spawn, "Spawn" ) );
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Appearance, "Appearance" ) );
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Scenario, "Scenario" ) );
    SW_EXPECT_EQUAL( -1, document.addItem( SequenceItemKind::Count, "Count" ) );
    SW_EXPECT_TRUE( document._asset._listItem.empty() );
}

// 쓰기는 검사하지 않으므로, 읽지 못할 문서를 저장 전에 가려야 멀쩡한 파일을 덮지 않는다.
SW_TEST_CASE( SequenceDocumentTest, IncompleteSpawnItemCannotBeReadBack )
{
    SW_EXPECT_LOG( "SequenceAsset.SequenceSpawnItemNeeds" );
    SequenceDocument  document;
    SequenceTrackItem item{};
    item._kind = SequenceItemKind::Spawn;
    document._asset._listItem.push_back( item );
    SW_EXPECT_FALSE( document.canReadBack() );
}

// 위젯이 주던 고정 길이 10 은 짧은 타임라인을 넘어 띠 밖으로 나갔다.
SW_TEST_CASE( SequenceDocumentTest, NewItemStaysInsideTheTimeline )
{
    SequenceDocument document;
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
SW_TEST_CASE( SequenceDocumentTest, NewNamesDoNotCollideAfterRemoval )
{
    SequenceDocument document;
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
SW_TEST_CASE( SequenceDocumentTest, RemoveItemKeepsTheSelectedItem )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, DuplicateCopiesTheItemWithANewName )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, AddTrackFitsChannelsAndSelects )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, RemoveSelectedTrackClosesTheCurve )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, KeyNavigationSkipsTheKeyAtTheCurrentFrame )
{
    SequenceDocument      document;
    const vector<float32> listKeyFrame = SequenceDocumentTestUtil::makeKeyFrames( { 10.0f, 20.0f, 30.0f } );
    SW_EXPECT_EQUAL( 10, document.findPreviousKeyFrame( listKeyFrame, 20 ).value() );
    SW_EXPECT_EQUAL( 30, document.findNextKeyFrame( listKeyFrame, 20 ).value() );
    SW_EXPECT_EQUAL( 20, document.findNextKeyFrame( listKeyFrame, 15 ).value() );
    SW_EXPECT_FALSE( document.findPreviousKeyFrame( listKeyFrame, 10 ).has_value() );
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, 30 ).has_value() );
    SW_EXPECT_FALSE( document.findNextKeyFrame( vector<float32>{}, 0 ).has_value() );
}

// 커브 편집기로 소수 프레임에 놓은 키도 정수 프레임으로 가고, 이동은 지금 자리에 머물지 않는다.
SW_TEST_CASE( SequenceDocumentTest, KeyNavigationRoundsFractionalKeys )
{
    SequenceDocument      document;
    const vector<float32> listKeyFrame{ 10.6f };
    const int32           nextFrame = document.findNextKeyFrame( listKeyFrame, 5 ).value();
    SW_EXPECT_EQUAL( 11, nextFrame );
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, nextFrame ).has_value() );
    SW_EXPECT_EQUAL( 11, document.findNextKeyFrame( listKeyFrame, 10 ).value() );
}

// 타임라인 밖의 키로 가면 시간 막대가 갈 수 없는 프레임에 머물렀다. 1e10 은 int32 로 바꾸기 전에 걸러야 한다.
SW_TEST_CASE( SequenceDocumentTest, KeyNavigationStaysInsideTheTimeline )
{
    SequenceDocument document;
    document._asset._frameMin          = 0;
    document._asset._frameMax          = 100;
    const vector<float32> listKeyFrame = { -5.0f, 50.0f, 150.0f, 1e10f };
    SW_EXPECT_FALSE( document.findNextKeyFrame( listKeyFrame, 60 ).has_value() );
    SW_EXPECT_FALSE( document.findPreviousKeyFrame( listKeyFrame, 40 ).has_value() );
    SW_EXPECT_EQUAL( 50, document.findPreviousKeyFrame( listKeyFrame, 100 ).value() );
    SW_EXPECT_EQUAL( 50, document.findNextKeyFrame( listKeyFrame, 0 ).value() );
}

SW_TEST_CASE( SequenceDocumentTest, RatioAndFrameAreInverse )
{
    SequenceDocument document;
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

SW_TEST_CASE( SequenceDocumentTest, RotationChannelsShowDegreesAndStoreRadians )
{
    SequenceKeyTrack track{};
    track._kind = SequenceTrackKind::Transform;
    track.fitChannelsToKind();
    SW_EXPECT_FALSE( SequenceDocument::isRotationChannel( track, 2 ) );
    SW_EXPECT_TRUE( SequenceDocument::isRotationChannel( track, 3 ) );
    SW_EXPECT_TRUE( SequenceDocument::isRotationChannel( track, 5 ) );
    SW_EXPECT_FALSE( SequenceDocument::isRotationChannel( track, 6 ) );

    SW_EXPECT_TRUE( SequenceDocument::setChannelDisplayValue( track, 3, 4.0f, 90.0f ) );
    SW_EXPECT_NEAR( MathUtil::toRadian( 90.0f ), track.evaluateChannel( 3, 4.0f ).value(), 1e-4f );
    SW_EXPECT_NEAR( 90.0f, SequenceDocument::computeChannelDisplayValue( track, 3, 4.0f, 0.0f ), 1e-3f );
    SW_EXPECT_TRUE( SequenceDocument::setChannelDisplayValue( track, 0, 4.0f, 90.0f ) );
    SW_EXPECT_NEAR( 90.0f, track.evaluateChannel( 0, 4.0f ).value(), 1e-4f );

    // 키가 없는 채널은 대상의 지금 값을 보인다.
    SW_EXPECT_NEAR( 7.0f, SequenceDocument::computeChannelDisplayValue( track, 1, 4.0f, 7.0f ), 1e-5f );
    SW_EXPECT_NEAR( 180.0f, SequenceDocument::computeChannelDisplayValue( track, 4, 4.0f, MathUtil::kPi ), 1e-3f );
    SW_EXPECT_FALSE( SequenceDocument::setChannelDisplayValue( track, kSequenceTransformChannelCount, 4.0f, 1.0f ) );

    // 프로퍼티 트랙의 채널 3 은 없고, 있어도 회전이 아니다.
    SequenceKeyTrack propertyTrack{};
    propertyTrack._kind = SequenceTrackKind::Property;
    SW_EXPECT_FALSE( SequenceDocument::isRotationChannel( propertyTrack, 3 ) );
}

SW_TEST_CASE( SequenceDocumentTest, ReadTrackValuesReadsTheTransformInChannelOrder )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "SequenceDocumentTestHero" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SceneComponent* pScene = pObject->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    pScene->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );
    pScene->setLocalRotation( float3{ 0.1f, 0.2f, 0.3f } );
    pScene->setLocalScale( float3{ 4.0f, 5.0f, 6.0f } );

    SequenceKeyTrack track{};
    track._kind         = SequenceTrackKind::Transform;
    track._targetObject = "SequenceDocumentTestHero";
    float32 arrValue[kSequenceTransformChannelCount]{};
    SW_REQUIRE_TRUE( SequenceDocument::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );
    SW_EXPECT_NEAR( 1.0f, arrValue[0], 1e-5f );
    SW_EXPECT_NEAR( 3.0f, arrValue[2], 1e-5f );
    SW_EXPECT_NEAR( 0.2f, arrValue[4], 1e-5f );
    SW_EXPECT_NEAR( 6.0f, arrValue[8], 1e-5f );

    // 자리가 모자라면 넘겨 쓰지 않는다.
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, arrValue, 1 ) );
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, nullptr, kSequenceTransformChannelCount ) );
}

SW_TEST_CASE( SequenceDocumentTest, ReadTrackValuesFailsWithoutATargetOrComponent )
{
    GameObjectManager manager;
    (void)manager.createGameObject( hashed_string( "SequenceDocumentTestBare" ) );
    float32 arrValue[kSequenceTransformChannelCount]{};

    SequenceKeyTrack track{};
    track._kind         = SequenceTrackKind::Transform;
    track._targetObject = "SequenceDocumentTestMissing";
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._targetObject = "SequenceDocumentTestBare";
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind = SequenceTrackKind::RigSlot;
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );

    track._kind         = SequenceTrackKind::Property;
    track._propertyPath = "NoSuchComponent.value";
    SW_EXPECT_FALSE( SequenceDocument::readTrackValues( &manager, track, arrValue, kSequenceTransformChannelCount ) );
}
