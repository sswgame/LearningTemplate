#include "pch.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "HeightfieldData" );

    namespace
    {
        struct HeightfieldDataInternal
        {
            static constexpr size_t kHeaderBytes = 16;

            static uint32 readUint32( const vector<uint8>& bytes, size_t offset )
            {
                return static_cast<uint32>( bytes[offset] ) | ( static_cast<uint32>( bytes[offset + 1] ) << 8 ) | ( static_cast<uint32>( bytes[offset + 2] ) << 16 ) |
                       ( static_cast<uint32>( bytes[offset + 3] ) << 24 );
            }

            static void appendUint32( vector<uint8>& outBytes, uint32 value )
            {
                outBytes.push_back( static_cast<uint8>( value & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( value >> 24 ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool HeightfieldData::isValid() const
    {
        if ( _resolution < 2 || _resolution > kMaxResolution )
            return false;
        const size_t sampleCount = static_cast<size_t>( _resolution ) * _resolution;
        const size_t cellCount   = static_cast<size_t>( _resolution - 1 ) * ( _resolution - 1 );
        return _listHeight.size() == sampleCount && ( _listHoleCell.empty() || _listHoleCell.size() == cellCount );
    }

    bool HeightfieldData::isHoleCell( uint32 cellX, uint32 cellZ ) const
    {
        if ( _listHoleCell.empty() || cellX + 1 >= _resolution || cellZ + 1 >= _resolution )
            return false;
        return _listHoleCell[static_cast<size_t>( cellZ ) * ( _resolution - 1 ) + cellX] != 0;
    }

    bool HeightfieldData::loadFromMemory( const vector<uint8>& bytes, string_view sourceName )
    {
        using Internal = HeightfieldDataInternal;
        *this          = HeightfieldData{};
        if ( bytes.size() < Internal::kHeaderBytes || Internal::readUint32( bytes, 0 ) != kMagic )
        {
            SW_LOG_ERROR( "'%#' is not a heightfield (magic SWHF missing)", sourceName );
            return false;
        }
        const uint32 version = Internal::readUint32( bytes, 4 );
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "'%#' is heightfield version %# - this build reads version %# only (re-import it)", sourceName, version, kVersion );
            return false;
        }
        const uint32 resolution = Internal::readUint32( bytes, 8 );
        const uint32 flags      = Internal::readUint32( bytes, 12 );
        if ( resolution < 2 || resolution > kMaxResolution )
        {
            SW_LOG_ERROR( "'%#' has resolution %# - expected 2..%#", sourceName, resolution, kMaxResolution );
            return false;
        }
        const size_t sampleCount = static_cast<size_t>( resolution ) * resolution;
        const size_t cellCount   = ( flags & kFlagHoleMask ) != 0u ? static_cast<size_t>( resolution - 1 ) * ( resolution - 1 ) : 0u;
        if ( bytes.size() != Internal::kHeaderBytes + sampleCount * 2 + cellCount )
        {
            SW_LOG_ERROR( "'%#' has %# bytes - resolution %# needs %#", sourceName, static_cast<uint64>( bytes.size() ), resolution,
                          static_cast<uint64>( Internal::kHeaderBytes + sampleCount * 2 + cellCount ) );
            return false;
        }
        _resolution = resolution;
        _listHeight.resize( sampleCount );
        const uint8* pSample = bytes.data() + Internal::kHeaderBytes;
        for ( size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            _listHeight[sampleIndex] = static_cast<uint16>( pSample[sampleIndex * 2] | ( pSample[sampleIndex * 2 + 1] << 8 ) );
        }
        if ( cellCount > 0 )
        {
            const uint8* pHole = pSample + sampleCount * 2;
            _listHoleCell.assign( pHole, pHole + cellCount );
        }
        return true;
    }

    bool HeightfieldData::loadFromResource( string_view resourceId )
    {
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( resourceId, bytes ) == false )
        {
            SW_LOG_ERROR( "Heightfield '%#' could not be read", resourceId );
            return false;
        }
        return loadFromMemory( bytes, resourceId );
    }

    void HeightfieldData::saveToMemory( vector<uint8>& outBytes ) const
    {
        using Internal = HeightfieldDataInternal;
        outBytes.clear();
        const bool bHoles = _listHoleCell.empty() == false;
        outBytes.reserve( Internal::kHeaderBytes + _listHeight.size() * 2 + _listHoleCell.size() );
        Internal::appendUint32( outBytes, kMagic );
        Internal::appendUint32( outBytes, kVersion );
        Internal::appendUint32( outBytes, _resolution );
        Internal::appendUint32( outBytes, bHoles ? kFlagHoleMask : 0u );
        for ( const uint16 height : _listHeight )
        {
            outBytes.push_back( static_cast<uint8>( height & 0xFFu ) );
            outBytes.push_back( static_cast<uint8>( height >> 8 ) );
        }
        outBytes.insert( outBytes.end(), _listHoleCell.begin(), _listHoleCell.end() );
    }

    bool HeightfieldData::saveToFile( string_view filePath ) const
    {
        if ( isValid() == false )
        {
            SW_LOG_ERROR( "Heightfield for '%#' is malformed (resolution %#) - not written", filePath, _resolution );
            return false;
        }
        vector<uint8> bytes;
        saveToMemory( bytes );
        return FileUtil::writeFile( filePath, bytes.data(), bytes.size() );
    }
} // namespace sw
