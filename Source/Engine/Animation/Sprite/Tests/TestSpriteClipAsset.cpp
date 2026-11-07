// SpriteClipAssetTest — 스프라이트 클립의 읽기 규칙(구간 자르기 · 테두리 · 이름)과 시간 계산, 재생 구간(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteClipPlayable.h"

using namespace sw;

namespace
{
    struct SpriteClipTestUtil
    {
        /** @brief 프레임 @p frameCount 개짜리 클립입니다. 프레임마다 100 ms 입니다. */
        static SpriteClipAsset makeClip( int32 frameCount )
        {
            SpriteClipAsset clip;
            clip._listFrame.resize( static_cast<size_t>( frameCount ) );
            return clip;
        }

        static SpriteClipAnimation makeAnimation( string_view name, int32 firstFrame, int32 frameCount )
        {
            SpriteClipAnimation animation;
            animation._name       = string{ name };
            animation._firstFrame = firstFrame;
            animation._frameCount = frameCount;
            return animation;
        }

        static SpriteClipKey makeKey( float32 time, float32 x, float32 angle )
        {
            SpriteClipKey key;
            key._time     = time;
            key._position = float2{ x, 0.0f };
            key._angle    = angle;
            return key;
        }
    };
} // namespace

/** @brief [SpriteClipAssetTest] 읽기: 프레임 밖 구간은 잘리거나 버려지고 알린다. 이름이 비거나 길이가 0 인 구간, 이름이 빈 알림은 버린다 */
SW_TEST_CASE( SpriteClipAssetTest, LoadClampsAndDropsRanges )
{
    SW_EXPECT_LOG( "Animation.SpriteClipRangeClamped" );
    SpriteClipAsset clip = SpriteClipTestUtil::makeClip( 4 );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "keep", 1, 2 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "tail", 2, 5 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "head", -1, 3 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "", 0, 1 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "zero", 0, 0 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "after", 9, 1 ) );
    AnimationNotifyEvent named;
    named._name = hashed_string( "step" );
    clip._listAnimation[0]._listNotify.push_back( named );
    clip._listAnimation[0]._listNotify.push_back( AnimationNotifyEvent{} );

    SpriteClipAsset loaded;
    SW_REQUIRE_TRUE( loaded.loadFromXMLText( clip.toXML() ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), loaded._listAnimation.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), loaded._listAnimation[0]._listNotify.size() );
    const SpriteClipAnimation* pTail = loaded.findAnimation( "tail" );
    SW_REQUIRE_NOT_NULL( pTail );
    SW_EXPECT_EQUAL( 2, pTail->_firstFrame );
    SW_EXPECT_EQUAL( 2, pTail->_frameCount );
    const SpriteClipAnimation* pHead = loaded.findAnimation( "head" );
    SW_REQUIRE_NOT_NULL( pHead );
    SW_EXPECT_EQUAL( 0, pHead->_firstFrame );
    SW_EXPECT_EQUAL( 2, pHead->_frameCount );
    SW_EXPECT_NULL( loaded.findAnimation( "zero" ) );
}

/** @brief [SpriteClipAssetTest] 읽기: 같은 이름의 구간은 뒤쪽을 찾을 수 없으므로 알린다. 찾기는 앞쪽을 돌려준다 */
SW_TEST_CASE( SpriteClipAssetTest, DuplicateAnimationNameWarns )
{
    SW_EXPECT_LOG( "SpriteClip.DuplicateAnimationName" );
    SpriteClipAsset clip = SpriteClipTestUtil::makeClip( 4 );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "run", 0, 2 ) );
    clip._listAnimation.push_back( SpriteClipTestUtil::makeAnimation( "run", 2, 2 ) );
    SpriteClipAsset loaded;
    SW_REQUIRE_TRUE( loaded.loadFromXMLText( clip.toXML() ) );
    const SpriteClipAnimation* pRun = loaded.findAnimation( "run" );
    SW_REQUIRE_NOT_NULL( pRun );
    SW_EXPECT_EQUAL( 0, pRun->_firstFrame );
}

/** @brief [SpriteClipAssetTest] 테두리: 픽셀을 프레임 크기로 나눠 0..1 로 자른다. 뒤집힌 프레임도 크기는 절댓값이다 */
SW_TEST_CASE( SpriteClipAssetTest, BordersBecomeFractions )
{
    SpriteClipAsset clip             = SpriteClipTestUtil::makeClip( 2 );
    clip._atlasPixelSize             = float2{ 100.0f, 50.0f };
    clip._listFrame[0]._uvRect       = float4{ 0.0f, 0.0f, 0.5f, 0.5f }; // 50 × 25 픽셀
    clip._listFrame[0]._borderPixels = float4{ 5.0f, 5.0f, 10.0f, 50.0f };
    clip._listFrame[1]._uvRect       = float4{ 0.5f, 0.0f, -0.5f, 0.5f };
    clip._listFrame[1]._borderPixels = float4{ 5.0f, 0.0f, 0.0f, 0.0f };
    SW_EXPECT_EQUAL( 0u, clip.rebuildFrameBorders() );
    SW_EXPECT_NEAR( 0.1f, clip._listFrame[0]._border._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.2f, clip._listFrame[0]._border._y, 1.0e-5f );
    SW_EXPECT_NEAR( 0.2f, clip._listFrame[0]._border._z, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, clip._listFrame[0]._border._w, 1.0e-5f );
    SW_EXPECT_NEAR( 0.1f, clip._listFrame[1]._border._x, 1.0e-5f );
    SW_EXPECT_TRUE( clip._listFrame[1].hasBorder() );

    clip._atlasPixelSize = float2{ 0.0f, 0.0f }; // 크기를 모르면 짐작하지 않고 버린다
    SW_EXPECT_EQUAL( 2u, clip.rebuildFrameBorders() );
    SW_EXPECT_FALSE( clip._listFrame[0].hasBorder() );
}

/** @brief [SpriteClipAssetTest] 읽기 실패: 음수 크기 · 음수 테두리는 오류이고 내용은 비어 있다. 아틀라스 크기가 없는 픽셀 테두리는 오류를 남기고 테두리 없이 읽는다 */
SW_TEST_CASE( SpriteClipAssetTest, LoadRejectsNegativeValues )
{
    SW_EXPECT_LOG( "SpriteClip.SpriteClipAtlasPixelSizeMust" );
    SW_EXPECT_LOG( "SpriteClip.SpriteClipFrameBorderPixels" );
    SW_EXPECT_LOG( "SpriteClip.SpriteClipFramePixel" );

    SpriteClipAsset negativeSize = SpriteClipTestUtil::makeClip( 1 );
    negativeSize._atlasPixelSize = float2{ -1.0f, 4.0f };
    SpriteClipAsset loaded;
    SW_EXPECT_FALSE( loaded.loadFromXMLText( negativeSize.toXML() ) );
    SW_EXPECT_EQUAL( 0, loaded.getFrameCount() );

    SpriteClipAsset negativeBorder                = SpriteClipTestUtil::makeClip( 1 );
    negativeBorder._listFrame[0]._borderPixels._w = -1.0f;
    SW_EXPECT_FALSE( loaded.loadFromXMLText( negativeBorder.toXML() ) );

    SpriteClipAsset noAtlas                = SpriteClipTestUtil::makeClip( 1 );
    noAtlas._listFrame[0]._borderPixels._x = 3.0f;
    SW_REQUIRE_TRUE( loaded.loadFromXMLText( noAtlas.toXML() ) );
    SW_EXPECT_FALSE( loaded._listFrame[0].hasBorder() );
}

/** @brief [SpriteClipAssetTest] 시간: 프레임 시간이 없으면 애니메이터 값을 쓰고, 시작 시각은 앞 프레임의 합이며 범위를 넘어도 안전하다 */
SW_TEST_CASE( SpriteClipAssetTest, FrameTimes )
{
    SpriteClipAsset clip           = SpriteClipTestUtil::makeClip( 3 );
    clip._listFrame[1]._durationMs = 0;
    SW_EXPECT_NEAR( 0.1f, clip.getFrameDurationSeconds( 0, 0.5f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, clip.getFrameDurationSeconds( 1, 0.5f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, clip.getFrameDurationSeconds( 9, 0.5f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, clip.computeFrameStartSeconds( -1, 0.5f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.6f, clip.computeFrameStartSeconds( 2, 0.5f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.7f, clip.computeFrameStartSeconds( 99, 0.5f ), 1.0e-5f );
    SW_EXPECT_NULL( clip.findFrame( -1 ) );
    SW_EXPECT_NULL( clip.findFrame( 3 ) );
    SW_EXPECT_NOT_NULL( clip.findFrame( 2 ) );
}

/** @brief [SpriteClipAssetTest] 구간 찾기: 이름 붙은 구간이 없으면 어떤 이름이든 전체 반복이고, 있으면 그 이름만 찾는다. 빈 클립은 없다 */
SW_TEST_CASE( SpriteClipAssetTest, FindFrameRange )
{
    SpriteClipAnimation range;
    SpriteClipAsset     empty;
    SW_EXPECT_FALSE( empty.findFrameRange( "any", range ) );

    SpriteClipAsset clip = SpriteClipTestUtil::makeClip( 5 );
    SW_REQUIRE_TRUE( clip.findFrameRange( "any", range ) );
    SW_EXPECT_EQUAL( 5, range._frameCount );
    SW_EXPECT_TRUE( range._bLoop == SW_TRUE );

    SpriteClipAnimation run = SpriteClipTestUtil::makeAnimation( "run", 1, 3 );
    run._bLoop              = SW_FALSE;
    clip._listAnimation.push_back( run );
    SW_EXPECT_FALSE( clip.findFrameRange( "any", range ) );
    SW_REQUIRE_TRUE( clip.findFrameRange( "run", range ) );
    SW_EXPECT_EQUAL( 1, range._firstFrame );
    SW_EXPECT_EQUAL( 3, range._frameCount );
    SW_EXPECT_TRUE( range._bLoop == SW_FALSE );
}

/** @brief [SpriteClipAssetTest] 트랜스폼 트랙: 순서 없는 키를 사이에서 보간하고 양 끝은 유지한다. 시각이 같은 키는 앞쪽을 쓴다. 키가 없으면 false */
SW_TEST_CASE( SpriteClipAssetTest, SamplesTransformKeys )
{
    SpriteClipAsset clip;
    SpriteClipKey   key;
    SW_EXPECT_FALSE( clip.sampleTransformKey( 0.0f, key ) );
    SW_EXPECT_FALSE( clip.hasTransformKeys() );

    clip._listKey.push_back( SpriteClipTestUtil::makeKey( 2.0f, 20.0f, 2.0f ) );
    clip._listKey.push_back( SpriteClipTestUtil::makeKey( 1.0f, 10.0f, 1.0f ) );
    clip._listKey.push_back( SpriteClipTestUtil::makeKey( 1.0f, 99.0f, 9.0f ) );

    SW_REQUIRE_TRUE( clip.sampleTransformKey( 0.0f, key ) );
    SW_EXPECT_NEAR( 10.0f, key._position._x, 1.0e-4f );
    SW_REQUIRE_TRUE( clip.sampleTransformKey( 1.5f, key ) );
    SW_EXPECT_NEAR( 15.0f, key._position._x, 1.0e-4f );
    SW_EXPECT_NEAR( 1.5f, key._angle, 1.0e-4f );
    SW_EXPECT_NEAR( 1.5f, key._time, 1.0e-4f );
    SW_REQUIRE_TRUE( clip.sampleTransformKey( 9.0f, key ) );
    SW_EXPECT_NEAR( 20.0f, key._position._x, 1.0e-4f );
}

/** @brief [SpriteClipAssetTest] 재생 구간: 길이는 프레임 시간의 합이고, 시각은 구간 안 프레임과 클립 시각으로 옮겨진다. 같은 구간의 알림을 가져온다 */
SW_TEST_CASE( SpriteClipAssetTest, PlayableMapsTime )
{
    SpriteClipAsset clip           = SpriteClipTestUtil::makeClip( 4 );
    clip._listFrame[2]._durationMs = 300;
    SpriteClipAnimation  run       = SpriteClipTestUtil::makeAnimation( "run", 1, 2 );
    AnimationNotifyEvent step;
    step._name = hashed_string( "step" );
    run._listNotify.push_back( step );
    clip._listAnimation.push_back( run );

    SpriteClipPlayable playable;
    SW_REQUIRE_TRUE( playable.configure( &clip, 1, 2, false, 0.5f ) );
    SW_EXPECT_NEAR( 0.4f, playable.getPlayLength(), 1.0e-5f );
    SW_EXPECT_FALSE( playable.isLoopingByDefault() );
    SW_EXPECT_NEAR( 0.1f, playable.computeFrameStart( 1 ), 1.0e-5f );
    SW_EXPECT_EQUAL( 0, playable.findFrameAtTime( 0.05f ) );
    SW_EXPECT_EQUAL( 1, playable.findFrameAtTime( 0.2f ) );
    SW_EXPECT_EQUAL( 1, playable.findFrameAtTime( 9.0f ) );
    SW_EXPECT_NEAR( 0.15f, playable.computeClipTime( 0.05f ), 1.0e-5f ); // 구간은 0.1 초에서 시작한다
    SW_EXPECT_NEAR( 0.5f, playable.computeClipTime( 9.0f ), 1.0e-5f );
    SW_REQUIRE_NOT_NULL( playable.findNotifyTrack() );
    SW_EXPECT_EQUAL( size_t( 1 ), playable.findNotifyTrack()->getEvents().size() );

    SW_REQUIRE_TRUE( playable.configure( &clip, 0, 2, true, 0.5f ) ); // 같은 구간이 없으면 알림도 없다
    SW_EXPECT_NULL( playable.findNotifyTrack() );

    SW_REQUIRE_TRUE( playable.configure( nullptr, 0, 3, true, 0.25f ) ); // 클립이 없으면 애니메이터 프레임 길이다
    SW_EXPECT_NEAR( 0.75f, playable.getPlayLength(), 1.0e-5f );
}

/** @brief [SpriteClipAssetTest] 재생 구간: 잘못된 값은 맞추지 않고 거절하며 이전 구간이 남는다 */
SW_TEST_CASE( SpriteClipAssetTest, PlayableRefusesBadRange )
{
    SW_EXPECT_LOG( "SpriteClipPlayable.SpriteClipPlayableRefusedFirstFrame" );
    SpriteClipPlayable playable;
    SW_REQUIRE_TRUE( playable.configure( nullptr, 2, 3, true, 0.25f ) );
    SW_EXPECT_FALSE( playable.configure( nullptr, -1, 3, true, 0.25f ) );
    SW_EXPECT_FALSE( playable.configure( nullptr, 0, 0, true, 0.25f ) );
    SW_EXPECT_FALSE( playable.configure( nullptr, 0, 3, true, 0.0f ) );
    SW_EXPECT_EQUAL( 2, playable.getFirstFrame() );
    SW_EXPECT_EQUAL( 3, playable.getFrameCount() );
}
