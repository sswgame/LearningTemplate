#include "pch.h"

#include "Engine/Graphics/Texture/TextureLayerBake.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    uint32 TextureLayerBakeConstants::encodeUnorm16( float32 value, float32 minValue, float32 maxValue )
    {
        const float32 range = maxValue - minValue;
        const float32 unit  = range > 0.0f ? MathUtil::saturate( ( value - minValue ) / range ) : 0.0f;
        return static_cast<uint32>( MathUtil::round( unit * static_cast<float32>( kUnorm16Max ) ) );
    }

    float32 TextureLayerBakeConstants::decodeUnorm16( uint32 step, float32 minValue, float32 maxValue )
    {
        return minValue + static_cast<float32>( step & kUnorm16Max ) / static_cast<float32>( kUnorm16Max ) * ( maxValue - minValue );
    }

    uint32 TextureLayerBakeConstants::encodeUnorm8( float32 value )
    {
        return static_cast<uint32>( MathUtil::round( MathUtil::saturate( value ) * static_cast<float32>( kUnorm8Max ) ) );
    }

    void TextureLayerBakeConstants::packLayer( const TextureLayerBakeLayer& layer )
    {
        _arrDword[kRootColor] = encodeUnorm8( layer._color._x ) | ( encodeUnorm8( layer._color._y ) << 8 ) | ( encodeUnorm8( layer._color._z ) << 16 ) |
                                ( encodeUnorm8( layer._color._w ) << 24 );
        _arrDword[kRootOffset] = encodeUnorm16( layer._offset._x, 0.0f, 1.0f ) | ( encodeUnorm16( layer._offset._y, 0.0f, 1.0f ) << 16 );
        _arrDword[kRootScale]  = encodeUnorm16( layer._scale._x, kMinScale, kMaxScale ) | ( encodeUnorm16( layer._scale._y, kMinScale, kMaxScale ) << 16 );
        uint32 rotationFlags   = encodeUnorm16( layer._rotationDegrees, -kMaxRotation, kMaxRotation ) | ( static_cast<uint32>( layer._blend & kBlendMask ) << kBlendShift );
        if ( layer._bPlaced == SW_TRUE )
            rotationFlags |= kFlagPlaced;
        if ( layer._bMirror == SW_TRUE )
            rotationFlags |= kFlagMirror;
        _arrDword[kRootRotationFlags] = rotationFlags;
        _arrDword[kRootRegionMin]     = encodeUnorm16( layer._regionMin._x, 0.0f, 1.0f ) | ( encodeUnorm16( layer._regionMin._y, 0.0f, 1.0f ) << 16 );
        _arrDword[kRootRegionMax]     = encodeUnorm16( layer._regionMax._x, 0.0f, 1.0f ) | ( encodeUnorm16( layer._regionMax._y, 0.0f, 1.0f ) << 16 );
        _arrDword[kRootSurface]       = encodeUnorm8( layer._roughness ) | ( encodeUnorm8( layer._roughnessWeight ) << 8 ) | ( encodeUnorm8( layer._metallic ) << 16 ) |
                                  ( encodeUnorm8( layer._metallicWeight ) << 24 );
    }
} // namespace sw
