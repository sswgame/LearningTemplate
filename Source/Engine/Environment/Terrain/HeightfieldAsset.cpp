#include "pch.h"

#include "Engine/Environment/Terrain/HeightfieldAsset.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/FileSystem/AssetIO.h"

namespace sw
{
    SW_LOG_CALLER( "HeightfieldAsset" );

    namespace
    {
        struct HeightfieldAssetInternal
        {
            static constexpr size_t kHeaderBytes = 16;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool HeightfieldAsset::isValid() const
    {
        if ( _resolution < 2 || _resolution > kMaxResolution )
            return false;
        const size_t sampleCount = static_cast<size_t>( _resolution ) * _resolution;
        const size_t cellCount   = static_cast<size_t>( _resolution - 1 ) * ( _resolution - 1 );
        return _listHeight.size() == sampleCount && ( _listHoleCell.empty() || _listHoleCell.size() == cellCount );
    }

    bool HeightfieldAsset::isHoleCell( uint32 cellX, uint32 cellZ ) const
    {
        const bool bNoHoleAtCell = _listHoleCell.empty() || cellX + 1 >= _resolution || cellZ + 1 >= _resolution;
        if ( bNoHoleAtCell )
            return false;
        return _listHoleCell[static_cast<size_t>( cellZ ) * ( _resolution - 1 ) + cellX] != 0;
    }

    bool HeightfieldAsset::loadFromMemory( const vector<uint8>& bytes, string_view sourceName )
    {
        using Internal = HeightfieldAssetInternal;
        *this          = HeightfieldAsset{};
        if ( bytes.size() < Internal::kHeaderBytes || LittleEndianUtil::readUint32( bytes.data() ) != kMagic )
        {
            SW_LOG_ERROR( "'%#' is not a heightfield (magic SWHF missing)", sourceName );
            return false;
        }
        const uint32 version = LittleEndianUtil::readUint32( bytes.data() + 4 );
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "'%#' is heightfield version %# - this build reads version %# only (re-import it)", sourceName, version, kVersion );
            return false;
        }
        const uint32 resolution = LittleEndianUtil::readUint32( bytes.data() + 8 );
        const uint32 flags      = LittleEndianUtil::readUint32( bytes.data() + 12 );
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

    bool HeightfieldAsset::loadFromAsset( const AssetID& id )
    {
        const string_view assetID = id.view();
        vector<uint8>     bytes;
        if ( AssetIO::readBinaryAsset( id, bytes ) == false )
        {
            SW_LOG_ERROR( "Heightfield '%#' could not be read", assetID );
            return false;
        }
        return loadFromMemory( bytes, assetID );
    }

    void HeightfieldAsset::saveToMemory( vector<uint8>& outBytes ) const
    {
        using Internal = HeightfieldAssetInternal;
        outBytes.clear();
        const bool bHoles = _listHoleCell.empty() == false;
        outBytes.reserve( Internal::kHeaderBytes + _listHeight.size() * 2 + _listHoleCell.size() );
        LittleEndianUtil::appendUint32( outBytes, kMagic );
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, _resolution );
        LittleEndianUtil::appendUint32( outBytes, bHoles ? kFlagHoleMask : 0u );
        for ( const uint16 height : _listHeight )
        {
            outBytes.push_back( static_cast<uint8>( height & 0xFFu ) );
            outBytes.push_back( static_cast<uint8>( height >> 8 ) );
        }
        outBytes.insert( outBytes.end(), _listHoleCell.begin(), _listHoleCell.end() );
    }

    bool HeightfieldAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( isValid() == false )
        {
            SW_LOG_ERROR( "Heightfield for '%#' is malformed (resolution %#) - not written", absPath.str(), _resolution );
            return false;
        }
        vector<uint8> bytes;
        saveToMemory( bytes );
        return FileUtil::writeFile( absPath.str(), bytes.data(), bytes.size() );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( HeightfieldAsset, HeightfieldAsset::kAssetType );
} // namespace sw
