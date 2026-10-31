// OutfitAtlas — 아틀라스 굽기의 순수 계산: 배치(선반 채우기) · UV 를 옮기는 병합 훅 · 텍스처 합성(가장자리 번짐).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/OutfitAtlas.h"

#include "Engine/Character/Fit/MeshMerger.h"

using namespace sw;

namespace
{
    OutfitAtlasSource makeSource( const utf8* pPart, uint32 width, uint32 height )
    {
        OutfitAtlasSource source;
        source._part   = hashed_string( pPart );
        source._width  = width;
        source._height = height;
        return source;
    }

    /** @brief 두 사각형(번짐 포함)이 겹치면 true 입니다. */
    bool overlapsWithPadding( const OutfitAtlasRect& left, const OutfitAtlasRect& right, uint32 padding )
    {
        const int64 leftX0  = static_cast<int64>( left._x ) - padding;
        const int64 leftX1  = static_cast<int64>( left._x ) + left._width + padding;
        const int64 leftY0  = static_cast<int64>( left._y ) - padding;
        const int64 leftY1  = static_cast<int64>( left._y ) + left._height + padding;
        const int64 rightX0 = static_cast<int64>( right._x ) - padding;
        const int64 rightX1 = static_cast<int64>( right._x ) + right._width + padding;
        const int64 rightY0 = static_cast<int64>( right._y ) - padding;
        const int64 rightY1 = static_cast<int64>( right._y ) + right._height + padding;
        return leftX0 < rightX1 && rightX0 < leftX1 && leftY0 < rightY1 && rightY0 < leftY1;
    }
} // namespace

/**
 * @brief [OutfitAtlasTest] 배치는 입력 순서로 사각형을 돌려주고, 번짐 폭까지 합쳐 서로 겹치지 않으며 아틀라스 안에 든다. 같은 입력은 같은 배치다
 */
SW_TEST_CASE( OutfitAtlasTest, PackPlacesEveryTextureWithoutOverlap )
{
    const OutfitAtlasSource arrSource[] = {
        makeSource( "Torso", 64, 96 ),
        makeSource( "Legs", 64, 80 ),
        makeSource( "Cape", 128, 48 ),
        makeSource( "Head", 32, 32 ),
    };
    constexpr uint32        kAtlasSize = 256;
    constexpr uint32        kPadding   = 4;
    vector<OutfitAtlasRect> listRect;
    string                  error;
    SW_REQUIRE_TRUE_MSG( OutfitAtlasLayout::pack( arrSource, kAtlasSize, kPadding, listRect, &error ), error );
    SW_REQUIRE_EQUAL( size_t( 4 ), listRect.size() );
    for ( size_t index = 0; index < listRect.size(); ++index )
    {
        SW_EXPECT_TRUE( listRect[index]._part == arrSource[index]._part ); // 입력 순서
        SW_EXPECT_EQUAL( arrSource[index]._width, listRect[index]._width );
        SW_EXPECT_EQUAL( arrSource[index]._height, listRect[index]._height );
        SW_EXPECT_TRUE( listRect[index]._x >= kPadding && listRect[index]._x + listRect[index]._width + kPadding <= kAtlasSize );
        SW_EXPECT_TRUE( listRect[index]._y >= kPadding && listRect[index]._y + listRect[index]._height + kPadding <= kAtlasSize );
        for ( size_t other = 0; other < index; ++other )
        {
            SW_EXPECT_FALSE( overlapsWithPadding( listRect[index], listRect[other], 0 ) );
        }
    }
    vector<OutfitAtlasRect> listAgain;
    SW_REQUIRE_TRUE( OutfitAtlasLayout::pack( arrSource, kAtlasSize, kPadding, listAgain, nullptr ) );
    SW_REQUIRE_EQUAL( listRect.size(), listAgain.size() );
    for ( size_t index = 0; index < listRect.size(); ++index )
    {
        SW_EXPECT_TRUE( listRect[index]._x == listAgain[index]._x && listRect[index]._y == listAgain[index]._y );
    }
}

/**
 * @brief [OutfitAtlasTest] 아틀라스에 다 앉지 않거나 이름이 겹치거나 크기가 0 이면 까닭을 남기고 거절한다
 */
SW_TEST_CASE( OutfitAtlasTest, PackRefusesWhatDoesNotFit )
{
    string                  error;
    vector<OutfitAtlasRect> listRect;
    const OutfitAtlasSource arrTooBig[] = { makeSource( "A", 100, 100 ), makeSource( "B", 100, 100 ) };
    SW_EXPECT_FALSE( OutfitAtlasLayout::pack( arrTooBig, 128, 0, listRect, &error ) );
    SW_EXPECT_TRUE( error.find( "does not fit" ) != string::npos );
    const OutfitAtlasSource arrWide[] = { makeSource( "Wide", 300, 10 ) };
    SW_EXPECT_FALSE( OutfitAtlasLayout::pack( arrWide, 256, 0, listRect, &error ) );
    const OutfitAtlasSource arrTwice[] = { makeSource( "A", 8, 8 ), makeSource( "A", 8, 8 ) };
    SW_EXPECT_FALSE( OutfitAtlasLayout::pack( arrTwice, 64, 0, listRect, &error ) );
    SW_EXPECT_TRUE( error.find( "twice" ) != string::npos );
    const OutfitAtlasSource arrEmpty[] = { makeSource( "A", 0, 8 ) };
    SW_EXPECT_FALSE( OutfitAtlasLayout::pack( arrEmpty, 64, 0, listRect, &error ) );
    SW_EXPECT_FALSE( OutfitAtlasLayout::pack( arrEmpty, 0, 0, listRect, &error ) );
}

/**
 * @brief [OutfitAtlasTest] 병합 훅 — 배치에 있는 부품은 한 그룹이 되고 UV 가 아틀라스 안의 자리로 옮겨 가며, 없는 부품은 자기 그룹에 UV 도 그대로다. 0..1 밖 UV 는 자르고 센다
 */
SW_TEST_CASE( OutfitAtlasTest, HooksGroupPartsAndRemapUv )
{
    vector<OutfitAtlasRect> listRect;
    OutfitAtlasRect         torso;
    torso._part   = hashed_string( "Torso" );
    torso._x      = 64;
    torso._y      = 32;
    torso._width  = 128;
    torso._height = 64;
    listRect.push_back( torso );
    const OutfitAtlasHooks hooks( hashed_string( "Outfit" ), 256, listRect );

    MeshMergeSource inAtlas;
    inAtlas._name = hashed_string( "Torso" );
    MeshMergeSource outside;
    outside._name = hashed_string( "Prop" );
    SW_EXPECT_TRUE( hooks.getMaterialGroup( inAtlas ) == hashed_string( "Outfit" ) );
    SW_EXPECT_TRUE( hooks.getMaterialGroup( outside ) == hashed_string( "Prop" ) );

    // (0, 0) 은 사각형의 왼쪽 위, (1, 1) 은 오른쪽 아래, (0.5, 0.5) 는 한가운데다.
    const float2 topLeft = hooks.remapUv( inAtlas, float2{ 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 64.0f / 256.0f, topLeft._x, 1.0e-6f );
    SW_EXPECT_NEAR( 32.0f / 256.0f, topLeft._y, 1.0e-6f );
    const float2 bottomRight = hooks.remapUv( inAtlas, float2{ 1.0f, 1.0f } );
    SW_EXPECT_NEAR( 192.0f / 256.0f, bottomRight._x, 1.0e-6f );
    SW_EXPECT_NEAR( 96.0f / 256.0f, bottomRight._y, 1.0e-6f );
    const float2 middle = hooks.remapUv( inAtlas, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR( 128.0f / 256.0f, middle._x, 1.0e-6f );
    SW_EXPECT_EQUAL( 0u, hooks.getClampedUvCount() );

    const float2 kept = hooks.remapUv( outside, float2{ 1.5f, -0.25f } );
    SW_EXPECT_NEAR( 1.5f, kept._x, 1.0e-6f );
    SW_EXPECT_EQUAL( 0u, hooks.getClampedUvCount() ); // 배치에 없는 부품은 UV 를 건드리지 않는다
    const float2 clamped = hooks.remapUv( inAtlas, float2{ 1.5f, -0.25f } );
    SW_EXPECT_NEAR( 192.0f / 256.0f, clamped._x, 1.0e-6f );
    SW_EXPECT_NEAR( 32.0f / 256.0f, clamped._y, 1.0e-6f );
    SW_EXPECT_EQUAL( 1u, hooks.getClampedUvCount() );
}

/**
 * @brief [OutfitAtlasTest] 합성 — 텍스처 픽셀이 자리에 그대로 들어가고 사방 번짐 띠는 가장자리 픽셀로 채워지며, 크기가 다르거나 자리가 아틀라스를 벗어나면 거절한다
 */
SW_TEST_CASE( OutfitAtlasTest, BlitCopiesPixelsAndBleedsTheEdges )
{
    constexpr uint32 kAtlasSize = 16;
    constexpr uint32 kPadding   = 2;
    // 2x2 텍스처 — 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 흰색.
    const vector<uint8> texture = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
    OutfitAtlasRect     rect;
    rect._part   = hashed_string( "Part" );
    rect._x      = 4;
    rect._y      = 6;
    rect._width  = 2;
    rect._height = 2;
    vector<uint8> atlas;
    OutfitAtlasComposer::initialize( kAtlasSize, atlas );
    SW_REQUIRE_EQUAL( size_t( kAtlasSize * kAtlasSize * 4 ), atlas.size() );
    string error;
    SW_REQUIRE_TRUE_MSG( OutfitAtlasComposer::blit( kAtlasSize, rect, kPadding, texture, 2, 2, atlas, &error ), error );

    const auto pixelAt = [&atlas]( uint32 x, uint32 y, uint32 channel )
    { return static_cast<int32>( atlas[( static_cast<size_t>( y ) * kAtlasSize + x ) * 4 + channel] ); };
    SW_EXPECT_EQUAL( 255, pixelAt( 4, 6, 0 ) ); // 왼쪽 위 빨강
    SW_EXPECT_EQUAL( 255, pixelAt( 5, 6, 1 ) ); // 오른쪽 위 초록
    SW_EXPECT_EQUAL( 255, pixelAt( 4, 7, 2 ) ); // 왼쪽 아래 파랑
    SW_EXPECT_EQUAL( 255, pixelAt( 5, 7, 0 ) ); // 오른쪽 아래 흰색
    // 번짐 띠: 왼쪽 위 바깥 모서리는 왼쪽 위 픽셀(빨강), 오른쪽 아래 바깥 모서리는 오른쪽 아래 픽셀(흰색), 위쪽 띠 오른쪽 끝은 오른쪽 위 픽셀(초록)이다.
    SW_EXPECT_EQUAL( 255, pixelAt( 2, 4, 0 ) );
    SW_EXPECT_EQUAL( 0, pixelAt( 2, 4, 1 ) );
    SW_EXPECT_EQUAL( 255, pixelAt( 7, 9, 0 ) );
    SW_EXPECT_EQUAL( 255, pixelAt( 7, 9, 2 ) );
    SW_EXPECT_EQUAL( 255, pixelAt( 7, 4, 1 ) );
    SW_EXPECT_EQUAL( 0, pixelAt( 7, 4, 0 ) );
    // 번짐 밖은 건드리지 않는다.
    SW_EXPECT_EQUAL( 0, pixelAt( 1, 4, 3 ) );
    SW_EXPECT_EQUAL( 0, pixelAt( 8, 9, 3 ) );

    // 거절은 오류 글로만 알린다(로그 없음).
    SW_EXPECT_FALSE( OutfitAtlasComposer::blit( kAtlasSize, rect, kPadding, texture, 4, 1, atlas, &error ) );
    OutfitAtlasRect edge = rect;
    edge._x              = 1; // 번짐 띠가 아틀라스 밖으로 나간다
    SW_EXPECT_FALSE( OutfitAtlasComposer::blit( kAtlasSize, edge, kPadding, texture, 2, 2, atlas, &error ) );
}
