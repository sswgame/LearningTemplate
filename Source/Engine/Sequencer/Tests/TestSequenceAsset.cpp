// 시퀀스 애셋(오브젝트 없이) — 종류 표, 칸 규칙, 바인딩 id, 키 트랙 키 찍기 · 지우기 · 옮기기, 읽기 실패 때 남는 것 없음, 프레임 번호 한도.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

using namespace sw;

/**
 * @brief [SequencerTest] 종류 표가 종류마다 한 줄이고, 표에 없는 정수는 nullptr 이다
 */
SW_TEST_CASE( SequencerTest, ItemKindTableCoversEveryKind )
{
    for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( SequenceItemKind::Count ); ++kindIndex )
    {
        const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( static_cast<SequenceItemKind>( kindIndex ) );
        SW_REQUIRE_NOT_NULL( pInfo );
        SW_EXPECT_EQUAL( kindIndex, static_cast<int32>( pInfo->_kind ) );
    }
    // 에디터 트랙 이름은 리플렉션 enum 이름이다.
    const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
    SW_EXPECT_EQUAL( string( "Clip" ), string( registry.enumToString( SequenceItemKind::Clip ) ) );
    SW_EXPECT_EQUAL( string( "Event" ), string( registry.enumToString( SequenceItemKind::Event ) ) );
    SW_EXPECT_EQUAL( string( "CameraCut" ), string( registry.enumToString( SequenceItemKind::CameraCut ) ) );
    SW_EXPECT_EQUAL( string( "Subtitle" ), string( registry.enumToString( SequenceItemKind::Subtitle ) ) );
    SW_EXPECT_NULL( SequenceAsset::findItemKindInfo( SequenceItemKind::Count ) );
    SW_EXPECT_NULL( SequenceAsset::findItemKindInfo( static_cast<SequenceItemKind>( -1 ) ) );
}

/**
 * @brief [SequencerTest] 표의 모든 줄은 필수 칸이 허용 칸 안이고, 구간 처리기가 있는 종류는 애니메이션 · 소리 · 자막이다
 */
SW_TEST_CASE( SequencerTest, KindTableRowsAreConsistent )
{
    for ( uint32 kindIndex = 0; kindIndex < SequenceAsset::getItemKindCount(); ++kindIndex )
    {
        const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( static_cast<SequenceItemKind>( kindIndex ) );
        SW_REQUIRE_NOT_NULL( pInfo );
        SW_EXPECT_EQUAL( 0, static_cast<int32>( pInfo->_requiredFields & ~pInfo->_allowedFields ) );
        const bool bSpanKind = pInfo->_kind == SequenceItemKind::Animation || pInfo->_kind == SequenceItemKind::Audio || pInfo->_kind == SequenceItemKind::Subtitle;
        SW_EXPECT_EQUAL( bSpanKind, pInfo->_pSpanHandler != nullptr );
    }
}

/**
 * @brief [SequencerTest] 같은 프레임에 키를 다시 찍으면 값만 바뀌고, 키 프레임은 채널을 겹쳐 하나로 세며, 지우면 모든 채널에서 빠진다
 */
SW_TEST_CASE( SequencerTest, KeyTrackSetKeyReplacesAndRemovesAcrossChannels )
{
    SequenceKeyTrack track{};
    track._kind = SequenceTrackKind::Transform;
    track.fitChannelsToKind();
    SW_REQUIRE_EQUAL( size_t( kSequenceTransformChannelCount ), track._listChannel.size() );

    float32 arrValue[kSequenceTransformChannelCount] = { 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    track.setKey( 10.0f, arrValue, kSequenceTransformChannelCount );
    arrValue[0] = 5.0f;
    track.setKey( 10.2f, arrValue, kSequenceTransformChannelCount ); // 반 프레임 안 — 같은 키
    (void)track.setChannelKey( 1, 30.0f, 7.0f );                     // 채널 하나에만

    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[0]._listKey.size() );
    SW_EXPECT_NEAR( 5.0f, track.evaluateChannel( 0, 10.0f ).value(), 0.0001f );
    vector<float32> listFrame;
    track.collectKeyFrames( listFrame );
    SW_REQUIRE_EQUAL( size_t( 2 ), listFrame.size() );
    SW_EXPECT_TRUE( track.hasKeyAt( 30.0f ) );
    SW_EXPECT_TRUE( track.findChannelKey( 0, 30.0f ).has_value() == false );

    SW_EXPECT_TRUE( track.removeKeysAt( 10.0f ) );
    SW_EXPECT_TRUE( track._listChannel[0]._listKey.empty() );
    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[1]._listKey.size() );
    SW_EXPECT_FALSE( track.removeKeysAt( 10.0f ) );
}

/**
 * @brief [SequencerTest] 키를 옮기면 모든 채널의 그 프레임 키가 함께 옮겨지고, 놓은 자리의 키는 덮인다(렌즈 트랙 — 채널 다섯)
 */
SW_TEST_CASE( SequencerTest, MoveKeysCarriesEveryChannel )
{
    SequenceKeyTrack track{};
    track._kind = SequenceTrackKind::CameraLens;
    track.fitChannelsToKind();
    SW_REQUIRE_EQUAL( size_t( kSequenceLensChannelCount ), track._listChannel.size() );
    const float32 arrValue[kSequenceLensChannelCount] = { 0.8f, 5.0f, 2.8f, 0.0f, 0.0f };
    track.setKey( 10.0f, arrValue, kSequenceLensChannelCount );
    (void)track.setChannelKey( 1, 20.0f, 9.0f );

    SW_EXPECT_TRUE( track.moveKeysAt( 10.0f, 20.0f ) );
    SW_EXPECT_FALSE( track.hasKeyAt( 10.0f ) );
    SW_EXPECT_NEAR( 5.0f, track.evaluateChannel( 1, 20.0f ).value(), 1e-4f ); // 놓은 자리의 9 를 덮었다
    SW_EXPECT_NEAR( 2.8f, track.evaluateChannel( 2, 20.0f ).value(), 1e-4f );
    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[1]._listKey.size() );
    SW_EXPECT_FALSE( track.moveKeysAt( 33.0f, 40.0f ) );
    SW_EXPECT_TRUE( string( track.getChannelName( 2 ) ) == "Aperture" );
}

/**
 * @brief [SequencerTest] 파싱이 실패하면 애셋에 반쯤 남은 상태가 없다
 * @details loadFromXMLText 이 _listItem 만 비우고 실패하면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아, 트랙 없는 옛 시퀀스가 새 시퀀스인 척한다.
 */
SW_TEST_CASE( SequencerTest, FailedParseLeavesNothingBehind )
{
    SW_EXPECT_LOG( "Xml.ParseError" );
    SW_EXPECT_LOG( "Config.NotXMLElement" );
    SequenceAsset asset;
    asset._frameMin = 5;
    asset._frameMax = 30;
    asset._note     = "previous sequence";
    asset._listItem.push_back( SequenceTrackItem{} );

    SW_EXPECT_FALSE( asset.loadFromXMLText( "<SequenceAsset _frameMin=\"0\" this is not xml" ) );
    SW_EXPECT_TRUE( asset._listItem.empty() );
    SW_EXPECT_TRUE( asset._note.empty() );
    SW_EXPECT_EQUAL( 0, asset._frameMin );
    SW_EXPECT_EQUAL( 100, asset._frameMax );
}

/**
 * @brief [SequencerTest] 파일이 준 프레임 번호가 `kSequenceFrameLimit` 밖이거나 끝이 시작보다 앞이면 읽지 않는다
 * @details 프레임 번호는 파일에서 온다. int32 최대값이 그대로 들어오면 `_frameMax - _frameMin` 과 트랙의 `_end - _start` 가 부호 있는 넘침이 된다.
 *          잘린 값은 파일이 말한 값이 아니므로 읽기 오류로 멈춘다.
 */
SW_TEST_CASE( SequencerTest, OutOfRangeFrameNumbersAreRejected )
{
    SW_EXPECT_LOG( "Sequence.FrameOutOfRange" );
    SW_EXPECT_LOG( "Sequence.FrameOrder" );
    SequenceAsset asset;
    SW_EXPECT_FALSE( asset.loadFromXMLText( R"(<SequenceAsset _frameMin="2147483647" _frameMax="0" _whenFinished="RestoreState" />)" ) );
    SW_EXPECT_FALSE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Wide" _start="2147483647" _end="-2147483648" _kind="Event" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_FALSE( asset.loadFromXMLText( R"(<SequenceAsset _frameMin="10" _frameMax="10" _whenFinished="RestoreState" />)" ) );
    SW_EXPECT_TRUE( asset._listItem.empty() );
    SW_EXPECT_EQUAL( 0, asset._frameMin );
    SW_EXPECT_EQUAL( 100, asset._frameMax );
}

/**
 * @brief [SequencerTest] 끝내기 계약(`_whenFinished`)은 파일마다 적는다 — 빠지면 읽기 오류다(계약을 고르지 않은 시퀀스가 없다)
 */
SW_TEST_CASE( SequencerTest, FinishPolicyIsRequired )
{
    SW_EXPECT_LOG( "Config.KeyMissing" );
    SequenceAsset asset;
    SW_EXPECT_FALSE( asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" />)" ) );
    SW_EXPECT_TRUE( asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="KeepState" />)" ) );
    SW_EXPECT_TRUE( asset._whenFinished == SequenceFinishPolicy::KeepState );
}

// 칸의 규칙은 종류 표 한 곳이다 - 파일 읽기와 에디터가 함께 본다. 종류마다 어떤 칸이 필수 · 허용인지를 못 박는다.
SW_TEST_CASE( SequencerTest, FieldIssueFollowsTheItemKind )
{
    SequenceTrackItem item{};
    SequenceItemField field = SequenceItemField::Count;
    SW_EXPECT_TRUE( item.findFieldIssue( &field ) == SequenceItemFieldIssue::MissingField ); // 클립은 대상 바인딩이 필수다
    SW_EXPECT_TRUE( field == SequenceItemField::Binding );
    item._binding = 1;
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );

    item._scenario = "a.scenario.xml";
    SW_EXPECT_TRUE( item.findFieldIssue( &field ) == SequenceItemFieldIssue::FieldNotAllowed );
    SW_EXPECT_TRUE( field == SequenceItemField::Scenario );
    item._kind = SequenceItemKind::Scenario;
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );
    item._binding = kSequenceNoBinding; // 시나리오는 대상이 없어도 된다
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );
    item._scenario.clear();
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::MissingField );

    item._kind    = SequenceItemKind::Spawn;
    item._binding = 2;
    SW_EXPECT_TRUE( item.findFieldIssue( &field ) == SequenceItemFieldIssue::MissingField );
    SW_EXPECT_TRUE( field == SequenceItemField::Prefab );
    item._prefab = "a.prefab.xml";
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );

    item._kind = SequenceItemKind::Appearance;
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::FieldNotAllowed ); // 프리팹이 남았다
    item._prefab.clear();
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::MissingField );
    item._preset = "wet";
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );

    item._kind = SequenceItemKind::Subtitle;
    item._preset.clear();
    SW_EXPECT_TRUE( item.findFieldIssue( &field ) == SequenceItemFieldIssue::MissingField );
    SW_EXPECT_TRUE( field == SequenceItemField::Text );
    item._text = "Cinematic.Line1";
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::None );

    item._kind = SequenceItemKind::Count;
    SW_EXPECT_TRUE( item.findFieldIssue() == SequenceItemFieldIssue::UnknownKind );
}

/**
 * @brief [SequencerTest] 시나리오 항목은 `_scenario` 가 있어야 읽히고, 다른 종류는 `_scenario` 를 가질 수 없다
 * @details 시나리오 항목은 시작 프레임을 지날 때 `ScenarioPlayback::play` 로 재생한다(`SequencePlayerComponent`). 빈 칸이면 연출이 조용히 아무것도 부르지 않으므로 읽기 오류다.
 */
SW_TEST_CASE( SequencerTest, ScenarioItemNeedsItsScenario )
{
    const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( SequenceItemKind::Scenario );
    SW_REQUIRE_NOT_NULL( pInfo );
    SW_EXPECT_TRUE( pInfo->_bPlaysScenario );
    SW_EXPECT_TRUE( pInfo->_bFiresOnCross );
    SW_EXPECT_FALSE( SequenceAsset::findItemKindInfo( SequenceItemKind::Event )->_bPlaysScenario );

    SequenceAsset asset;
    SW_EXPECT_TRUE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Demo" _scenario="game/empty/automation/loadscene.scenario.xml" _start="3" _end="3" _kind="Scenario" /></_listItem></SequenceAsset>)" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, asset._listItem.size() );
    SW_EXPECT_EQUAL( string( "game/empty/automation/loadscene.scenario.xml" ), asset._listItem[0]._scenario );

    SW_EXPECT_LOG( "SequenceAsset.SequenceItemField" );
    SW_EXPECT_FALSE(
        asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Empty" _start="3" _end="3" _kind="Scenario" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_FALSE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Stray" _scenario="a.scenario.xml" _start="3" _end="3" _kind="Event" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_TRUE( asset._listItem.empty() );
}

/**
 * @brief [SequencerTest] 바인딩 id 는 유일하고 `_nextBindingID` 아래이며, 항목 · 트랙이 가리키는 id 는 시퀀스에 있어야 읽힌다. 옛 이름 칸(`_targetObject`)은 모르는 속성이다
 */
SW_TEST_CASE( SequencerTest, BindingIdsAreCheckedOnRead )
{
    SequenceAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _nextBindingID="3" _whenFinished="RestoreState"><_listBinding><SequenceBinding _id="1" _name="Hero" /><SequenceBinding _id="2" _name="Cam" /></_listBinding><_listItem><SequenceTrackItem _name="Show" _binding="1" _kind="Clip" /><SequenceTrackItem _name="Cut" _binding="2" _kind="CameraCut" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_TRUE( asset.findBinding( 2 ) != nullptr );
    SW_EXPECT_EQUAL( 1u, asset.countBindingUses( 1 ) );
    SW_EXPECT_EQUAL( 3u, asset.addBinding( "New" ) ); // 다음 id 는 다시 쓰지 않는다
    SW_EXPECT_EQUAL( 4u, asset._nextBindingID );
    const string written = asset.toXML();
    SW_EXPECT_TRUE( asset.loadFromXMLText( written ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, asset._listBinding.size() );

    SW_EXPECT_LOG( "SequenceAsset.BindingIDDuplicate" );
    SW_EXPECT_FALSE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _nextBindingID="3" _whenFinished="RestoreState"><_listBinding><SequenceBinding _id="1" _name="A" /><SequenceBinding _id="1" _name="B" /></_listBinding></SequenceAsset>)" ) );
    SW_EXPECT_LOG( "SequenceAsset.BindingIDRange" );
    SW_EXPECT_FALSE( asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" _nextBindingID="2" _whenFinished="RestoreState"><_listBinding><SequenceBinding _id="5" _name="A" /></_listBinding></SequenceAsset>)" ) );
    SW_EXPECT_LOG( "SequenceAsset.ItemBindingUnknown" );
    SW_EXPECT_FALSE(
        asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Show" _binding="7" _kind="Clip" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_LOG( "SequenceAsset.TrackBindingUnknown" );
    SW_EXPECT_FALSE( asset.loadFromXMLText(
        R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listTrack><SequenceKeyTrack _name="Move" _binding="4" _kind="Property"><_listChannel><FloatCurve><_listKey /></FloatCurve></_listChannel></SequenceKeyTrack></_listTrack></SequenceAsset>)" ) );
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    SW_EXPECT_FALSE(
        asset.loadFromXMLText( R"(<SequenceAsset _frameMin="0" _frameMax="10" _whenFinished="RestoreState"><_listItem><SequenceTrackItem _name="Old" _targetObject="Hero" _kind="Event" /></_listItem></SequenceAsset>)" ) );
}

/**
 * @brief [SequencerTest] 바인딩 창은 스폰 바인딩을 스폰한 오브젝트로, 나머지를 씬의 표로 푼다 — 스폰 바인딩은 표에 칸이 있어도 표를 보지 않는다(이름으로 찾지 않는다)
 */
SW_TEST_CASE( SequencerTest, BindingViewPrefersSpawnedObjects )
{
    SequenceAsset asset;
    asset._whenFinished      = SequenceFinishPolicy::RestoreState;
    const uint32      heroID = asset.addBinding( "Hero" );
    const uint32      propID = asset.addBinding( "Prop" );
    SequenceTrackItem spawn{};
    spawn._kind    = SequenceItemKind::Spawn;
    spawn._binding = propID;
    spawn._prefab  = "a.prefab.xml";
    asset._listItem.push_back( spawn );

    vector<SequenceBindingSlot> listSlot( 2 );
    listSlot[0]._binding = heroID;
    listSlot[0]._object  = GameObjectHandle::make( 11 );
    listSlot[1]._binding = propID;
    listSlot[1]._object  = GameObjectHandle::make( 12 );
    vector<SequenceSpawnedObject> listSpawned;
    SequenceBindingView           view;
    view._pAsset       = &asset;
    view._pListSlot    = &listSlot;
    view._pListSpawned = &listSpawned;

    SW_EXPECT_TRUE( view.findHandle( heroID ) == GameObjectHandle::make( 11 ) );
    SW_EXPECT_FALSE( view.findHandle( propID ).isValid() ); // 아직 스폰하지 않았다 — 씬의 칸은 보지 않는다
    SequenceSpawnedObject spawned;
    spawned._itemIndex = 0;
    spawned._object    = GameObjectHandle::make( 40 );
    listSpawned.push_back( spawned );
    SW_EXPECT_TRUE( view.findHandle( propID ) == GameObjectHandle::make( 40 ) );
    SW_EXPECT_FALSE( view.findHandle( kSequenceNoBinding ).isValid() );
    SW_EXPECT_FALSE( view.findHandle( 99 ).isValid() );
}
