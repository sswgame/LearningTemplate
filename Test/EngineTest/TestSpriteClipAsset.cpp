#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Resource/SpriteClipCache.h"

#include "TestFramework/TestFramework.h"

// 스프라이트 클립(.sprite.json) — 에디터가 쓰고 런타임이 읽는 한 벌의 파서.

namespace
{
    /** @brief 이름 붙은 애니메이션이 없는 클립 파일입니다 — 쓰는 쪽(`SpriteClipAsset::toJson`)은 빈 목록이면 "animations" 키를 쓰지 않습니다. */
    constexpr const utf8* kClipWithoutAnimations = R"({
  "atlas": "game/empty/textures/hero.dds",
  "frames": [
    { "u": 0.0, "v": 0.0, "w": 0.25, "h": 0.5, "durationMs": 80 },
    { "u": 0.25, "v": 0.0, "w": 0.25, "h": 0.5, "durationMs": 120 },
    { "u": 0.5, "v": 0.5, "w": 0.25, "h": 0.5, "durationMs": 0 }
  ],
  "transformKeys": [
    { "time": 0.5, "x": 1.0, "y": 2.0, "angleDeg": 45.0 }
  ]
})";
} // namespace

/**
 * @brief [SpriteClipAssetTest] 에디터가 쓰는 클립 파일을 그대로 읽고, 이름 없는 클립은 어떤 이름이든 프레임 전체가 반복 구간이다
 * @details 파서는 런타임 하나다(에디터와 게임이 같이 쓴다). "animations" 키가 없는 것은 빈 목록을 쓴 지금 형식이라(옛 형식이 아니다) 같은 값으로
 *          읽혀야 하고, 다시 쓴 글에도 "animations" 키가 생기지 않아야 한다(읽은 파일과 같은 모양).
 */
SW_TEST_CASE( SpriteClipAssetTest, ClipWithoutAnimationsReadsAsOneWholeClipAnimation )
{
    sw::SpriteClipAsset clip;
    SW_ASSERT_TRUE( clip.parseJson( kClipWithoutAnimations ) );
    SW_EXPECT_STREQ( "game/empty/textures/hero.dds", clip._atlasPath.c_str() );
    SW_ASSERT_EQUAL( 3, clip.getFrameCount() );
    SW_EXPECT_NEAR_EQUAL( 0.25f, clip._listFrame[1]._uvRect._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[2]._uvRect._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, clip._listFrame[2]._uvRect._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[2]._uvRect._w, 1e-6f );
    SW_EXPECT_EQUAL( 120, clip._listFrame[1]._durationMs );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( clip._listKey.size() ) );
    SW_EXPECT_NEAR_EQUAL( 45.0f, clip._listKey[0]._angleDeg, 1e-6f );
    SW_EXPECT_TRUE( clip._listAnimation.empty() );

    // 이름 붙은 구간이 없으면 어떤 이름이든 프레임 전체 · 반복이다(클립 하나 = 애니메이션 하나).
    sw::SpriteClipAnimation range{};
    SW_ASSERT_TRUE( clip.findFrameRange( "whatever", range ) );
    SW_EXPECT_EQUAL( 0, range._firstFrame );
    SW_EXPECT_EQUAL( 3, range._frameCount );
    SW_EXPECT_TRUE( range._bLoop == SW_TRUE );

    // 프레임 시간: 있으면 그 값(초), 0 이면 애니메이터의 프레임 속도.
    SW_EXPECT_NEAR_EQUAL( 0.08f, clip.getFrameDurationSeconds( 0, 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip.getFrameDurationSeconds( 2, 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip.getFrameDurationSeconds( 9, 0.5f ), 1e-6f );

    // 다시 쓰고 읽어도 같다. 애니메이션이 없으면 키를 쓰지 않는다.
    const sw::string written = clip.toJson();
    SW_EXPECT_TRUE( written.find( "animations" ) == sw::string::npos );
    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJson( written ) );
    SW_EXPECT_EQUAL( clip.getFrameCount(), reread.getFrameCount() );
    SW_EXPECT_STREQ( clip._atlasPath.c_str(), reread._atlasPath.c_str() );
    SW_EXPECT_EQUAL( clip._listFrame[2]._durationMs, reread._listFrame[2]._durationMs );
}

/**
 * @brief [SpriteClipAssetTest] 이름 붙은 구간은 왕복하고, 프레임 밖으로 나간 구간은 잘리거나 버려지며 그 사실을 로드마다 한 번 알린다
 * @details 손으로 고친 파일이 프레임 수보다 긴 구간을 적으면 애니메이터가 없는 프레임을 보이려 한다. 조용히 다른 프레임을 보이지 않게 자르고 알린다.
 *          이름이 없거나 길이가 0 이 된 구간은 버린다(찾을 수 없는 구간이다).
 */
SW_TEST_CASE( SpriteClipAssetTest, NamedAnimationsRoundTripAndOutOfRangeIsClamped )
{
    constexpr const utf8*    kClip = R"({
  "atlas": "a.dds",
  "frames": [ { "u": 0, "v": 0, "w": 0.5, "h": 1 }, { "u": 0.5, "v": 0, "w": 0.5, "h": 1 }, { "u": 0, "v": 0, "w": 1, "h": 1 } ],
  "animations": [
    { "name": "idle", "start": 0, "count": 1, "loop": true },
    { "name": "attack", "start": 1, "count": 9, "loop": false },
    { "name": "gone", "start": 7, "count": 2 },
    { "name": "", "start": 0, "count": 1 }
  ]
})";
    sw::SpriteClipAsset      clip;
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "an animation range past the last frame is clamped and reported" );
        SW_ASSERT_TRUE( clip.parseJson( kClip ) );
    }
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "clamped or dropped" ) );
    // 키가 빠진 프레임 칸은 구조체 기본값이다(시간 0 = 프레임 속도).
    SW_EXPECT_EQUAL( 0, clip._listFrame[0]._durationMs );

    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( clip._listAnimation.size() ) );
    sw::SpriteClipAnimation range{};
    SW_ASSERT_TRUE( clip.findFrameRange( "attack", range ) );
    SW_EXPECT_EQUAL( 1, range._firstFrame );
    SW_EXPECT_EQUAL( 2, range._frameCount ); // 9 → 프레임 끝까지
    SW_EXPECT_TRUE( range._bLoop == SW_FALSE );
    SW_EXPECT_FALSE( clip.findFrameRange( "gone", range ) );    // 프레임 밖이라 버렸다
    SW_EXPECT_FALSE( clip.findFrameRange( "missing", range ) ); // 이름 붙은 것이 있으면 모르는 이름은 없다

    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJson( clip.toJson() ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( reread._listAnimation.size() ) );
    SW_EXPECT_STREQ( "attack", reread._listAnimation[1]._name.c_str() );
    SW_EXPECT_EQUAL( 2, reread._listAnimation[1]._frameCount );
    SW_EXPECT_TRUE( reread._listAnimation[1]._bLoop == SW_FALSE );
}

/**
 * @brief [SpriteClipAssetTest] 구문이 틀렸거나 루트가 객체가 아니면 읽지 않는다 — 빈 클립으로 성공하지 않는다
 */
SW_TEST_CASE( SpriteClipAssetTest, MalformedTextIsRejected )
{
    test::ScopedDefensiveTestLog expected( "malformed sprite clips are rejected" );
    sw::SpriteClipAsset          clip;
    SW_EXPECT_FALSE( clip.parseJson( "<<< merge conflict { not json" ) );
    SW_EXPECT_FALSE( clip.parseJson( "[ 1, 2, 3 ]" ) );
    SW_EXPECT_EQUAL( 0, clip.getFrameCount() );
}

/**
 * @brief [SpriteClipAssetTest] 같은 경로의 클립은 한 번 읽어 나눠 갖고, 아무도 안 쓰면 표에서 사라지며, 읽을 수 없는 경로는 nullptr 이다
 * @details 같은 클립을 쓰는 스프라이트 백 개가 파일을 백 번 읽지 않게 하는 표다(`SpriteComponent` 가 씬 로드 워커에서도 부른다).
 */
SW_TEST_CASE( SpriteClipAssetTest, CacheSharesOneClipPerPath )
{
    const sw::string path = test::makeTempPath( "shared.sprite.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, kClipWithoutAnimations ) );

    sw::shared_ptr<const sw::SpriteClipAsset> first  = sw::SpriteClipCache::acquire( path );
    sw::shared_ptr<const sw::SpriteClipAsset> second = sw::SpriteClipCache::acquire( path );
    SW_ASSERT_NOT_NULL( first.get() );
    SW_EXPECT_TRUE( first.get() == second.get() );
    SW_EXPECT_EQUAL( 3, first->getFrameCount() );

    // 다 놓으면 다음 요청은 파일을 새로 읽는다(편집한 내용이 다시 잡힌다).
    first.reset();
    second.reset();
    sw::SpriteClipAsset edited;
    SW_ASSERT_TRUE( edited.parseJson( kClipWithoutAnimations ) );
    edited._listFrame.pop_back();
    SW_ASSERT_TRUE( edited.saveToFile( path ) );
    sw::shared_ptr<const sw::SpriteClipAsset> reloaded = sw::SpriteClipCache::acquire( path );
    SW_ASSERT_NOT_NULL( reloaded.get() );
    SW_EXPECT_EQUAL( 2, reloaded->getFrameCount() );

    test::ScopedDefensiveTestLog expected( "a missing sprite clip is reported and yields nothing" );
    SW_EXPECT_TRUE( sw::SpriteClipCache::acquire( test::makeTempPath( "never_written.sprite.json" ) ) == nullptr );
    SW_EXPECT_TRUE( sw::SpriteClipCache::acquire( "" ) == nullptr );
}

/**
 * @brief [SpriteClipAssetTest] 9-슬라이스 테두리("border": [왼, 아래, 오른, 위])는 프레임마다 왕복하고, 없는 프레임은 키를 쓰지 않는다
 * @details 테두리는 스프라이트 에셋에 산다(유니티 Sprite Border). 키가 없는 프레임을 다시 쓸 때 "border" 가 생기면 테두리를 안 쓰는 모든 클립 파일이
 *          저장할 때마다 바뀐다. 숫자 넷의 배열이 아니면 데이터 오류라 알리고 테두리 없이 읽는다.
 */
SW_TEST_CASE( SpriteClipAssetTest, SliceBorderRoundTripsPerFrame )
{
    constexpr const utf8* kBorderClip = R"({
  "atlas": "engine/textures/test/checker.dds",
  "frames": [
    { "u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "durationMs": 0, "border": [0.125, 0.25, 0.375, 0.5] },
    { "u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "durationMs": 0 }
  ],
  "transformKeys": []
})";
    sw::SpriteClipAsset   clip;
    SW_ASSERT_TRUE( clip.parseJson( kBorderClip ) );
    SW_ASSERT_EQUAL( 2, clip.getFrameCount() );
    SW_EXPECT_TRUE( clip._listFrame[0].hasBorder() );
    SW_EXPECT_NEAR_EQUAL( 0.125f, clip._listFrame[0]._border._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[0]._border._w, 1e-6f );
    SW_EXPECT_FALSE( clip._listFrame[1].hasBorder() );

    const sw::string written = clip.toJson();
    SW_EXPECT_TRUE( written.find( "border" ) == written.rfind( "border" ) ); // 테두리가 있는 프레임 하나만 키를 쓴다
    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJson( written ) );
    SW_EXPECT_NEAR_EQUAL( 0.375f, reread._listFrame[0]._border._z, 1e-6f );
    SW_EXPECT_FALSE( reread._listFrame[1].hasBorder() );

    SW_TEST_DEFENSIVE_SCOPE( "a border that is not four numbers is a data error" );
    sw::SpriteClipAsset malformed;
    SW_ASSERT_TRUE( malformed.parseJson( R"({ "atlas": "a.dds", "frames": [ { "u": 0, "v": 0, "w": 1, "h": 1, "border": [0.1, 0.2] } ] })" ) );
    SW_EXPECT_FALSE( malformed._listFrame[0].hasBorder() );
}
