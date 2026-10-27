#include "pch.h"

#include "Engine/Environment/Terrain/LayerWeightsData.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "LayerWeightsData" );

    namespace
    {
        struct LayerWeightsDataInternal
        {
            static constexpr size_t kHeaderBytes = 20;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LayerWeightsData::isValid() const
    {
        const bool bSizeInRange = _width >= 1 && _width <= kMaxResolution && _height >= 1 && _height <= kMaxResolution;
        return bSizeInRange && _bytes.size() == static_cast<size_t>( _width ) * _height * kLayerCount;
    }

    bool LayerWeightsData::loadFromMemory( const vector<uint8>& bytes, string_view sourceName )
    {
        using Internal = LayerWeightsDataInternal;
        *this          = LayerWeightsData{};
        if ( bytes.size() < Internal::kHeaderBytes || LittleEndianUtil::readUint32( bytes.data() ) != kMagic )
        {
            SW_LOG_ERROR( "'%#' is not a layer weight map (magic SWLW missing)", sourceName );
            return false;
        }
        // 옛 판(레이어 넷, 버전 1)은 읽지 않는다 — 저장소의 파일은 현재 판으로 다시 썼다.
        const uint32 version = LittleEndianUtil::readUint32( bytes.data() + 4 );
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "'%#' is layer weight map version %# - this build reads version %# only", sourceName, version, kVersion );
            return false;
        }
        const uint32 width      = LittleEndianUtil::readUint32( bytes.data() + 8 );
        const uint32 height     = LittleEndianUtil::readUint32( bytes.data() + 12 );
        const uint32 layerCount = LittleEndianUtil::readUint32( bytes.data() + 16 );
        const bool   bSizeOk    = width >= 1 && width <= kMaxResolution && height >= 1 && height <= kMaxResolution;
        if ( bSizeOk == false || layerCount != kLayerCount )
        {
            SW_LOG_ERROR( "'%#' is %#x%# with %# layers - expected 1..%# per side and %# layers", sourceName, width, height, layerCount, kMaxResolution, kLayerCount );
            return false;
        }
        const size_t weightBytes = static_cast<size_t>( width ) * height * kLayerCount;
        if ( bytes.size() != Internal::kHeaderBytes + weightBytes )
        {
            SW_LOG_ERROR( "'%#' has %# bytes - %#x%# needs %#", sourceName, static_cast<uint64>( bytes.size() ), width, height,
                          static_cast<uint64>( Internal::kHeaderBytes + weightBytes ) );
            return false;
        }
        _width  = width;
        _height = height;
        _bytes.assign( bytes.begin() + static_cast<ptrdiff_t>( Internal::kHeaderBytes ), bytes.end() );
        return true;
    }

    bool LayerWeightsData::loadFromResource( string_view resourceID )
    {
        vector<uint8> bytes;
        if ( ResourceIO::readBinaryResource( ResourceID{ resourceID }, bytes ) == false )
        {
            SW_LOG_ERROR( "Layer weight map '%#' could not be read", resourceID );
            return false;
        }
        return loadFromMemory( bytes, resourceID );
    }

    void LayerWeightsData::saveToMemory( vector<uint8>& outBytes ) const
    {
        using Internal = LayerWeightsDataInternal;
        outBytes.clear();
        outBytes.reserve( Internal::kHeaderBytes + _bytes.size() );
        LittleEndianUtil::appendUint32( outBytes, kMagic );
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, _width );
        LittleEndianUtil::appendUint32( outBytes, _height );
        LittleEndianUtil::appendUint32( outBytes, kLayerCount );
        outBytes.insert( outBytes.end(), _bytes.begin(), _bytes.end() );
    }

    bool LayerWeightsData::saveToFile( string_view filePath ) const
    {
        if ( isValid() == false )
        {
            SW_LOG_ERROR( "Layer weight map for '%#' is malformed (%#x%#, %# bytes) - not written", filePath, _width, _height, static_cast<uint64>( _bytes.size() ) );
            return false;
        }
        vector<uint8> bytes;
        saveToMemory( bytes );
        return FileUtil::writeFile( filePath, bytes.data(), bytes.size() );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( LayerWeightsData, LayerWeightsData::kAssetType );
} // namespace sw
