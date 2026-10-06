#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// CanvasDrawListTest — 캔버스 그리기 목록과 칠하기 도구(CanvasPainter). 순수 CPU(nogpu): 사각형 배치 · 일괄 끊김 · 9-슬라이스 · 가위 · 둥근 자르기 · 글리프.

namespace
{
    struct CanvasDrawListTestUtil
    {
        /** @brief 빈 텍스처 에셋 하나입니다(디바이스 없이 — 일괄은 포인터 정체성만 본다). */
        static sw::shared_ptr<const sw::Texture2D> makeTexture() { return sw::make_shared<sw::Texture2D>(); }

        /** @brief 그 텍스처의 단색 그림 브러시입니다. */
        static sw::CanvasBrush makeImageBrush( const sw::shared_ptr<const sw::Texture2D>& texture )
        {
            sw::CanvasBrush brush{};
            brush._image = texture;
            return brush;
        }
    };
} // namespace

/** @brief [CanvasDrawListTest] CanvasQuad 의 배치가 canvas.hlsl 의 SwCanvasQuad 와 같다(float4 여덟 뒤 uint 넷, 144 바이트) */
SW_TEST_CASE( CanvasDrawListTest, QuadLayoutMatchesShader )
{
    SW_EXPECT_EQUAL( size_t{ 144 }, sizeof( sw::CanvasQuad ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, offsetof( sw::CanvasQuad, _rect ) );
    SW_EXPECT_EQUAL( size_t{ 80 }, offsetof( sw::CanvasQuad, _clipRect ) );
    SW_EXPECT_EQUAL( size_t{ 96 }, offsetof( sw::CanvasQuad, _axis ) );
    SW_EXPECT_EQUAL( size_t{ 112 }, offsetof( sw::CanvasQuad, _params ) );
    SW_EXPECT_EQUAL( size_t{ 128 }, offsetof( sw::CanvasQuad, _kind ) );
    SW_EXPECT_EQUAL( size_t{ 132 }, offsetof( sw::CanvasQuad, _textureSlot ) );
    SW_EXPECT_EQUAL( size_t{ 136 }, offsetof( sw::CanvasQuad, _flags ) );
}

/** @brief [CanvasDrawListTest] 텍스처 다섯을 차례로 쓰면 일괄 둘 — 첫 일괄이 넷을 채우고 다섯째에서 끊긴다 */
SW_TEST_CASE( CanvasDrawListTest, BatchesBreakOnFifthTexture )
{
    sw::CanvasDrawList                              list{};
    sw::CanvasPainter                               painter( list, 1.0f );
    sw::vector<sw::shared_ptr<const sw::Texture2D>> listTexture;
    for ( uint32 index = 0; index < 5; ++index )
    {
        listTexture.push_back( CanvasDrawListTestUtil::makeTexture() );
        painter.fillRect( sw::float2{ 10.0f * static_cast<float32>( index ), 0.0f }, sw::float2{ 8.0f, 8.0f }, CanvasDrawListTestUtil::makeImageBrush( listTexture.back() ) );
    }
    SW_ASSERT_EQUAL( size_t{ 2 }, list._listBatch.size() );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( list._listBatch[0]._textureCount ) );
    SW_EXPECT_EQUAL( 4u, list._listBatch[0]._quadCount );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( list._listBatch[1]._textureCount ) );
    SW_EXPECT_EQUAL( 4u, list._listBatch[1]._firstQuad );
    SW_EXPECT_EQUAL( 3u, list._listQuad[3]._textureSlot );
    SW_EXPECT_EQUAL( 0u, list._listQuad[4]._textureSlot );
}

/** @brief [CanvasDrawListTest] 같은 텍스처 · 단색 사각형이 이어지면 한 일괄이다 — 단색은 텍스처 자리를 쓰지 않는다 */
SW_TEST_CASE( CanvasDrawListTest, SameTextureContinuesBatch )
{
    sw::CanvasDrawList                        list{};
    sw::CanvasPainter                         painter( list, 1.0f );
    const sw::shared_ptr<const sw::Texture2D> texture = CanvasDrawListTestUtil::makeTexture();
    for ( uint32 index = 0; index < 6; ++index )
        painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 4.0f, 4.0f }, CanvasDrawListTestUtil::makeImageBrush( texture ) );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 4.0f, 4.0f }, sw::CanvasBrush{} );

    SW_ASSERT_EQUAL( size_t{ 1 }, list._listBatch.size() );
    SW_EXPECT_EQUAL( 7u, list._listBatch[0]._quadCount );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( list._listBatch[0]._textureCount ) );
    SW_EXPECT_EQUAL( sw::invalid_index::kUint32, list._listQuad[6]._textureSlot );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CanvasQuadKind::Rect ), list._listQuad[6]._kind );
}

/**
 * @brief [CanvasDrawListTest] 경로 그림은 그리기 목록에 경로로 실린다 — 같은 경로는 한 텍스처 자리, 다른 경로 · 텍스처 객체는 다른 자리
 * @details 게임 스레드는 디바이스가 없어 그림을 경로로만 가리킨다(렌더 스레드가 `TextureCache` 로 푼다). 변이: `CanvasTextureRef::isEqual` 에서 경로 비교를 빼면
 *          두 경로가 한 자리로 합쳐져 진다.
 */
SW_TEST_CASE( CanvasDrawListTest, PathImagesShareSlotsByPath )
{
    sw::CanvasDrawList list{};
    sw::CanvasPainter  painter( list, 1.0f );
    sw::CanvasBrush    crosshair{};
    crosshair._imagePath = sw::hashed_string( "game/x/textures/crosshair.dds" );
    sw::CanvasBrush hitMarker{};
    hitMarker._imagePath = sw::hashed_string( "game/x/textures/hitmarker.dds" );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 8.0f, 8.0f }, crosshair );
    painter.fillRect( sw::float2{ 10.0f, 0.0f }, sw::float2{ 8.0f, 8.0f }, hitMarker );
    painter.fillRect( sw::float2{ 20.0f, 0.0f }, sw::float2{ 8.0f, 8.0f }, crosshair );
    painter.fillRect( sw::float2{ 30.0f, 0.0f }, sw::float2{ 8.0f, 8.0f }, CanvasDrawListTestUtil::makeImageBrush( CanvasDrawListTestUtil::makeTexture() ) );

    SW_ASSERT_EQUAL( size_t{ 1 }, list._listBatch.size() );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( list._listBatch[0]._textureCount ) );
    SW_EXPECT_TRUE( list._listBatch[0]._arrTexture[0]._texturePath == crosshair._imagePath );
    SW_EXPECT_TRUE( list._listBatch[0]._arrTexture[0]._texture == nullptr );
    SW_EXPECT_TRUE( list._listBatch[0]._arrTexture[1]._texturePath == hitMarker._imagePath );
    SW_EXPECT_TRUE( list._listBatch[0]._arrTexture[2]._texturePath.empty() );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CanvasQuadKind::Image ), list._listQuad[0]._kind );
    SW_EXPECT_EQUAL( 0u, list._listQuad[0]._textureSlot );
    SW_EXPECT_EQUAL( 1u, list._listQuad[1]._textureSlot );
    SW_EXPECT_EQUAL( 0u, list._listQuad[2]._textureSlot );
    SW_EXPECT_EQUAL( 2u, list._listQuad[3]._textureSlot );
}

/** @brief [CanvasDrawListTest] 9-슬라이스는 조각 아홉(가운데 = 여백을 뺀 나머지), 사각형이 여백 합보다 작으면 여백을 비율대로 줄인다 */
SW_TEST_CASE( CanvasDrawListTest, NineSliceEmitsNinePiecesAndShrinksMargins )
{
    sw::CanvasBrush brush  = CanvasDrawListTestUtil::makeImageBrush( CanvasDrawListTestUtil::makeTexture() );
    brush._nineSliceMargin = sw::float4{ 0.25f, 0.25f, 0.25f, 0.25f };
    brush._imageSize       = sw::float2{ 64.0f, 64.0f }; // 여백 = 0.25 × 64 = 16

    sw::CanvasDrawList wide{};
    {
        sw::CanvasPainter painter( wide, 1.0f );
        painter.drawImage( sw::float2{ 0.0f, 0.0f }, sw::float2{ 100.0f, 40.0f }, brush );
    }
    SW_ASSERT_EQUAL( size_t{ 9 }, wide._listQuad.size() );
    const sw::CanvasQuad& center = wide._listQuad[4];
    SW_EXPECT_NEAR_EQUAL( 16.0f, center._rect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, center._rect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 68.0f, center._rect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, center._rect._w, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, center._uvRect._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, center._uvRect._z, 1e-5f );
    const sw::CanvasQuad& rightBottom = wide._listQuad[8];
    SW_EXPECT_NEAR_EQUAL( 84.0f, rightBottom._rect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, rightBottom._uvRect._z, 1e-5f );

    // 20×20 — 여백 합 32 가 20 을 넘어 각 여백이 10 으로 줄고, 가운데(크기 0)는 내지 않는다.
    sw::CanvasDrawList narrow{};
    {
        sw::CanvasPainter painter( narrow, 1.0f );
        painter.drawImage( sw::float2{ 0.0f, 0.0f }, sw::float2{ 20.0f, 20.0f }, brush );
    }
    SW_ASSERT_EQUAL( size_t{ 4 }, narrow._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, narrow._listQuad[0]._rect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, narrow._listQuad[3]._rect._x, 1e-4f );
}

/** @brief [CanvasDrawListTest] 사각 자르기는 일괄의 가위다 — 겹친 자르기의 교집합이 가위이고 사각형에는 둥근 자르기 표시가 없다 */
SW_TEST_CASE( CanvasDrawListTest, RectClipBecomesBatchScissor )
{
    sw::CanvasDrawList list{};
    list._targetSize = sw::float2{ 1000.0f, 1000.0f };
    sw::CanvasPainter painter( list, 1.0f );
    painter.pushClip( sw::float2{ 10.0f, 10.0f }, sw::float2{ 100.0f, 100.0f }, 0.0f );
    painter.pushClip( sw::float2{ 50.5f, 50.5f }, sw::float2{ 100.0f, 100.0f }, 0.0f );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 200.0f, 200.0f }, sw::CanvasBrush{} );
    painter.popClip();
    painter.popClip();

    SW_ASSERT_EQUAL( size_t{ 1 }, list._listBatch.size() );
    const sw::CanvasBatch& batch = list._listBatch[0];
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_TRUE ), batch._bScissor );
    SW_EXPECT_EQUAL( 50u, batch._scissor._x ); // 내림
    SW_EXPECT_EQUAL( 50u, batch._scissor._y );
    SW_EXPECT_EQUAL( 60u, batch._scissor._width ); // 110 − 50
    SW_EXPECT_EQUAL( 60u, batch._scissor._height );
    SW_EXPECT_EQUAL( 0u, list._listQuad[0]._flags & sw::CanvasQuadFlag::kRoundedClip );
}

/** @brief [CanvasDrawListTest] 가위가 바뀌는 자리에서 일괄이 끊긴다(자르기 밖 · 안 · 다시 밖 = 일괄 셋), 자르기 밖에 통째로 있는 사각형은 내지 않는다 */
SW_TEST_CASE( CanvasDrawListTest, ClipChangeBreaksBatchAndDropsHiddenQuads )
{
    sw::CanvasDrawList list{};
    sw::CanvasPainter  painter( list, 1.0f );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 10.0f, 10.0f }, sw::CanvasBrush{} );
    painter.pushClip( sw::float2{ 0.0f, 0.0f }, sw::float2{ 50.0f, 50.0f }, 0.0f );
    painter.fillRect( sw::float2{ 5.0f, 5.0f }, sw::float2{ 10.0f, 10.0f }, sw::CanvasBrush{} );
    painter.fillRect( sw::float2{ 60.0f, 60.0f }, sw::float2{ 10.0f, 10.0f }, sw::CanvasBrush{} ); // 자르기 밖
    painter.popClip();
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 10.0f, 10.0f }, sw::CanvasBrush{} );

    SW_EXPECT_EQUAL( size_t{ 3 }, list._listQuad.size() );
    SW_ASSERT_EQUAL( size_t{ 3 }, list._listBatch.size() );
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_FALSE ), list._listBatch[0]._bScissor );
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_TRUE ), list._listBatch[1]._bScissor );
    SW_EXPECT_EQUAL( 1u, list._listBatch[1]._quadCount );
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_FALSE ), list._listBatch[2]._bScissor );
}

/** @brief [CanvasDrawListTest] 둥근 자르기는 셰이더가 한다 — 사각형에 표시 · 자르기 사각형 · 반지름(픽셀)이 실리고, 가위는 그 경계 상자다 */
SW_TEST_CASE( CanvasDrawListTest, RoundedClipGoesToShader )
{
    sw::CanvasDrawList list{};
    sw::CanvasPainter  painter( list, 2.0f );
    painter.pushClip( sw::float2{ 10.0f, 20.0f }, sw::float2{ 30.0f, 40.0f }, 6.0f );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 100.0f, 100.0f }, sw::CanvasBrush{} );
    painter.popClip();

    SW_ASSERT_EQUAL( size_t{ 1 }, list._listQuad.size() );
    const sw::CanvasQuad& quad = list._listQuad[0];
    SW_EXPECT_EQUAL( sw::CanvasQuadFlag::kRoundedClip, quad._flags & sw::CanvasQuadFlag::kRoundedClip );
    SW_EXPECT_NEAR_EQUAL( 12.0f, quad._params._w, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, quad._clipRect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, quad._clipRect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, quad._clipRect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 120.0f, quad._clipRect._w, 1e-4f );
    SW_EXPECT_EQUAL( 60u, list._listBatch[0]._scissor._width );
}

/** @brief [CanvasDrawListTest] 불투명도 스택은 곱해져 색 · 테두리 색의 알파에 들어간다 */
SW_TEST_CASE( CanvasDrawListTest, OpacityMultipliesColorAlpha )
{
    sw::CanvasDrawList list{};
    sw::CanvasPainter  painter( list, 1.0f );
    sw::CanvasBrush    brush{};
    brush._color       = sw::float4{ 1.0f, 0.0f, 0.0f, 0.8f };
    brush._borderColor = sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f };
    painter.pushOpacity( 0.5f );
    painter.pushOpacity( 0.5f );
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 4.0f, 4.0f }, brush );
    painter.popOpacity();
    painter.popOpacity();
    painter.fillRect( sw::float2{ 0.0f, 0.0f }, sw::float2{ 4.0f, 4.0f }, brush );

    SW_ASSERT_EQUAL( size_t{ 2 }, list._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 0.2f, list._listQuad[0]._color._w, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, list._listQuad[0]._borderColor._w, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, list._listQuad[0]._color._x, 1e-5f ); // 색 성분은 곧은 값 그대로(프리멀티플라이는 셰이더가)
    SW_EXPECT_NEAR_EQUAL( 0.8f, list._listQuad[1]._color._w, 1e-5f );
}

/** @brief [CanvasDrawListTest] UI 배율이 위치 · 크기 · 둥근 모서리 · 테두리를 물리 픽셀로 바꾸고, 변환은 축(2×2)과 위치에 실린다 */
SW_TEST_CASE( CanvasDrawListTest, UiScaleConvertsToPixels )
{
    sw::CanvasDrawList list{};
    sw::CanvasPainter  painter( list, 2.0f );
    sw::CanvasBrush    brush{};
    brush._cornerRadius = sw::float4{ 3.0f, 3.0f, 3.0f, 3.0f };
    brush._borderWidth  = 1.5f;
    painter.fillRect( sw::float2{ 10.0f, 20.0f }, sw::float2{ 30.0f, 40.0f }, brush );

    sw::CanvasTransform rotate{};
    rotate._axisX       = sw::float2{ 0.0f, 1.0f }; // 90 도
    rotate._axisY       = sw::float2{ -1.0f, 0.0f };
    rotate._translation = sw::float2{ 100.0f, 0.0f };
    painter.pushTransform( rotate );
    painter.fillRect( sw::float2{ 10.0f, 0.0f }, sw::float2{ 5.0f, 5.0f }, sw::CanvasBrush{} );
    painter.popTransform();

    SW_ASSERT_EQUAL( size_t{ 2 }, list._listQuad.size() );
    const sw::CanvasQuad& plain = list._listQuad[0];
    SW_EXPECT_NEAR_EQUAL( 20.0f, plain._rect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, plain._rect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, plain._rect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, plain._rect._w, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, plain._cornerRadius._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, plain._params._x, 1e-4f );

    const sw::CanvasQuad& rotated = list._listQuad[1];
    SW_EXPECT_NEAR_EQUAL( 200.0f, rotated._rect._x, 1e-4f ); // (100 + 0, 0 + 10) × 2
    SW_EXPECT_NEAR_EQUAL( 20.0f, rotated._rect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, rotated._axis._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, rotated._axis._z, 1e-5f );
}

/** @brief [CanvasDrawListTest] 글리프는 캐시의 아틀라스 사각형으로 SDF 사각형 하나 — 페이지가 일괄 텍스처, UV 는 페이지 비율, 거리 배율은 2 × spread × 래스터→픽셀 */
SW_TEST_CASE( CanvasDrawListTest, GlyphQuadUsesAtlasRect )
{
    sw::test::FakeFontRasterizer rasterizer;
    const sw::FontFaceId         face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache               cache( rasterizer );
    const float32                rasterPixelSize = static_cast<float32>( cache.getRasterParams()._pixelSize );

    sw::CanvasDrawList   list{};
    sw::CanvasPainter    painter( list, 1.0f );
    sw::CanvasGlyphStyle style{};
    style._fontSize = rasterPixelSize * 2.0f; // 래스터 1 픽셀 = 화면 2 픽셀
    SW_EXPECT_TRUE( painter.drawGlyph( sw::float2{ 100.0f, 200.0f }, face, 'A', style, cache, 1 ) );
    SW_EXPECT_FALSE( painter.drawGlyph( sw::float2{ 100.0f, 200.0f }, face, ' ', style, cache, 1 ) ); // 공백은 사각형이 없다

    SW_ASSERT_EQUAL( size_t{ 1 }, list._listQuad.size() );
    const sw::CanvasQuad& quad = list._listQuad[0];
    const float32         size = static_cast<float32>( sw::test::FakeFontRasterizer::kSdfSize ) * 2.0f;
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CanvasQuadKind::Glyph ), quad._kind );
    SW_EXPECT_NEAR_EQUAL( 100.0f, quad._rect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 200.0f - size, quad._rect._y, 1e-4f ); // 베어링 y = 비트맵 높이(가짜) — 비트맵 바닥이 기준선
    SW_EXPECT_NEAR_EQUAL( size, quad._rect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f * static_cast<float32>( cache.getRasterParams()._spreadPx ) * 2.0f, quad._params._x, 1e-4f );

    const sw::CachedGlyph* pGlyph = cache.findOrAddGlyph( face, 'A', 1 );
    SW_ASSERT_NOT_NULL( pGlyph );
    const float32 page = static_cast<float32>( sw::GlyphAtlas::kPageSize );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( pGlyph->_rect._x ) / page, quad._uvRect._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( pGlyph->_rect._y + pGlyph->_rect._height ) / page, quad._uvRect._w, 1e-6f );
    SW_ASSERT_EQUAL( size_t{ 1 }, list._listBatch.size() );
    SW_EXPECT_EQUAL( pGlyph->_rect._page, list._listBatch[0]._arrTexture[0]._atlasPage );
    SW_EXPECT_EQUAL( 0u, quad._textureSlot );
}

/** @brief [CanvasDrawListTest] 한 페이지의 구간 업로드가 한도를 넘으면 경계 상자 하나로 합치고, 한도 안이면 그대로 둔다(업로드 비용은 호출 수가 지배한다) */
SW_TEST_CASE( CanvasDrawListTest, ManySmallUploadsMergeToBoundingRect )
{
    sw::vector<sw::GlyphAtlasRect> listRegion;
    for ( uint16 index = 0; index < 3; ++index )
        listRegion.push_back( sw::GlyphAtlasRect{ 2, static_cast<uint16>( 10 + index * 20 ), 5, 8, 8 } );
    sw::CanvasRenderer::mergeUploadRegions( listRegion, sw::CanvasRenderer::kMaxRegionUploadPerPage );
    SW_EXPECT_EQUAL( size_t{ 3 }, listRegion.size() );

    listRegion.clear();
    for ( uint16 index = 0; index <= sw::CanvasRenderer::kMaxRegionUploadPerPage; ++index )
        listRegion.push_back( sw::GlyphAtlasRect{ 2, static_cast<uint16>( 100 + index * 10 ), static_cast<uint16>( 40 + index ), 6, 9 } );
    sw::CanvasRenderer::mergeUploadRegions( listRegion, sw::CanvasRenderer::kMaxRegionUploadPerPage );
    SW_ASSERT_EQUAL( size_t{ 1 }, listRegion.size() );
    const uint16 lastIndex = static_cast<uint16>( sw::CanvasRenderer::kMaxRegionUploadPerPage );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRegion[0]._page ) );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( listRegion[0]._x ) );
    SW_EXPECT_EQUAL( 40u, static_cast<uint32>( listRegion[0]._y ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( lastIndex * 10 + 6 ), static_cast<uint32>( listRegion[0]._width ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( lastIndex + 9 ), static_cast<uint32>( listRegion[0]._height ) );
}

/**
 * @brief [CanvasDrawListTest] 따로 칠한 목록(위젯 그림 캐시)을 이어 붙이면 가위가 같고 텍스처 합이 넷 안일 때 한 일괄로 합치고 텍스처 번호를 다시 매긴다 —
 *        가위가 다르면 새 일괄이다. 같은 내용 비교(`isSameContent`)는 텍스처 · 사각형 바이트를 본다
 * @details 변이: `appendDrawList` 의 번호 다시 매기기를 빼면 두 번째 목록의 사각형이 t2 를 0 번으로 가리켜 진다.
 */
SW_TEST_CASE( CanvasDrawListTest, AppendMergesBatchesAndRemapsTextures )
{
    const sw::shared_ptr<const sw::Texture2D> t1 = CanvasDrawListTestUtil::makeTexture();
    const sw::shared_ptr<const sw::Texture2D> t2 = CanvasDrawListTestUtil::makeTexture();
    sw::CanvasDrawList                        first{};
    sw::CanvasPainter                         firstPainter( first, 1.0f );
    firstPainter.fillRect( sw::float2{}, sw::float2{ 4.0f, 4.0f }, CanvasDrawListTestUtil::makeImageBrush( t1 ) );
    sw::CanvasDrawList second{};
    sw::CanvasPainter  secondPainter( second, 1.0f );
    secondPainter.fillRect( sw::float2{}, sw::float2{ 4.0f, 4.0f }, CanvasDrawListTestUtil::makeImageBrush( t2 ) );
    secondPainter.fillRect( sw::float2{}, sw::float2{ 4.0f, 4.0f }, CanvasDrawListTestUtil::makeImageBrush( t1 ) );
    sw::CanvasDrawList clipped{};
    sw::CanvasPainter  clippedPainter( clipped, 1.0f );
    clippedPainter.pushClip( sw::float2{}, sw::float2{ 2.0f, 2.0f }, 0.0f );
    clippedPainter.fillRect( sw::float2{}, sw::float2{ 4.0f, 4.0f }, CanvasDrawListTestUtil::makeImageBrush( t1 ) );
    clippedPainter.popClip();

    sw::CanvasDrawList frame{};
    frame.appendDrawList( first );
    frame.appendDrawList( second );
    SW_ASSERT_EQUAL( size_t{ 1 }, frame._listBatch.size() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( frame._listBatch[0]._textureCount ) );
    SW_EXPECT_EQUAL( 3u, frame._listBatch[0]._quadCount );
    SW_EXPECT_EQUAL( 0u, frame._listQuad[0]._textureSlot ); // t1
    SW_EXPECT_EQUAL( 1u, frame._listQuad[1]._textureSlot ); // t2 — 두 번째 목록에서는 0 번이었다
    SW_EXPECT_EQUAL( 0u, frame._listQuad[2]._textureSlot ); // t1 — 두 번째 목록에서는 1 번이었다
    frame.appendDrawList( clipped );
    SW_ASSERT_EQUAL( size_t{ 2 }, frame._listBatch.size() );
    SW_EXPECT_EQUAL( 3u, frame._listBatch[1]._firstQuad );
    SW_EXPECT_TRUE( frame._listBatch[1]._bScissor == SW_TRUE );

    sw::CanvasDrawList copy{};
    copy.appendDrawList( frame );
    SW_EXPECT_TRUE( copy.isSameContent( frame ) );
    copy._listQuad[0]._color._x = 0.5f;
    SW_EXPECT_FALSE( copy.isSameContent( frame ) );
}
