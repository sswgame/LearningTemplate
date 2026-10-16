#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteClipCache.h"
#include "Engine/Resource/ResourceIO.h"

#include "TestFramework/TestFramework.h"

// 스프라이트 클립(.sprite.json) — 에디터가 쓰고 런타임이 읽는 한 벌의 파서. 키는 리플렉션 PROPERTY 이름이다.

namespace
{
    /** @brief 이름 붙은 애니메이션이 없는 클립 파일입니다 — 적지 않은 칸(`_listAnimation`, 테두리)은 구조체 초기값입니다. */
    constexpr const utf8* kClipWithoutAnimations = R"({
  "_atlasPath": "game/empty/textures/hero.dds",
  "_listFrame": [
    { "_uvRect": "0,0,0.25,0.5", "_durationMs": 80 },
    { "_uvRect": "0.25,0,0.25,0.5", "_durationMs": 120 },
    { "_uvRect": "0.5,0.5,0.25,0.5", "_durationMs": 0 }
  ],
  "_listKey": [
    { "_time": 0.5, "_position": "1,2", "_angleDeg": 45 }
  ]
})";
} // namespace

/**
 * @brief [SpriteClipAssetTest] 클립 파일을 읽고, 이름 없는 클립은 어떤 이름이든 프레임 전체가 반복 구간이다. 다시 쓴 글은 다시 읽어 쓰면 바이트까지 같다
 * @details 파서는 런타임 하나다(에디터와 게임이 같이 쓴다). 쓰는 쪽은 모든 PROPERTY 를 쓰므로 처음 쓴 글과 그것을 다시 읽어 쓴 글이 같아야
 *          저장할 때마다 파일이 바뀌지 않는다.
 */
SW_TEST_CASE( SpriteClipAssetTest, ClipWithoutAnimationsReadsAsOneWholeClipAnimation )
{
    sw::SpriteClipAsset clip;
    SW_ASSERT_TRUE( clip.parseJSON( kClipWithoutAnimations ) );
    SW_EXPECT_STREQ( "game/empty/textures/hero.dds", clip._atlasPath.c_str() );
    SW_ASSERT_EQUAL( 3, clip.getFrameCount() );
    SW_EXPECT_NEAR_EQUAL( 0.25f, clip._listFrame[1]._uvRect._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[2]._uvRect._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, clip._listFrame[2]._uvRect._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[2]._uvRect._w, 1e-6f );
    SW_EXPECT_EQUAL( 120, clip._listFrame[1]._durationMs );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( clip._listKey.size() ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, clip._listKey[0]._position._y, 1e-6f );
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

    // 다시 쓰고 읽어도 같다(값). 다시 읽은 것을 쓰면 글이 바이트까지 같다.
    const sw::string    written = clip.toJSON();
    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJSON( written ) );
    SW_EXPECT_EQUAL( clip.getFrameCount(), reread.getFrameCount() );
    SW_EXPECT_STREQ( clip._atlasPath.c_str(), reread._atlasPath.c_str() );
    SW_EXPECT_EQUAL( clip._listFrame[2]._durationMs, reread._listFrame[2]._durationMs );
    SW_EXPECT_NEAR_EQUAL( 1.0f, reread._listKey[0]._position._x, 1e-6f );
    SW_EXPECT_TRUE( reread._listAnimation.empty() );
    SW_EXPECT_STREQ( written.c_str(), reread.toJSON().c_str() );
}

/**
 * @brief [SpriteClipAssetTest] 저장소의 클립 파일은 엔진이 쓰는 글 그대로다 — 읽고 다시 쓰면 바이트까지 같다
 * @details 생성 스크립트(`GenerateSpriteTextures.py`)가 만든 파일이 엔진 저장 형식과 어긋나면 에디터에서 한 번 저장할 때마다 파일 전체가 바뀐다.
 */
SW_TEST_CASE( SpriteClipAssetTest, RepositoryClipResavesByteForByte )
{
    constexpr const utf8* kClipPath = "engine/textures/test/quadrants.sprite.json";
    sw::string            fileText;
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( kClipPath, fileText ) );
    // core.autocrlf 체크아웃은 줄끝을 CRLF 로 바꾼다 — 저장소의 바이트(LF)로 비교한다.
    fileText.erase( std::remove( fileText.begin(), fileText.end(), '\r' ), fileText.end() );
    sw::SpriteClipAsset clip;
    SW_ASSERT_TRUE( clip.loadFromFile( kClipPath ) );
    SW_ASSERT_EQUAL( 4, clip.getFrameCount() );
    SW_EXPECT_EQUAL( 250, clip._listFrame[3]._durationMs );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( clip._listAnimation.size() ) );
    SW_EXPECT_STREQ( "cycle", clip._listAnimation[0]._name.c_str() );
    SW_EXPECT_EQUAL( 4, clip._listAnimation[0]._frameCount );
    SW_EXPECT_STREQ( fileText.c_str(), clip.toJSON().c_str() );
}

/**
 * @brief [SpriteClipAssetTest] 이름 붙은 구간은 왕복하고, 프레임 밖으로 나간 구간은 잘리거나 버려지며 그 사실을 로드마다 한 번 알린다
 * @details 에디터가 구간 칸을 숫자로 바로 고치므로 프레임 수보다 긴 구간이 저장될 수 있다. 조용히 다른 프레임을 보이지 않게 자르고 알린다.
 *          이름이 없거나 길이가 0 이 된 구간은 버린다(찾을 수 없는 구간이다). 이름이 빈 알림도 버린다(울릴 곳이 없다).
 */
SW_TEST_CASE( SpriteClipAssetTest, NamedAnimationsRoundTripAndOutOfRangeIsClamped )
{
    constexpr const utf8*    kClip = R"({
  "_atlasPath": "a.dds",
  "_listFrame": [ { "_uvRect": "0,0,0.5,1", "_durationMs": 0 }, { "_uvRect": "0.5,0,0.5,1" }, { "_uvRect": "0,0,1,1" } ],
  "_listAnimation": [
    { "_name": "idle", "_firstFrame": 0, "_frameCount": 1, "_bLoop": 1 },
    { "_name": "attack", "_firstFrame": 1, "_frameCount": 9, "_bLoop": 0,
      "_listNotify": [ { "_name": "swing", "_time": 0.1 }, { "_name": "", "_time": 0.2 } ] },
    { "_name": "gone", "_firstFrame": 7, "_frameCount": 2 },
    { "_name": "", "_firstFrame": 0, "_frameCount": 1 }
  ]
})";
    sw::SpriteClipAsset      clip;
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "an animation range past the last frame is clamped and reported" );
        SW_ASSERT_TRUE( clip.parseJSON( kClip ) );
    }
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "clamped or dropped" ) );
    SW_EXPECT_EQUAL( 0, clip._listFrame[0]._durationMs );

    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( clip._listAnimation.size() ) );
    sw::SpriteClipAnimation range{};
    SW_ASSERT_TRUE( clip.findFrameRange( "attack", range ) );
    SW_EXPECT_EQUAL( 1, range._firstFrame );
    SW_EXPECT_EQUAL( 2, range._frameCount ); // 9 → 프레임 끝까지
    SW_EXPECT_TRUE( range._bLoop == SW_FALSE );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( clip._listAnimation[1]._listNotify.size() ) );
    SW_EXPECT_STREQ( "swing", clip._listAnimation[1]._listNotify[0]._name.c_str() );
    SW_EXPECT_FALSE( clip.findFrameRange( "gone", range ) );    // 프레임 밖이라 버렸다
    SW_EXPECT_FALSE( clip.findFrameRange( "missing", range ) ); // 이름 붙은 것이 있으면 모르는 이름은 없다

    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJSON( clip.toJSON() ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( reread._listAnimation.size() ) );
    SW_EXPECT_STREQ( "attack", reread._listAnimation[1]._name.c_str() );
    SW_EXPECT_EQUAL( 2, reread._listAnimation[1]._frameCount );
    SW_EXPECT_TRUE( reread._listAnimation[1]._bLoop == SW_FALSE );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( reread._listAnimation[1]._listNotify.size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.1f, reread._listAnimation[1]._listNotify[0]._time, 1e-6f );
}

/**
 * @brief [SpriteClipAssetTest] 구문이 틀렸거나, 루트가 객체가 아니거나, 모르는 키(옛 키 이름 포함)가 있으면 읽지 않는다 — 빈 클립으로 성공하지 않는다
 * @details 옛 키(`"atlas"`, `"frames"`)를 받아 주는 별칭은 없다. 데이터는 새 이름으로 다시 썼다.
 */
SW_TEST_CASE( SpriteClipAssetTest, MalformedTextIsRejected )
{
    test::ScopedDefensiveTestLog expected( "malformed sprite clips are rejected" );
    sw::SpriteClipAsset          clip;
    SW_EXPECT_FALSE( clip.parseJSON( "<<< merge conflict { not json" ) );
    SW_EXPECT_FALSE( clip.parseJSON( "[ 1, 2, 3 ]" ) );
    SW_EXPECT_FALSE( clip.parseJSON( R"({ "atlas": "a.dds", "frames": [ { "u": 0, "v": 0, "w": 1, "h": 1 } ] })" ) );
    SW_EXPECT_FALSE( clip.parseJSON( R"({ "_atlasPath": "a.dds", "_listFrame": [ { "_uvRect": "0,0,1" } ] })" ) );
    SW_EXPECT_FALSE( clip.parseJSON( R"({ "_atlasPath": "a.dds", "_atlasPixelSize": "-4,4" })" ) );
    SW_EXPECT_EQUAL( 0, clip.getFrameCount() );
    SW_EXPECT_TRUE( clip._atlasPath.empty() );
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
    SW_ASSERT_TRUE( edited.parseJSON( kClipWithoutAnimations ) );
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
 * @brief [SpriteClipAssetTest] 9-슬라이스 테두리(`_borderPixels`: 왼, 아래, 오른, 위)는 아틀라스 픽셀로 적고 프레임 픽셀 크기로 나눈 비율로 읽힌다. 왕복한다
 * @details 테두리는 스프라이트 에셋에 산다(유니티 Sprite Border, Godot patch margin — 둘 다 픽셀). 아틀라스 128 × 64, 프레임이 아틀라스의 반 폭 · 전체 높이면
 *          프레임은 64 × 64 픽셀이라 왼쪽 8 px 는 0.125 다. 비율(`_border`)은 파일에 쓰지 않는다. 아틀라스 크기가 없으면 데이터 오류라 알리고 테두리 없이 읽는다.
 */
SW_TEST_CASE( SpriteClipAssetTest, SliceBorderRoundTripsPerFrame )
{
    constexpr const utf8* kBorderClip = R"({
  "_atlasPath": "engine/textures/test/checker.dds",
  "_atlasPixelSize": "128,64",
  "_listFrame": [
    { "_uvRect": "0,0,0.5,1", "_durationMs": 0, "_borderPixels": "8,16,24,32" },
    { "_uvRect": "0.5,0,0.5,1", "_durationMs": 0 }
  ]
})";
    sw::SpriteClipAsset   clip;
    SW_ASSERT_TRUE( clip.parseJSON( kBorderClip ) );
    SW_ASSERT_EQUAL( 2, clip.getFrameCount() );
    SW_EXPECT_TRUE( clip._listFrame[0].hasBorder() );
    SW_EXPECT_NEAR_EQUAL( 8.0f, clip._listFrame[0]._borderPixels._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, clip._listFrame[0]._border._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, clip._listFrame[0]._border._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.375f, clip._listFrame[0]._border._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, clip._listFrame[0]._border._w, 1e-6f );
    SW_EXPECT_FALSE( clip._listFrame[1].hasBorder() );

    const sw::string written = clip.toJSON();
    SW_EXPECT_TRUE( written.find( "\"_border\"" ) == sw::string::npos ); // 비율은 읽을 때 다시 구한다
    sw::SpriteClipAsset reread;
    SW_ASSERT_TRUE( reread.parseJSON( written ) );
    SW_EXPECT_NEAR_EQUAL( 24.0f, reread._listFrame[0]._borderPixels._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.375f, reread._listFrame[0]._border._z, 1e-6f );
    SW_EXPECT_FALSE( reread._listFrame[1].hasBorder() );

    // 코드가 아틀라스 크기를 바꾸면 비율을 다시 채운다(아틀라스를 두 배로 키우면 같은 픽셀이 반 비율).
    reread._atlasPixelSize = sw::float2{ 256.0f, 128.0f };
    SW_EXPECT_EQUAL( 0u, reread.rebuildFrameBorders() );
    SW_EXPECT_NEAR_EQUAL( 0.0625f, reread._listFrame[0]._border._x, 1e-6f );

    SW_TEST_DEFENSIVE_SCOPE( "a pixel border without the atlas size, or a negative border, is a data error" );
    sw::SpriteClipAsset noAtlasSize;
    SW_ASSERT_TRUE( noAtlasSize.parseJSON( R"({ "_atlasPath": "a.dds", "_listFrame": [ { "_uvRect": "0,0,1,1", "_borderPixels": "4,4,4,4" } ] })" ) );
    SW_EXPECT_FALSE( noAtlasSize._listFrame[0].hasBorder() );
    sw::SpriteClipAsset negative;
    SW_EXPECT_FALSE( negative.parseJSON( R"({ "_atlasPixelSize": "32,32", "_listFrame": [ { "_borderPixels": "-1,0,0,0" } ] })" ) );
}
