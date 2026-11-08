#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct SkinLayerInternal
        {
            static constexpr const utf8* kArrValueAttribute[] = { "layer", "color", "opacity", "offset", "scale", "rotation", "mirror", "hidden" };

            static float32 quantizeUnit( float32 value, uint32 steps )
            {
                return static_cast<float32>( static_cast<uint32>( MathUtil::saturate( value ) * static_cast<float32>( steps ) + 0.5f ) ) / static_cast<float32>( steps );
            }

            /** @brief `"u v"` 두 실수를 읽습니다. 없으면 그대로 두고, 깨졌으면 오류입니다. */
            static void readOptionalFloat2( const XMLNode& node, const utf8* pAttribute, float2& inoutValue, AppearanceLoadReport& report, string_view sourceName )
            {
                if ( node.findAttribute( pAttribute ) == nullptr )
                    return;
                float32 arrValue[2] = {};
                uint32  count       = 0;
                bool    bValid      = true;
                StringUtil::forEachToken( node.getAttributeText( pAttribute ), " ,", [&]( string_view token )
                {
                    float32 value = 0.0f;
                    if ( count < 2 && StringUtil::parseFloat( token, value ) )
                        arrValue[count] = value;
                    else
                        bValid = false;
                    ++count;
                } );
                if ( bValid && count == 2 )
                    inoutValue = float2{ arrValue[0], arrValue[1] };
                else
                    report.addErrorWithID( "Appearance.ValueShape", "%#: <%#> '%#' needs exactly two numbers 'u v'", sourceName, node.getName(), pAttribute );
            }

            static float32 applyOverlay( float32 below, float32 layer ) { return below < 0.5f ? 2.0f * below * layer : 1.0f - 2.0f * ( 1.0f - below ) * ( 1.0f - layer ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 SkinLayerValue::encodeFine( float32 value, float32 minValue, float32 maxValue )
    {
        const float32 unit = MathUtil::saturate( ( value - minValue ) / ( maxValue - minValue ) );
        return static_cast<uint32>( unit * static_cast<float32>( kFineSteps ) + 0.5f );
    }

    float32 SkinLayerValue::decodeFine( uint32 step, float32 minValue, float32 maxValue )
    {
        return minValue + ( maxValue - minValue ) * ( static_cast<float32>( MathUtil::min( step, kFineSteps ) ) / static_cast<float32>( kFineSteps ) );
    }

    void SkinLayerValue::quantize()
    {
        _color           = float4{ SkinLayerInternal::quantizeUnit( _color._x, kColorSteps ), SkinLayerInternal::quantizeUnit( _color._y, kColorSteps ), SkinLayerInternal::quantizeUnit( _color._z, kColorSteps ),
                         SkinLayerInternal::quantizeUnit( _color._w, kColorSteps ) };
        _opacity         = SkinLayerInternal::quantizeUnit( _opacity, kColorSteps );
        _offset          = float2{ decodeFine( encodeFine( _offset._x, 0.0f, 1.0f ), 0.0f, 1.0f ), decodeFine( encodeFine( _offset._y, 0.0f, 1.0f ), 0.0f, 1.0f ) };
        _scale           = float2{ decodeFine( encodeFine( _scale._x, kMinScale, kMaxScale ), kMinScale, kMaxScale ), decodeFine( encodeFine( _scale._y, kMinScale, kMaxScale ), kMinScale, kMaxScale ) };
        _rotationDegrees = decodeFine( encodeFine( _rotationDegrees, -kMaxRotation, kMaxRotation ), -kMaxRotation, kMaxRotation );
    }

    bool SkinLayerValue::operator==( const SkinLayerValue& other ) const
    {
        return _layer == other._layer && _color == other._color && _offset == other._offset && _scale == other._scale && _opacity == other._opacity && _rotationDegrees == other._rotationDegrees &&
               _bMirror == other._bMirror && _bHidden == other._bHidden;
    }

    float2 SkinLayerValue::toLayerUV( const float2& skinUV, bool bMirrorCopy ) const
    {
        const float2  source  = bMirrorCopy ? float2{ 1.0f - skinUV._x, skinUV._y } : skinUV;
        const float2  local   = source - _offset;
        const float32 radians = -_rotationDegrees * MathUtil::kDegreeToRadian;
        const float32 cosine  = MathUtil::cos( radians );
        const float32 sine    = MathUtil::sin( radians );
        const float2  rotated{ local._x * cosine - local._y * sine, local._x * sine + local._y * cosine };
        const float2  layerUV{ rotated._x / MathUtil::max( _scale._x, kMinScale ) + 0.5f, rotated._y / MathUtil::max( _scale._y, kMinScale ) + 0.5f };
        return bMirrorCopy ? float2{ 1.0f - layerUV._x, layerUV._y } : layerUV;
    }

    bool SkinLayerValue::readFromXML( const XMLNode& node, AppearanceLoadReport& report, string_view sourceName )
    {
        (void)AppearanceXMLUtil::reportUnknownAttributes( node, SkinLayerInternal::kArrValueAttribute, report, sourceName );
        _layer = AppearanceXMLUtil::readRequiredName( node, "layer", report, sourceName );
        AppearanceXMLUtil::readOptionalFloat4( node, "color", _color, report, sourceName );
        AppearanceXMLUtil::readOptionalFloat( node, "opacity", 0.0f, 1.0f, _opacity, report, sourceName );
        SkinLayerInternal::readOptionalFloat2( node, "offset", _offset, report, sourceName );
        SkinLayerInternal::readOptionalFloat2( node, "scale", _scale, report, sourceName );
        AppearanceXMLUtil::readOptionalFloat( node, "rotation", -kMaxRotation, kMaxRotation, _rotationDegrees, report, sourceName );
        _bMirror = StringUtil::equals( node.getAttributeText( "mirror" ), "true" ) ? SW_TRUE : SW_FALSE;
        _bHidden = StringUtil::equals( node.getAttributeText( "hidden" ), "true" ) ? SW_TRUE : SW_FALSE;
        return _layer.empty() == false;
    }

    void SkinLayerValue::writeToXML( const XMLNode& node ) const
    {
        fixed_string<constant::buffer::kMax64> color;
        formatstring( color.data(), color.capacity(), "%# %# %# %#", _color._x, _color._y, _color._z, _color._w );
        fixed_string<constant::buffer::kMax64> opacity;
        formatstring( opacity.data(), opacity.capacity(), "%#", _opacity );
        fixed_string<constant::buffer::kMax64> offset;
        formatstring( offset.data(), offset.capacity(), "%# %#", _offset._x, _offset._y );
        fixed_string<constant::buffer::kMax64> scale;
        formatstring( scale.data(), scale.capacity(), "%# %#", _scale._x, _scale._y );
        fixed_string<constant::buffer::kMax64> angle;
        formatstring( angle.data(), angle.capacity(), "%#", _rotationDegrees );
        node.appendAttribute( "layer", _layer.c_str() );
        node.appendAttribute( "color", color.c_str() );
        node.appendAttribute( "opacity", opacity.c_str() );
        node.appendAttribute( "offset", offset.c_str() );
        node.appendAttribute( "scale", scale.c_str() );
        node.appendAttribute( "rotation", angle.c_str() );
        if ( _bMirror == SW_TRUE )
            node.appendAttribute( "mirror", "true" );
        if ( _bHidden == SW_TRUE )
            node.appendAttribute( "hidden", "true" );
    }

    bool SkinLayerDef::isInsideRegion( const float2& skinUV ) const
    {
        return skinUV._x >= _regionMin._x && skinUV._y >= _regionMin._y && skinUV._x <= _regionMax._x && skinUV._y <= _regionMax._y;
    }

    SkinLayerSmearState::SkinLayerSmearState()
        : _listOffset{}
        , _listDilution{}
        , _revision{ 0 }
        , _resolution{ 0 }
        , _bDirty{ SW_FALSE }
    {
    }

    void SkinLayerSmearState::initialize( uint16 resolution )
    {
        _resolution = resolution;
        _listOffset.assign( static_cast<size_t>( resolution ) * resolution, float2{} );
        _listDilution.assign( static_cast<size_t>( resolution ) * resolution, 0.0f );
        _bDirty = SW_FALSE;
        ++_revision;
    }

    void SkinLayerSmearState::clear()
    {
        if ( _bDirty == SW_FALSE )
            return;
        std::fill( _listOffset.begin(), _listOffset.end(), float2{} );
        std::fill( _listDilution.begin(), _listDilution.end(), 0.0f );
        _bDirty = SW_FALSE;
        ++_revision;
    }

    uint32 SkinLayerSmearState::findCell( const float2& uv ) const
    {
        const float32 last = static_cast<float32>( _resolution - 1 );
        const uint32  x    = static_cast<uint32>( MathUtil::clamp( uv._x, 0.0f, 1.0f ) * last + 0.5f );
        const uint32  y    = static_cast<uint32>( MathUtil::clamp( uv._y, 0.0f, 1.0f ) * last + 0.5f );
        return y * _resolution + x;
    }

    void SkinLayerSmearState::apply( const float2& uv, const float2& displacement, float32 radius, float32 dilution )
    {
        if ( _resolution == 0 )
            return;
        const float32 cellSize = 1.0f / static_cast<float32>( _resolution );
        const int32   reach    = static_cast<int32>( radius / cellSize ) + 1;
        const int32   centerX  = static_cast<int32>( MathUtil::clamp( uv._x, 0.0f, 1.0f ) * static_cast<float32>( _resolution - 1 ) + 0.5f );
        const int32   centerY  = static_cast<int32>( MathUtil::clamp( uv._y, 0.0f, 1.0f ) * static_cast<float32>( _resolution - 1 ) + 0.5f );
        for ( int32 y = MathUtil::max( centerY - reach, 0 ); y <= MathUtil::min( centerY + reach, static_cast<int32>( _resolution ) - 1 ); ++y )
        {
            for ( int32 x = MathUtil::max( centerX - reach, 0 ); x <= MathUtil::min( centerX + reach, static_cast<int32>( _resolution ) - 1 ); ++x )
            {
                const float2  cellUV{ static_cast<float32>( x ) / static_cast<float32>( _resolution - 1 ), static_cast<float32>( y ) / static_cast<float32>( _resolution - 1 ) };
                const float32 distance = ( cellUV - uv ).getLength();
                if ( distance > radius )
                    continue;
                const float32 falloff = 1.0f - distance / MathUtil::max( radius, cellSize );
                const size_t  index   = static_cast<size_t>( y ) * _resolution + static_cast<size_t>( x );
                _listOffset[index] += displacement * falloff;
                _listDilution[index] = MathUtil::saturate( _listDilution[index] + dilution * falloff );
            }
        }
        _bDirty = SW_TRUE;
        ++_revision;
    }

    void SkinLayerSmearState::advect( const float2& uv, const float2& direction, float32 radius, float32 amount, float32 resistance, float32 dilution )
    {
        const float32 strength = MathUtil::max( amount, 0.0f ) * ( 1.0f - MathUtil::saturate( resistance ) );
        if ( strength <= 0.0f )
            return;
        apply( uv, direction * ( strength * radius ), radius, strength * dilution );
    }

    void SkinLayerSmearState::drag( const float2& uv, const float2& direction, float32 radius, float32 amount, float32 resistance )
    {
        const float32 strength = MathUtil::max( amount, 0.0f ) * ( 1.0f - MathUtil::saturate( resistance ) );
        if ( strength <= 0.0f )
            return;
        apply( uv, direction * ( strength * radius ), radius, 0.0f );
    }

    float2 SkinLayerSmearState::sampleOffset( const float2& uv ) const
    {
        return _resolution == 0 ? float2{} : _listOffset[findCell( uv )];
    }

    float32 SkinLayerSmearState::sampleDilution( const float2& uv ) const
    {
        return _resolution == 0 ? 0.0f : _listDilution[findCell( uv )];
    }

    float3 SkinSurfaceSample::blend( const float3& below, const float3& layer, float32 coverage, SkinLayerBlend mode )
    {
        float3 blended = layer;
        switch ( mode )
        {
            case SkinLayerBlend::Normal:
                blended = layer;
                break;
            case SkinLayerBlend::Multiply:
                blended = float3{ below._x * layer._x, below._y * layer._y, below._z * layer._z };
                break;
            case SkinLayerBlend::Overlay:
                blended = float3{ SkinLayerInternal::applyOverlay( below._x, layer._x ), SkinLayerInternal::applyOverlay( below._y, layer._y ), SkinLayerInternal::applyOverlay( below._z, layer._z ) };
                break;
        }
        return float3::lerp( below, blended, MathUtil::saturate( coverage ) );
    }

    SkinSurfaceSample SkinSurfaceSample::composite( span<const SkinLayerSample> listLayer ) const
    {
        SkinSurfaceSample result = *this;
        for ( const SkinLayerSample& layer : listLayer )
        {
            const float32 coverage = MathUtil::saturate( layer._coverage );
            result._albedo         = blend( result._albedo, float3{ layer._color._x, layer._color._y, layer._color._z }, coverage, layer._blend );
            result._roughness      = MathUtil::lerp( result._roughness, layer._roughness, coverage * layer._roughnessWeight );
            result._metallic       = MathUtil::lerp( result._metallic, layer._metallic, coverage * layer._metallicWeight );
        }
        return result;
    }

    bool SkinLayerCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "SkinLayer" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            SkinLayerDef layer;
            if ( AppearanceXMLUtil::readFields( node, layer, report, sourceName, { "defaultColor", "regionMin", "regionMax" } ) == false )
                continue;
            AppearanceXMLUtil::readOptionalFloat4( node, "defaultColor", layer._defaultColor, report, sourceName );
            SkinLayerInternal::readOptionalFloat2( node, "regionMin", layer._regionMin, report, sourceName );
            SkinLayerInternal::readOptionalFloat2( node, "regionMax", layer._regionMax, report, sourceName );
            const bool bInvalidRegion = layer._regionMin._x >= layer._regionMax._x || layer._regionMin._y >= layer._regionMax._y;
            if ( bInvalidRegion )
                report.addErrorWithID( "Appearance.SkinLayerRegionInvalid", "%#: skin layer '%#' needs regionMin < regionMax", sourceName, layer._id.c_str() );
            if ( findLayer( layer._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <SkinLayer> has a duplicate id '%#'", sourceName, layer._id.c_str() );
                continue;
            }
            _listLayer.push_back( layer );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const SkinLayerDef* SkinLayerCatalog::findLayer( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&SkinLayerDef::_id>( _listLayer, id );
    }
} // namespace sw
