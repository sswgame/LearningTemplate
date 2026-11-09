#include "pch.h"

#include "Editor/Common/Asset/OutfitAtlas.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct OutfitAtlasInternal
        {
            static constexpr uint32 kChannelCount = 4; ///< RGBA8

            /** @brief 0..count-1 안으로 자른 좌표입니다(가장자리 번짐용). */
            static int64 clampIndex( int64 value, uint32 count ) { return value < 0 ? 0 : ( value >= static_cast<int64>( count ) ? static_cast<int64>( count ) - 1 : value ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> OutfitAtlasLayout::pack( vector_reference<const OutfitAtlasSource> listSource, uint32 atlasSize, uint32 padding, vector<OutfitAtlasRect>& outListRect )
    {
        outListRect.clear();
        if ( atlasSize == 0 || atlasSize > kMaxAtlasSize )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the atlas size must be 1..kMaxAtlasSize", to_string( atlasSize ) } );
        for ( size_t index = 0; index < listSource.size(); ++index )
        {
            const OutfitAtlasSource& source    = listSource[index];
            const bool               bNoPixels = source._width == 0 || source._height == 0;
            if ( source._part.empty() || bNoPixels )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an atlas texture has no part name or no pixels", "atlas texture " }.appendDetail( to_string( static_cast<uint64>( index ) ) ) );
            for ( size_t other = 0; other < index; ++other )
            {
                if ( listSource[other]._part == source._part )
                    return unexpected( Error{ ErrorKind::AlreadyExists, "an atlas part is listed twice", source._part.c_str() } );
            }
        }

        // 키가 큰 것(같으면 넓은 것, 같으면 입력 순서)부터 줄에 채운다 — 결정적이다.
        vector<uint32> listOrder( listSource.size() );
        for ( size_t index = 0; index < listOrder.size(); ++index )
        {
            listOrder[index] = static_cast<uint32>( index );
        }
        VectorUtil::stableSort( listOrder, [&listSource]( uint32 left, uint32 right )
        {
            if ( listSource[left]._height != listSource[right]._height )
                return listSource[left]._height > listSource[right]._height;
            return listSource[left]._width > listSource[right]._width;
        } );

        outListRect.assign( listSource.size(), OutfitAtlasRect{} );
        uint32 cursorX   = 0;
        uint32 cursorY   = 0;
        uint32 shelfSize = 0; // 지금 줄의 칸 높이(패딩 포함)
        for ( const uint32 index : listOrder )
        {
            const OutfitAtlasSource& source = listSource[index];
            const uint32             cellW  = source._width + 2 * padding;
            const uint32             cellH  = source._height + 2 * padding;
            if ( cellW > atlasSize )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an atlas part is wider than the atlas", source._part.c_str() } );
            if ( cursorX + cellW > atlasSize )
            {
                cursorX = 0;
                cursorY += shelfSize;
                shelfSize = 0;
            }
            if ( cursorY + cellH > atlasSize )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the atlas is too small - a part does not fit", source._part.c_str() } );
            OutfitAtlasRect rect;
            rect._part         = source._part;
            rect._x            = cursorX + padding;
            rect._y            = cursorY + padding;
            rect._width        = source._width;
            rect._height       = source._height;
            outListRect[index] = rect;
            cursorX += cellW;
            shelfSize = MathUtil::max( shelfSize, cellH );
        }
        return {};
    }
} // namespace sw

namespace sw
{
    OutfitAtlasHooks::OutfitAtlasHooks( const hashed_string& groupName, uint32 atlasSize, vector<OutfitAtlasRect> listRect )
        : _groupName{ groupName }
        , _atlasSize{ static_cast<float32>( atlasSize ) }
        , _listRect{ std::move( listRect ) }
        , _clampedUVCount{ 0 }
    {
    }

    const OutfitAtlasRect* OutfitAtlasHooks::findRect( const hashed_string& part ) const
    {
        for ( const OutfitAtlasRect& rect : _listRect )
        {
            if ( rect._part == part )
                return &rect;
        }
        return nullptr;
    }

    hashed_string OutfitAtlasHooks::getMaterialGroup( const MeshMergeSource& source ) const
    {
        return findRect( source._name ) != nullptr ? _groupName : source._name;
    }

    float2 OutfitAtlasHooks::remapUV( const MeshMergeSource& source, const float2& uv ) const
    {
        const OutfitAtlasRect* pRect = findRect( source._name );
        if ( pRect == nullptr || _atlasSize <= 0.0f )
            return uv;
        const float32 clampedU = MathUtil::saturate( uv._x );
        const float32 clampedV = MathUtil::saturate( uv._y );
        if ( clampedU != uv._x || clampedV != uv._y )
            ++_clampedUVCount;
        return float2{ ( static_cast<float32>( pRect->_x ) + clampedU * static_cast<float32>( pRect->_width ) ) / _atlasSize,
                       ( static_cast<float32>( pRect->_y ) + clampedV * static_cast<float32>( pRect->_height ) ) / _atlasSize };
    }
} // namespace sw

namespace sw
{
    void OutfitAtlasComposer::initialize( uint32 atlasSize, vector<uint8>& outListPixel )
    {
        outListPixel.assign( static_cast<size_t>( atlasSize ) * atlasSize * OutfitAtlasInternal::kChannelCount, 0 );
    }

    expected<void> OutfitAtlasComposer::blit( uint32 atlasSize, const OutfitAtlasRect& rect, uint32 padding, const vector<uint8>& textureRGBA, uint32 textureWidth, uint32 textureHeight,
                                              vector<uint8>& inoutAtlas )
    {
        using Internal = OutfitAtlasInternal;
        if ( textureWidth != rect._width || textureHeight != rect._height )
        {
            StringBuilder<constant::buffer::kMax256> detail;
            detail.appendFormat( "%#: %#x%# vs %#x%#", rect._part.c_str(), textureWidth, textureHeight, rect._width, rect._height );
            return unexpected( Error{ ErrorKind::InvalidArgument, "a part texture differs in size from its atlas slot", detail.view() } );
        }
        const size_t expectedBytes = static_cast<size_t>( textureWidth ) * textureHeight * Internal::kChannelCount;
        const size_t atlasBytes    = static_cast<size_t>( atlasSize ) * atlasSize * Internal::kChannelCount;
        const bool   bTextureShort = textureRGBA.size() < expectedBytes;
        const bool   bAtlasShort   = inoutAtlas.size() < atlasBytes;
        const bool   bOutside      = rect._x < padding || rect._y < padding || rect._x + rect._width + padding > atlasSize || rect._y + rect._height + padding > atlasSize;
        const bool   bBadBuffer    = bTextureShort || bAtlasShort;
        if ( bBadBuffer || bOutside )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a part does not fit the atlas buffer (short pixels or a slot outside the padded atlas)", rect._part.c_str() } );

        // 사각형 + 사방 패딩을 훑으며 가장자리로 자른 원본 좌표의 픽셀을 복사한다 — 안쪽은 그대로, 바깥 띠는 가장자리 번짐이다.
        const int64 firstX = static_cast<int64>( rect._x ) - static_cast<int64>( padding );
        const int64 firstY = static_cast<int64>( rect._y ) - static_cast<int64>( padding );
        const int64 lastX  = static_cast<int64>( rect._x ) + rect._width + padding;
        const int64 lastY  = static_cast<int64>( rect._y ) + rect._height + padding;
        for ( int64 atlasY = firstY; atlasY < lastY; ++atlasY )
        {
            const int64 sourceY = Internal::clampIndex( atlasY - static_cast<int64>( rect._y ), textureHeight );
            for ( int64 atlasX = firstX; atlasX < lastX; ++atlasX )
            {
                const int64  sourceX = Internal::clampIndex( atlasX - static_cast<int64>( rect._x ), textureWidth );
                const size_t from    = ( static_cast<size_t>( sourceY ) * textureWidth + static_cast<size_t>( sourceX ) ) * Internal::kChannelCount;
                const size_t to      = ( static_cast<size_t>( atlasY ) * atlasSize + static_cast<size_t>( atlasX ) ) * Internal::kChannelCount;
                for ( uint32 channel = 0; channel < Internal::kChannelCount; ++channel )
                {
                    inoutAtlas[to + channel] = textureRGBA[from + channel];
                }
            }
        }
        return {};
    }
} // namespace sw
