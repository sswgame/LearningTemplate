#include "pch.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceState.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterSurfaceBinding.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 표면 상태 → 머티리얼 파라미터 · 마스크 텍스처 — 이름 규칙, 마스크 픽셀, 바뀐 프레임만 올리게 하는 상태 번호.

namespace
{
    constexpr const utf8* kChannelsXML = "<SurfaceChannels>"
                                         "  <Channel name='Wet' decayPerSecond='0.5'/>"
                                         "  <Channel name='Blood' decayPerSecond='0.25' maskResolution='4'/>"
                                         "  <Channel name='Tear' maskResolution='4' clip='true'/>"
                                         "</SurfaceChannels>";

} // namespace

/**
 * @brief [CharacterSurfaceBindingTest] 머티리얼 파라미터 · 마스크 텍스처의 이름 규칙 — 머티리얼 에셋이 같은 이름을 선언한다
 */
SW_TEST_CASE( CharacterSurfaceBindingTest, ParameterNamesFollowTheContract )
{
    SW_EXPECT_STREQ( "_surfaceWet", SurfaceParameterNames::makeChannel( hashed_string( "Wet" ) ).c_str() );
    SW_EXPECT_STREQ( "_surfaceBloodMask", SurfaceParameterNames::makeMask( hashed_string( "Blood" ) ).c_str() );
    SW_EXPECT_STREQ( "_surfaceRegion3Chunk1", SurfaceParameterNames::makeRegion( 3, 1 ).c_str() );
    SW_EXPECT_EQUAL( "cpu/surface/42/2/Blood", SurfaceParameterNames::makeMaskTexturePath( 42, 2, hashed_string( "Blood" ) ) );
    // 소유자 키가 다르면 같은 부품 · 채널도 다른 텍스처다.
    SW_EXPECT_TRUE( SurfaceParameterNames::makeMaskTexturePath( 1, 0, hashed_string( "Blood" ) ) != SurfaceParameterNames::makeMaskTexturePath( 2, 0, hashed_string( "Blood" ) ) );
}

/**
 * @brief [CharacterSurfaceBindingTest] 마스크 픽셀은 값을 R 채널(최댓값 → 255)에 두고 알파는 255 다. 빈 마스크는 픽셀이 없다
 */
SW_TEST_CASE( CharacterSurfaceBindingTest, MaskPixelsScaleTheValueIntoRed )
{
    SurfaceMask mask;
    mask.initialize( 2 );
    (void)mask.stampCircle( float2{ 0.5f, 0.5f }, 2.0f, 1.0f ); // 네 셀 모두 1
    vector<uint8> listPixel;
    CharacterSurfaceBinding::makeMaskPixels( mask, 1.0f, listPixel );
    SW_ASSERT_EQUAL( size_t( 16 ), listPixel.size() );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( listPixel[0] ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listPixel[1] ) );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( listPixel[3] ) );
    CharacterSurfaceBinding::makeMaskPixels( mask, 2.0f, listPixel ); // 최댓값이 2 면 1 은 절반
    SW_EXPECT_EQUAL( 128, static_cast<int32>( listPixel[0] ) );
    SurfaceMask empty;
    CharacterSurfaceBinding::makeMaskPixels( empty, 1.0f, listPixel );
    SW_EXPECT_TRUE( listPixel.empty() );
}

/**
 * @brief [CharacterSurfaceBindingTest] 상태 번호는 값이나 마스크가 실제로 바뀔 때만 오른다 — 바뀐 프레임에만 머티리얼로 올리게 한다
 */
SW_TEST_CASE( CharacterSurfaceBindingTest, StateRevisionRisesOnlyWhenSomethingChanges )
{
    SurfaceChannelTable channels;
    SW_ASSERT_TRUE( channels.loadFromXMLText( kChannelsXML, "channels" ) );
    CharacterSurfaceState state;
    state.initialize( channels, 2, 1 );
    const uint64 start = state.getRevision();

    SW_EXPECT_TRUE( state.setRegionValue( 0, hashed_string( "Wet" ), 0.8f ) );
    const uint64 afterSet = state.getRevision();
    SW_EXPECT_TRUE( afterSet > start );
    SW_EXPECT_TRUE( state.setRegionValue( 0, hashed_string( "Wet" ), 0.8f ) ); // 같은 값
    SW_EXPECT_EQUAL( afterSet, state.getRevision() );

    state.update( 0.5f ); // 젖음이 줄어든다
    const uint64 afterDecay = state.getRevision();
    SW_EXPECT_TRUE( afterDecay > afterSet );
    SW_EXPECT_NEAR_EQUAL( 0.55f, state.getRegionValue( 0, hashed_string( "Wet" ) ), 1.0e-5f );

    SW_EXPECT_TRUE( state.stampHit( 0, CharacterGeometryConstant::kNoGroup, hashed_string( "Blood" ), float2{ 0.5f, 0.5f }, 0.3f, 1.0f ) );
    const uint64 afterStamp = state.getRevision();
    SW_EXPECT_TRUE( afterStamp > afterDecay );

    // 다 말랐으면 흘러도 바뀌는 것이 없다.
    state.update( 100.0f );
    const uint64 afterDry = state.getRevision();
    SW_EXPECT_TRUE( afterDry > afterStamp );
    state.update( 1.0f );
    SW_EXPECT_EQUAL( afterDry, state.getRevision() );
    SW_EXPECT_EQUAL( 2u, state.getRegionCount() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( state.getChannels().size() ) );
}
