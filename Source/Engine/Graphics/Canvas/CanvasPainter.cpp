#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    namespace
    {
        struct CanvasPainterInternal
        {
            /** @brief 축 하나의 9-슬라이스 구간 셋(시작 여백 · 가운데 · 끝 여백)입니다. 크기가 0 인 구간은 내지 않는다. */
            struct SliceSpan
            {
                float32 _start{ 0.0f };
                float32 _length{ 0.0f };
                float32 _uvStart{ 0.0f };
                float32 _uvEnd{ 0.0f };
            };

            /**
             * @brief 길이 @p length 를 여백 둘로 나눕니다 — 여백 합이 길이보다 크면 비율대로 줄입니다(SpriteMeshBuilder 와 같은 규칙).
             * @param marginStart 시작 여백(UI 단위) · @param marginEnd 끝 여백 · @param uvFractionStart 시작 여백의 UV 비율 · @param uvFractionEnd 끝 여백의 UV 비율
             */
            static void makeSpans( float32 length, float32 marginStart, float32 marginEnd, float32 uvStart, float32 uvEnd, float32 uvFractionStart,
                                   float32 uvFractionEnd, SliceSpan ( &outArrSpan )[3] )
            {
                const float32 marginSum = marginStart + marginEnd;
                if ( marginSum > length && marginSum > 0.0f )
                {
                    const float32 scale = length / marginSum;
                    marginStart *= scale;
                    marginEnd *= scale;
                }
                const float32 uvRange   = uvEnd - uvStart;
                const float32 uvCenter0 = uvStart + uvRange * uvFractionStart;
                const float32 uvCenter1 = uvEnd - uvRange * uvFractionEnd;
                outArrSpan[0]           = SliceSpan{ 0.0f, marginStart, uvStart, uvCenter0 };
                outArrSpan[1]           = SliceSpan{ marginStart, MathUtil::max( 0.0f, length - marginStart - marginEnd ), uvCenter0, uvCenter1 };
                outArrSpan[2]           = SliceSpan{ length - marginEnd, marginEnd, uvCenter1, uvEnd };
            }

            /** @brief 두 축 정렬 사각형(x0, y0, x1, y1)의 교집합입니다. 비면 크기가 0 이하입니다. */
            static float4 intersect( const float4& lhs, const float4& rhs )
            {
                return float4{ MathUtil::max( lhs._x, rhs._x ), MathUtil::max( lhs._y, rhs._y ), MathUtil::min( lhs._z, rhs._z ), MathUtil::min( lhs._w, rhs._w ) };
            }

            /** @brief 변환된 사각형(왼쪽 위 · 두 축 · 크기, 픽셀)의 축 정렬 경계 상자(x0, y0, x1, y1)입니다. */
            static float4 computeBounds( const CanvasQuad& quad )
            {
                const float32 originX = quad._rect._x;
                const float32 originY = quad._rect._y;
                const float32 arrX[]  = { originX, originX + quad._rect._z * quad._axis._x, originX + quad._rect._w * quad._axis._z,
                                          originX + quad._rect._z * quad._axis._x + quad._rect._w * quad._axis._z };
                const float32 arrY[]  = { originY, originY + quad._rect._z * quad._axis._y, originY + quad._rect._w * quad._axis._w,
                                          originY + quad._rect._z * quad._axis._y + quad._rect._w * quad._axis._w };
                float4        bounds{ arrX[0], arrY[0], arrX[0], arrY[0] };
                for ( uint32 cornerIndex = 1; cornerIndex < 4; ++cornerIndex )
                {
                    bounds._x = MathUtil::min( bounds._x, arrX[cornerIndex] );
                    bounds._y = MathUtil::min( bounds._y, arrY[cornerIndex] );
                    bounds._z = MathUtil::max( bounds._z, arrX[cornerIndex] );
                    bounds._w = MathUtil::max( bounds._w, arrY[cornerIndex] );
                }
                return bounds;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasTransform CanvasTransform::makeTranslation( const float2& offset )
    {
        CanvasTransform transform{};
        transform._translation = offset;
        return transform;
    }

    float2 CanvasTransform::transformPoint( const float2& point ) const
    {
        return float2{ _axisX._x * point._x + _axisY._x * point._y + _translation._x, _axisX._y * point._x + _axisY._y * point._y + _translation._y };
    }

    float2 CanvasTransform::transformVector( const float2& vector ) const
    {
        return float2{ _axisX._x * vector._x + _axisY._x * vector._y, _axisX._y * vector._x + _axisY._y * vector._y };
    }

    CanvasTransform CanvasTransform::makeConcatenated( const CanvasTransform& child ) const
    {
        CanvasTransform combined{};
        combined._axisX       = transformVector( child._axisX );
        combined._axisY       = transformVector( child._axisY );
        combined._translation = transformPoint( child._translation );
        return combined;
    }

    CanvasPainter::CanvasPainter( CanvasDrawList& outCanvas, float32 uiScale )
        : _pDrawList{ &outCanvas }
        , _listTransform{}
        , _listClip{}
        , _listOpacity{}
        , _uiScale{ uiScale }
    {
    }

    void CanvasPainter::fillRect( const float2& position, const float2& size, const CanvasBrush& brush )
    {
        if ( brush.hasImage() )
        {
            drawImage( position, size, brush );
            return;
        }
        CanvasQuad quad{};
        placeRect( position, size, quad );
        quad._kind         = static_cast<uint32>( CanvasQuadKind::Rect );
        quad._color        = brush._color;
        quad._borderColor  = brush._borderColor;
        quad._cornerRadius = float4{ brush._cornerRadius._x * _uiScale, brush._cornerRadius._y * _uiScale, brush._cornerRadius._z * _uiScale,
                                     brush._cornerRadius._w * _uiScale };
        quad._params._x    = brush._borderWidth * _uiScale;
        appendQuad( quad, nullptr );
    }

    void CanvasPainter::drawImage( const float2& position, const float2& size, const CanvasBrush& brush )
    {
        if ( brush.hasImage() == false )
            return;
        CanvasTextureRef texture{};
        texture._texture = brush._image;
        if ( brush._image == nullptr )
            texture._texturePath = brush._imagePath;

        const float4& margin     = brush._nineSliceMargin;
        const bool    bNineSlice = margin._x > 0.0f || margin._y > 0.0f || margin._z > 0.0f || margin._w > 0.0f;
        if ( bNineSlice == false )
        {
            CanvasQuad quad{};
            placeRect( position, size, quad );
            quad._kind         = static_cast<uint32>( CanvasQuadKind::Image );
            quad._uvRect       = brush._uvRect;
            quad._color        = brush._color;
            quad._cornerRadius = float4{ brush._cornerRadius._x * _uiScale, brush._cornerRadius._y * _uiScale, brush._cornerRadius._z * _uiScale,
                                         brush._cornerRadius._w * _uiScale };
            appendQuad( quad, &texture );
            return;
        }

        // 여백의 기준 크기는 브러시가 준 그림 크기(Slate ImageSize), 없으면 텍스처 픽셀 크기다. 경로 그림은 게임 스레드가 크기를 모르므로 칠할 크기다.
        const bool                       bHasImageSize = brush._imageSize._x > 0.0f && brush._imageSize._y > 0.0f;
        const float2                     imageSize     = bHasImageSize           ? brush._imageSize
                                                       : brush._image != nullptr ? float2{ static_cast<float32>( brush._image->getWidth() ), static_cast<float32>( brush._image->getHeight() ) }
                                                                                 : size;
        CanvasPainterInternal::SliceSpan arrColumn[3];
        CanvasPainterInternal::SliceSpan arrRow[3];
        CanvasPainterInternal::makeSpans( size._x, margin._x * imageSize._x, margin._z * imageSize._x, brush._uvRect._x, brush._uvRect._z, margin._x, margin._z,
                                          arrColumn );
        CanvasPainterInternal::makeSpans( size._y, margin._y * imageSize._y, margin._w * imageSize._y, brush._uvRect._y, brush._uvRect._w, margin._y, margin._w,
                                          arrRow );
        for ( const CanvasPainterInternal::SliceSpan& row : arrRow )
        {
            for ( const CanvasPainterInternal::SliceSpan& column : arrColumn )
            {
                if ( row._length <= 0.0f || column._length <= 0.0f )
                    continue;
                CanvasQuad quad{};
                placeRect( float2{ position._x + column._start, position._y + row._start }, float2{ column._length, row._length }, quad );
                quad._kind   = static_cast<uint32>( CanvasQuadKind::Image );
                quad._uvRect = float4{ column._uvStart, row._uvStart, column._uvEnd, row._uvEnd };
                quad._color  = brush._color;
                appendQuad( quad, &texture );
            }
        }
    }

    bool CanvasPainter::drawGlyph( const float2& origin, FontFaceId face, uint32 glyphIndex, const CanvasGlyphStyle& style, GlyphCache& glyphCache, uint64 frameIndex )
    {
        const CachedGlyph* pGlyph = glyphCache.findOrAddGlyph( face, glyphIndex, frameIndex );
        if ( pGlyph == nullptr || pGlyph->_rect._width == 0 || pGlyph->_rect._height == 0 )
            return false;

        // 래스터 픽셀 → UI 단위 → 물리 픽셀. 글리프 비트맵은 윤곽 둘레로 spread 만큼 넓고, 베어링은 그 비트맵의 왼쪽 위다(기준선 위가 +).
        const SdfRasterParams& rasterParams  = glyphCache.getRasterParams();
        const float32          rasterToUi    = style._fontSize / static_cast<float32>( rasterParams._pixelSize );
        const float32          rasterToPixel = rasterToUi * _uiScale;
        const GlyphAtlasRect&  atlasRect     = pGlyph->_rect;
        const float2           glyphPosition{ origin._x + pGlyph->_bearingPx._x * rasterToUi, origin._y - pGlyph->_bearingPx._y * rasterToUi };
        const float2           glyphSize{ static_cast<float32>( atlasRect._width ) * rasterToUi, static_cast<float32>( atlasRect._height ) * rasterToUi };

        CanvasQuad quad{};
        placeRect( glyphPosition, glyphSize, quad );
        constexpr float32 kInvPageSize = 1.0f / static_cast<float32>( GlyphAtlas::kPageSize );
        quad._kind                     = static_cast<uint32>( CanvasQuadKind::Glyph );
        quad._uvRect                   = float4{ static_cast<float32>( atlasRect._x ) * kInvPageSize, static_cast<float32>( atlasRect._y ) * kInvPageSize,
                               static_cast<float32>( atlasRect._x + atlasRect._width ) * kInvPageSize,
                               static_cast<float32>( atlasRect._y + atlasRect._height ) * kInvPageSize };
        quad._color                    = style._color;
        quad._borderColor              = style._outlineColor;
        // 아틀라스 값 0..1 은 윤곽에서 ±spread 래스터 픽셀 — 셰이더가 (값 − 0.5) × 이 배율로 화면 픽셀 거리를 만든다.
        quad._params._x = 2.0f * static_cast<float32>( rasterParams._spreadPx ) * rasterToPixel;
        quad._params._y = style._outlineWidth * _uiScale;
        quad._params._z = ( style._bFauxBold == SW_TRUE ) ? kFauxBoldEmFraction * style._fontSize * _uiScale : 0.0f;
        if ( style._bFauxItalic == SW_TRUE )
        {
            // 기준선을 축으로 기울인다 — 사각형 위쪽은 오른쪽으로, 아래쪽은 왼쪽으로 간다.
            const float2  axisX{ quad._axis._x, quad._axis._y };
            const float32 baselineFromTop = pGlyph->_bearingPx._y * rasterToPixel;
            quad._axis._z -= kFauxItalicShear * axisX._x;
            quad._axis._w -= kFauxItalicShear * axisX._y;
            quad._rect._x += kFauxItalicShear * baselineFromTop * axisX._x;
            quad._rect._y += kFauxItalicShear * baselineFromTop * axisX._y;
        }

        CanvasTextureRef texture{};
        texture._atlasPage  = atlasRect._page;
        const size_t before = _pDrawList->_listQuad.size();
        appendQuad( quad, &texture );
        return _pDrawList->_listQuad.size() != before;
    }

    void CanvasPainter::drawShadow( const float2& position, const float2& size, const float4& cornerRadius, const float4& color, float32 blur, const float2& offset )
    {
        CanvasQuad quad{};
        placeRect( float2{ position._x + offset._x, position._y + offset._y }, size, quad );
        quad._kind         = static_cast<uint32>( CanvasQuadKind::Shadow );
        quad._color        = color;
        quad._cornerRadius = float4{ cornerRadius._x * _uiScale, cornerRadius._y * _uiScale, cornerRadius._z * _uiScale, cornerRadius._w * _uiScale };
        quad._params._y    = MathUtil::max( 0.0f, blur ) * _uiScale;
        appendQuad( quad, nullptr );
    }

    void CanvasPainter::pushClip( const float2& position, const float2& size, float32 cornerRadius )
    {
        CanvasQuad bounds{};
        placeRect( position, size, bounds );
        const float4 clipBounds = CanvasPainterInternal::computeBounds( bounds );

        ClipState state{};
        state._bClipped = SW_TRUE;
        state._bounds   = clipBounds;
        if ( _listClip.empty() == false )
        {
            const ClipState& parent = _listClip.back();
            state._bounds           = CanvasPainterInternal::intersect( parent._bounds, clipBounds );
            state._roundedBounds    = parent._roundedBounds;
            state._roundedRadius    = parent._roundedRadius;
        }
        if ( cornerRadius > 0.0f )
        {
            state._roundedBounds = clipBounds;
            state._roundedRadius = cornerRadius * _uiScale;
        }
        _listClip.push_back( state );
    }

    void CanvasPainter::popClip()
    {
        SW_LOG_ASSERT( _listClip.empty() == false, "popClip without pushClip" );
        if ( _listClip.empty() == false )
            _listClip.pop_back();
    }

    void CanvasPainter::pushTransform( const CanvasTransform& transform )
    {
        const CanvasTransform combined = _listTransform.empty() ? transform : _listTransform.back().makeConcatenated( transform );
        _listTransform.push_back( combined );
    }

    void CanvasPainter::popTransform()
    {
        SW_LOG_ASSERT( _listTransform.empty() == false, "popTransform without pushTransform" );
        if ( _listTransform.empty() == false )
            _listTransform.pop_back();
    }

    void CanvasPainter::pushOpacity( float32 opacity )
    {
        const float32 parent = _listOpacity.empty() ? 1.0f : _listOpacity.back();
        _listOpacity.push_back( parent * MathUtil::saturate( opacity ) );
    }

    void CanvasPainter::popOpacity()
    {
        SW_LOG_ASSERT( _listOpacity.empty() == false, "popOpacity without pushOpacity" );
        if ( _listOpacity.empty() == false )
            _listOpacity.pop_back();
    }

    void CanvasPainter::appendQuad( CanvasQuad& quad, const CanvasTextureRef* pTexture )
    {
        if ( _listOpacity.empty() == false )
        {
            quad._color._w *= _listOpacity.back();
            quad._borderColor._w *= _listOpacity.back();
        }

        RHIScissorRect scissor{};
        bool           bHasScissor{ false };
        if ( _listClip.empty() == false )
        {
            if ( computeScissor( scissor, bHasScissor ) == false )
                return;
            // 자르기 밖에 통째로 있는 사각형은 내지 않는다(가위가 어차피 버린다 — 사각형 버퍼 · 드로우만 아낀다).
            const float4 overlap = CanvasPainterInternal::intersect( CanvasPainterInternal::computeBounds( quad ), _listClip.back()._bounds );
            if ( overlap._z <= overlap._x || overlap._w <= overlap._y )
                return;
            const ClipState& clip = _listClip.back();
            if ( clip._roundedRadius > 0.0f )
            {
                quad._flags |= CanvasQuadFlag::kRoundedClip;
                quad._clipRect  = clip._roundedBounds;
                quad._params._w = clip._roundedRadius;
            }
        }

        // 가위가 바뀌면 일괄이 끊긴다 — 지금 일괄과 같은 가위인지 본다.
        const bool bSameScissor = _pDrawList->_listBatch.empty() == false &&
                                  ( _pDrawList->_listBatch.back()._bScissor == SW_TRUE ) == bHasScissor &&
                                  ( bHasScissor == false || Memory::compare( &_pDrawList->_listBatch.back()._scissor, &scissor, sizeof( RHIScissorRect ) ) == 0 );
        if ( bSameScissor == false )
        {
            CanvasBatch& batch = _pDrawList->_listBatch.emplace_back();
            batch._firstQuad   = static_cast<uint32>( _pDrawList->_listQuad.size() );
            batch._scissor     = scissor;
            batch._bScissor    = bHasScissor ? SW_TRUE : SW_FALSE;
        }
        quad._textureSlot = selectBatch( pTexture );
        _pDrawList->_listQuad.push_back( quad );
        ++_pDrawList->_listBatch.back()._quadCount;
    }

    uint32 CanvasPainter::selectBatch( const CanvasTextureRef* pTexture )
    {
        // appendQuad 가 가위가 같은 일괄을 이미 끝에 두었다. 여기서는 텍스처만 본다.
        CanvasBatch& current = _pDrawList->_listBatch.back();
        if ( pTexture == nullptr )
            return invalid_index::kUint32;
        for ( uint32 slot = 0; slot < current._textureCount; ++slot )
        {
            if ( current._arrTexture[slot].isEqual( *pTexture ) )
                return slot;
        }
        if ( current._textureCount < shaderslot::kMaterialTextureCount )
        {
            current._arrTexture[current._textureCount] = *pTexture;
            return current._textureCount++;
        }

        // 텍스처 자리가 찼다 — 같은 가위로 새 일괄을 연다.
        CanvasBatch& next   = _pDrawList->_listBatch.emplace_back();
        CanvasBatch& prior  = _pDrawList->_listBatch[_pDrawList->_listBatch.size() - 2];
        next._firstQuad     = static_cast<uint32>( _pDrawList->_listQuad.size() );
        next._scissor       = prior._scissor;
        next._bScissor      = prior._bScissor;
        next._arrTexture[0] = *pTexture;
        next._textureCount  = 1;
        return 0;
    }

    bool CanvasPainter::computeScissor( RHIScissorRect& outScissor, bool& outHasScissor ) const
    {
        outHasScissor = false;
        if ( _listClip.empty() || _listClip.back()._bClipped == SW_FALSE )
            return true;
        const float4& bounds = _listClip.back()._bounds;
        float32       right  = MathUtil::ceil( bounds._z );
        float32       bottom = MathUtil::ceil( bounds._w );
        if ( _pDrawList->_targetSize._x > 0.0f )
            right = MathUtil::min( right, _pDrawList->_targetSize._x );
        if ( _pDrawList->_targetSize._y > 0.0f )
            bottom = MathUtil::min( bottom, _pDrawList->_targetSize._y );
        const float32 left = MathUtil::max( 0.0f, MathUtil::floor( bounds._x ) );
        const float32 top  = MathUtil::max( 0.0f, MathUtil::floor( bounds._y ) );
        if ( right <= left || bottom <= top )
            return false;
        outScissor._x      = static_cast<uint32>( left );
        outScissor._y      = static_cast<uint32>( top );
        outScissor._width  = static_cast<uint32>( right - left );
        outScissor._height = static_cast<uint32>( bottom - top );
        outHasScissor      = true;
        return true;
    }

    void CanvasPainter::placeRect( const float2& position, const float2& size, CanvasQuad& outQuad ) const
    {
        const CanvasTransform  identity{};
        const CanvasTransform& transform = _listTransform.empty() ? identity : _listTransform.back();
        const float2           topLeft   = transform.transformPoint( position );
        outQuad._rect                    = float4{ topLeft._x * _uiScale, topLeft._y * _uiScale, size._x * _uiScale, size._y * _uiScale };
        outQuad._axis                    = float4{ transform._axisX._x, transform._axisX._y, transform._axisY._x, transform._axisY._y };
    }
} // namespace sw
