#include "pch.h"

#include "Engine/Character/Fit/SurfaceState.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SurfaceStateInternal
        {
            static constexpr const utf8* kArrChannelAttribute[] = { "name", "decayPerSecond", "max", "maskResolution", "clip" };
            static constexpr int32       kMaxMaskResolution     = 4096;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SurfaceChannelTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        _listChannel.clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "SurfaceChannels", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool SurfaceChannelTable::loadFromResource( string_view path )
    {
        _listChannel.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "SurfaceChannels", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void SurfaceChannelTable::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Channel", true ) == false )
            {
                reader.reportUnknownElement( child );
                continue;
            }
            reader.reportUnknownAttributes( child, SurfaceStateInternal::kArrChannelAttribute );
            SurfaceChannelDef channel;
            channel._name = reader.readName( child, "name", true );
            reader.readOptionalFloat( child, "decayPerSecond", channel._decayPerSecond );
            reader.readOptionalFloat( child, "max", channel._maxValue );
            int32 resolution{ static_cast<int32>( channel._maskResolution ) };
            bool  bClip{ channel._bClip != SW_FALSE };
            reader.readOptionalInt( child, "maskResolution", resolution );
            reader.readOptionalBool( child, "clip", bClip );
            channel._bClip = bClip ? SW_TRUE : SW_FALSE;
            if ( resolution < 0 || resolution > SurfaceStateInternal::kMaxMaskResolution )
                reader.addError( child, "needs maskResolution in [0, 4096]" );
            else
                channel._maskResolution = static_cast<uint16>( resolution );
            if ( channel._decayPerSecond < 0.0f || channel._maxValue <= 0.0f )
                reader.addError( child, "needs decayPerSecond >= 0 and max > 0" );
            if ( channel._name.empty() )
                continue;
            if ( findChannelIndex( channel._name ) >= 0 )
                reader.addError( child, string( "repeats channel '" ) + channel._name.c_str() + "'" );
            else
                _listChannel.push_back( channel );
        }
    }

    void SurfaceChannelTable::addChannel( const SurfaceChannelDef& channel )
    {
        for ( SurfaceChannelDef& existing : _listChannel )
        {
            if ( existing._name == channel._name )
            {
                existing = channel;
                return;
            }
        }
        _listChannel.push_back( channel );
    }

    int32 SurfaceChannelTable::findChannelIndex( const hashed_string& name ) const
    {
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex]._name == name )
                return static_cast<int32>( channelIndex );
        }
        return -1;
    }
} // namespace sw

namespace sw
{
    SurfaceMask::SurfaceMask()
        : _listTexel{}
        , _revision{ 0 }
        , _resolution{ 0 }
    {
    }

    void SurfaceMask::initialize( uint16 resolution )
    {
        ++_revision;
        _resolution = resolution;
        _listTexel.assign( static_cast<size_t>( resolution ) * resolution, 0.0f );
    }

    uint32 SurfaceMask::stampCircle( const float2& uvCenter, float32 uvRadius, float32 value )
    {
        if ( _resolution == 0 || uvRadius <= 0.0f )
            return 0;
        const float32 resolution   = static_cast<float32>( _resolution );
        const float32 texelRadius  = uvRadius * resolution;
        const float32 centerX      = uvCenter._x * resolution;
        const float32 centerY      = uvCenter._y * resolution;
        const int32   minX         = MathUtil::max( 0, static_cast<int32>( MathUtil::floor( centerX - texelRadius - 1.0f ) ) );
        const int32   maxX         = MathUtil::min( static_cast<int32>( _resolution ) - 1, static_cast<int32>( MathUtil::floor( centerX + texelRadius + 1.0f ) ) );
        const int32   minY         = MathUtil::max( 0, static_cast<int32>( MathUtil::floor( centerY - texelRadius - 1.0f ) ) );
        const int32   maxY         = MathUtil::min( static_cast<int32>( _resolution ) - 1, static_cast<int32>( MathUtil::floor( centerY + texelRadius + 1.0f ) ) );
        uint32        changedCount = 0;
        for ( int32 texelY = minY; texelY <= maxY; ++texelY )
        {
            for ( int32 texelX = minX; texelX <= maxX; ++texelX )
            {
                // 셀 중심까지의 거리를 본다. 반지름 안은 값 그대로 쓰고, 바깥 한 셀 폭은 부드럽게 0 으로 줄인다.
                const float32 offsetX  = static_cast<float32>( texelX ) + 0.5f - centerX;
                const float32 offsetY  = static_cast<float32>( texelY ) + 0.5f - centerY;
                const float32 distance = MathUtil::sqrt( offsetX * offsetX + offsetY * offsetY );
                const float32 weight   = CharacterGeometryUtil::computeFalloff( distance - texelRadius );
                if ( weight <= 0.0f )
                    continue;
                float32&      texel   = _listTexel[static_cast<size_t>( texelY ) * _resolution + static_cast<size_t>( texelX )];
                const float32 stamped = value * weight;
                if ( stamped <= texel )
                    continue;
                texel = stamped;
                ++changedCount;
            }
        }
        if ( changedCount > 0 )
            ++_revision;
        return changedCount;
    }

    float32 SurfaceMask::sample( const float2& uv ) const
    {
        if ( _resolution == 0 )
            return 0.0f;
        const int32 maxTexel = static_cast<int32>( _resolution ) - 1;
        const int32 texelX   = MathUtil::clamp( static_cast<int32>( MathUtil::floor( MathUtil::saturate( uv._x ) * _resolution ) ), 0, maxTexel );
        const int32 texelY   = MathUtil::clamp( static_cast<int32>( MathUtil::floor( MathUtil::saturate( uv._y ) * _resolution ) ), 0, maxTexel );
        return _listTexel[static_cast<size_t>( texelY ) * _resolution + static_cast<size_t>( texelX )];
    }

    void SurfaceMask::decay( float32 amount )
    {
        if ( amount <= 0.0f )
            return;
        bool bChanged = false;
        for ( float32& texel : _listTexel )
        {
            bChanged = bChanged || texel > 0.0f;
            texel    = MathUtil::max( 0.0f, texel - amount );
        }
        if ( bChanged )
            ++_revision;
    }
} // namespace sw

namespace sw
{
    void CharacterSurfaceState::initialize( const SurfaceChannelTable& channels, uint32 regionCount, uint32 partCount )
    {
        ++_revision;
        _listChannel = channels.getChannels();
        _regionCount = regionCount;
        _partCount   = partCount;
        _listRegionValue.assign( static_cast<size_t>( regionCount ) * _listChannel.size(), 0.0f );
        _listMask.clear();
        _listMask.resize( static_cast<size_t>( partCount ) * _listChannel.size() );
        for ( uint32 part = 0; part < partCount; ++part )
        {
            for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
            {
                _listMask[part * _listChannel.size() + channelIndex].initialize( _listChannel[channelIndex]._maskResolution );
            }
        }
    }

    bool CharacterSurfaceState::setRegionValue( uint32 region, const hashed_string& channel, float32 value )
    {
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex]._name != channel || region >= _regionCount )
                continue;
            float32&      slot    = _listRegionValue[region * _listChannel.size() + channelIndex];
            const float32 clamped = MathUtil::clamp( value, 0.0f, _listChannel[channelIndex]._maxValue );
            if ( slot != clamped )
                ++_revision;
            slot = clamped;
            return true;
        }
        return false;
    }

    bool CharacterSurfaceState::addRegionValue( uint32 region, const hashed_string& channel, float32 amount )
    {
        return setRegionValue( region, channel, getRegionValue( region, channel ) + amount );
    }

    float32 CharacterSurfaceState::getRegionValue( uint32 region, const hashed_string& channel ) const
    {
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex]._name == channel && region < _regionCount )
                return _listRegionValue[region * _listChannel.size() + channelIndex];
        }
        return 0.0f;
    }

    void CharacterSurfaceState::getRegionParameters( uint32 region, vector<float32>& outListValue ) const
    {
        outListValue.clear();
        if ( region >= _regionCount )
            return;
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            outListValue.push_back( _listRegionValue[region * _listChannel.size() + channelIndex] );
        }
    }

    bool CharacterSurfaceState::stampHit( uint32 part, uint16 region, const hashed_string& channel, const float2& uvCenter, float32 uvRadius, float32 value )
    {
        if ( part >= _partCount )
            return false;
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex]._name != channel )
                continue;
            const float32 clamped = MathUtil::clamp( value, 0.0f, _listChannel[channelIndex]._maxValue );
            if ( _listMask[part * _listChannel.size() + channelIndex].stampCircle( uvCenter, uvRadius, clamped ) > 0 )
                ++_revision;
            if ( region != CharacterGeometryConstant::kNoGroup )
                (void)addRegionValue( region, channel, clamped );
            return true;
        }
        return false;
    }

    const SurfaceMask* CharacterSurfaceState::findMask( uint32 part, const hashed_string& channel ) const
    {
        if ( part >= _partCount )
            return nullptr;
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex]._name == channel && _listChannel[channelIndex]._maskResolution > 0 )
                return &_listMask[part * _listChannel.size() + channelIndex];
        }
        return nullptr;
    }

    void CharacterSurfaceState::update( float32 deltaSeconds )
    {
        if ( deltaSeconds <= 0.0f )
            return;
        const size_t channelCount = _listChannel.size();
        for ( size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            const float32 amount = _listChannel[channelIndex]._decayPerSecond * deltaSeconds;
            if ( amount <= 0.0f )
                continue;
            for ( uint32 region = 0; region < _regionCount; ++region )
            {
                float32& value = _listRegionValue[region * channelCount + channelIndex];
                if ( value > 0.0f )
                    ++_revision;
                value = MathUtil::max( 0.0f, value - amount );
            }
            for ( uint32 part = 0; part < _partCount; ++part )
            {
                SurfaceMask& mask           = _listMask[part * channelCount + channelIndex];
                const uint32 revisionBefore = mask.getRevision();
                mask.decay( amount );
                if ( mask.getRevision() != revisionBefore )
                    ++_revision;
            }
        }
    }
} // namespace sw

namespace sw
{
    uint32 SurfaceMask::markTornTriangles( const AppearanceGeometry& geometry, float32 threshold, vector<uint8>& outListTorn ) const
    {
        outListTorn.assign( geometry.getTriangleCount(), SW_FALSE );
        if ( geometry._listUv.size() != geometry._listPosition.size() || getResolution() == 0 )
            return 0;
        uint32 tornCount = 0;
        for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
        {
            const float2& uvA = geometry._listUv[geometry._listIndex[triangle * 3]];
            const float2& uvB = geometry._listUv[geometry._listIndex[triangle * 3 + 1]];
            const float2& uvC = geometry._listUv[geometry._listIndex[triangle * 3 + 2]];
            const float2  centroid( ( uvA._x + uvB._x + uvC._x ) / 3.0f, ( uvA._y + uvB._y + uvC._y ) / 3.0f );
            const bool    bTorn = sample( uvA ) >= threshold && sample( uvB ) >= threshold && sample( uvC ) >= threshold && sample( centroid ) >= threshold;
            if ( bTorn == false )
                continue;
            outListTorn[triangle] = SW_TRUE;
            ++tornCount;
        }
        return tornCount;
    }

    /** @brief 표면 채널 표 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( SurfaceChannels, "surfacechannels", nullptr, nullptr, ".surfacechannels.xml", &ResourceDataKindUtil::loadCatalog<SurfaceChannelTable> );
} // namespace sw
