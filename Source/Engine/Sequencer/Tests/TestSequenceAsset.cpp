// 시퀀스 애셋(오브젝트 없이) — 종류 표, 키 트랙 키 찍기 · 지우기, 읽기 실패 때 남는 것 없음, 프레임 번호 한도.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"

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
    SW_EXPECT_NULL( SequenceAsset::findItemKindInfo( SequenceItemKind::Count ) );
    SW_EXPECT_NULL( SequenceAsset::findItemKindInfo( static_cast<SequenceItemKind>( -1 ) ) );
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
    SW_EXPECT_TRUE( track.findChannelKey( 0, 30.0f ) == invalid_index::kUint32 );

    SW_EXPECT_TRUE( track.removeKeysAt( 10.0f ) );
    SW_EXPECT_TRUE( track._listChannel[0]._listKey.empty() );
    SW_EXPECT_EQUAL( size_t( 1 ), track._listChannel[1]._listKey.size() );
    SW_EXPECT_FALSE( track.removeKeysAt( 10.0f ) );
}

/**
 * @brief [SequencerTest] 파싱이 실패하면 애셋에 반쯤 남은 상태가 없다
 * @details parseXML 이 _listItem 만 비우고 실패하면 앞 시퀀스의 프레임 범위와 노트가 그대로 남아, 트랙 없는 옛 시퀀스가 새 시퀀스인 척한다.
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

    SW_EXPECT_FALSE( asset.parseXML( "<SequenceAsset _frameMin=\"0\" this is not xml" ) );
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
    SW_EXPECT_FALSE( asset.parseXML( R"(<SequenceAsset _frameMin="2147483647" _frameMax="0" />)" ) );
    SW_EXPECT_FALSE(
        asset.parseXML( R"(<SequenceAsset _frameMin="0" _frameMax="10"><_listItem><SequenceTrackItem _name="Wide" _start="2147483647" _end="-2147483648" /></_listItem></SequenceAsset>)" ) );
    SW_EXPECT_FALSE( asset.parseXML( R"(<SequenceAsset _frameMin="10" _frameMax="10" />)" ) );
    SW_EXPECT_TRUE( asset._listItem.empty() );
    SW_EXPECT_EQUAL( 0, asset._frameMin );
    SW_EXPECT_EQUAL( 100, asset._frameMax );
}
