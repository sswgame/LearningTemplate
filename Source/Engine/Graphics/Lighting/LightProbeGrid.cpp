#include "pch.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    namespace
    {
        struct LightProbeGridInternal
        {
            /// @brief 머리말 바이트 수 — 매직 · 버전 · 시작점 · 간격 · 프로브 수 셋.
            static constexpr size_t kHeaderBytes = sizeof( uint32 ) * 2 + sizeof( float32 ) * 6 + sizeof( uint32 ) * 3;
            /// @brief 프로브 하나의 바이트 수입니다.
            static constexpr size_t kProbeBytes = sizeof( float32 ) * SH9Color::kCoefficientCount;
            /// @brief 한 파일의 프로브 수 상한 — 깨진 머리말이 큰 할당을 일으키지 않게 막는다(256 × 64 × 256).
            static constexpr uint64 kMaxProbeCount = 4194304;

            static void writeUint32( vector<uint8>& outBytes, uint32 value )
            {
                for ( uint32 shift = 0; shift < 32; shift += 8 )
                {
                    outBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            static void writeFloat32( vector<uint8>& outBytes, float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                writeUint32( outBytes, bits );
            }

            static uint32 readUint32( const uint8* pData, size_t& inoutOffset )
            {
                const uint8* pByte = pData + inoutOffset;
                inoutOffset += sizeof( uint32 );
                return static_cast<uint32>( pByte[0] ) | ( static_cast<uint32>( pByte[1] ) << 8 ) | ( static_cast<uint32>( pByte[2] ) << 16 ) |
                       ( static_cast<uint32>( pByte[3] ) << 24 );
            }

            static float32 readFloat32( const uint8* pData, size_t& inoutOffset )
            {
                const uint32 bits  = readUint32( pData, inoutOffset );
                float32      value = 0.0f;
                Memory::copy( &value, &bits, sizeof( value ) );
                return value;
            }

            static void setError( string* pOutError, const utf8* pMessage )
            {
                if ( pOutError != nullptr )
                    *pOutError = pMessage;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float3 LightProbeGridLayout::computeProbePosition( uint32 probeIndex ) const
    {
        const uint32 cellX = probeIndex % _countX;
        const uint32 cellY = ( probeIndex / _countX ) % _countY;
        const uint32 cellZ = probeIndex / ( _countX * _countY );
        return float3{ _origin._x + _spacing._x * static_cast<float32>( cellX ), _origin._y + _spacing._y * static_cast<float32>( cellY ),
                       _origin._z + _spacing._z * static_cast<float32>( cellZ ) };
    }

    bool LightProbeGridLayout::matches( const LightProbeGridLayout& other, float32 tolerance ) const
    {
        const bool bSameCount   = _countX == other._countX && _countY == other._countY && _countZ == other._countZ;
        const bool bSameOrigin  = ( _origin - other._origin ).getLength() <= tolerance;
        const bool bSameSpacing = ( _spacing - other._spacing ).getLength() <= tolerance;
        return bSameCount && bSameOrigin && bSameSpacing;
    }
} // namespace sw

namespace sw
{
    bool LightProbeGrid::isValid( string* pOutError ) const
    {
        using Internal                = LightProbeGridInternal;
        const bool bPositiveSpacing   = _layout._spacing._x > 0.0f && _layout._spacing._y > 0.0f && _layout._spacing._z > 0.0f;
        const bool bHasProbe          = _layout._countX > 0 && _layout._countY > 0 && _layout._countZ > 0;
        const bool bMatchesProbeCount = _listProbe.size() == static_cast<size_t>( _layout.getProbeCount() );
        if ( bPositiveSpacing == false )
        {
            Internal::setError( pOutError, "probe spacing must be positive on every axis" );
            return false;
        }
        if ( bHasProbe == false )
        {
            Internal::setError( pOutError, "probe grid has no probe on some axis" );
            return false;
        }
        if ( bMatchesProbeCount == false )
        {
            Internal::setError( pOutError, "probe list does not match the grid size" );
            return false;
        }
        return true;
    }

    void LightProbeGrid::clear()
    {
        _layout = LightProbeGridLayout{};
        _listProbe.clear();
    }

    void LightProbeGrid::makeBytes( vector<uint8>& outBytes ) const
    {
        using Internal = LightProbeGridInternal;
        outBytes.clear();
        outBytes.reserve( Internal::kHeaderBytes + Internal::kProbeBytes * _listProbe.size() );
        Internal::writeUint32( outBytes, kMagic );
        Internal::writeUint32( outBytes, kVersion );
        Internal::writeFloat32( outBytes, _layout._origin._x );
        Internal::writeFloat32( outBytes, _layout._origin._y );
        Internal::writeFloat32( outBytes, _layout._origin._z );
        Internal::writeFloat32( outBytes, _layout._spacing._x );
        Internal::writeFloat32( outBytes, _layout._spacing._y );
        Internal::writeFloat32( outBytes, _layout._spacing._z );
        Internal::writeUint32( outBytes, _layout._countX );
        Internal::writeUint32( outBytes, _layout._countY );
        Internal::writeUint32( outBytes, _layout._countZ );
        for ( const SH9Color& probe : _listProbe )
        {
            for ( const float32 coefficient : probe._arrCoefficient )
            {
                Internal::writeFloat32( outBytes, coefficient );
            }
        }
    }

    bool LightProbeGrid::readFromBytes( const uint8* pData, size_t size, string* pOutError )
    {
        using Internal = LightProbeGridInternal;
        clear();
        if ( pData == nullptr || size < Internal::kHeaderBytes )
        {
            Internal::setError( pOutError, "probe file is shorter than its header" );
            return false;
        }
        size_t offset = 0;
        if ( Internal::readUint32( pData, offset ) != kMagic )
        {
            Internal::setError( pOutError, "probe file magic is not SWLP" );
            return false;
        }
        if ( Internal::readUint32( pData, offset ) != kVersion )
        {
            Internal::setError( pOutError, "probe file version differs - bake the volume again" );
            return false;
        }
        LightProbeGridLayout layout{};
        layout._origin._x         = Internal::readFloat32( pData, offset );
        layout._origin._y         = Internal::readFloat32( pData, offset );
        layout._origin._z         = Internal::readFloat32( pData, offset );
        layout._spacing._x        = Internal::readFloat32( pData, offset );
        layout._spacing._y        = Internal::readFloat32( pData, offset );
        layout._spacing._z        = Internal::readFloat32( pData, offset );
        layout._countX            = Internal::readUint32( pData, offset );
        layout._countY            = Internal::readUint32( pData, offset );
        layout._countZ            = Internal::readUint32( pData, offset );
        const uint64 probeCount   = static_cast<uint64>( layout._countX ) * layout._countY * layout._countZ;
        const bool   bSizeMatches = probeCount <= Internal::kMaxProbeCount && size - offset == probeCount * Internal::kProbeBytes;
        if ( bSizeMatches == false )
        {
            Internal::setError( pOutError, "probe file length does not match its grid size" );
            return false;
        }
        _layout = layout;
        _listProbe.resize( static_cast<size_t>( probeCount ) );
        for ( SH9Color& probe : _listProbe )
        {
            for ( float32& coefficient : probe._arrCoefficient )
            {
                coefficient = Internal::readFloat32( pData, offset );
            }
        }
        if ( isValid( pOutError ) == false )
        {
            clear();
            return false;
        }
        return true;
    }

    bool LightProbeGrid::saveToFile( string_view path ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool LightProbeGrid::loadFromResource( string_view path, string* pOutError )
    {
        clear();
        vector<uint8> bytes;
        if ( ResourceIO::readBinaryResource( path, bytes ) == false )
        {
            LightProbeGridInternal::setError( pOutError, "probe file could not be read" );
            return false;
        }
        return readFromBytes( bytes.data(), bytes.size(), pOutError );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( LightProbeGrid, LightProbeGrid::kAssetType );
} // namespace sw
