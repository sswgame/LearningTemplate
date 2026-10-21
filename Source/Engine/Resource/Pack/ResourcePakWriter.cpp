#include "pch.h"

#include "Engine/Resource/Pack/ResourcePakWriter.h"

#include "Core/Common/HashUtil.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Pack/ResourcePakCompressionUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ResourcePakWriter" );

    namespace
    {
        /** @brief 오프셋을 섹터 경계로 올립니다. */
        uint64 alignToPackSector( uint64 offset )
        {
            return MathUtil::align( offset, static_cast<uint64>( kPackSectorAlignment ) );
        }
    } // namespace
} // namespace sw

namespace sw
{
    ResourcePakWriter::ResourcePakWriter( const ResourcePakWriterDesc& desc )
        : _listEntry{}
        , _desc{ desc }
    {
    }

    void ResourcePakWriter::addBytes( string_view relativePath, vector<uint8> bytes )
    {
        Entry entry;
        entry._relativePath = FileUtil::normalizeSeparators( relativePath );
        entry._pathHash     = HashUtil::computeStringHash64( entry._relativePath );
        entry._bytes        = std::move( bytes );
        _listEntry.push_back( std::move( entry ) );
    }

    bool ResourcePakWriter::compressEntry( const Entry& entry, vector<uint8>& outPayloadBytes ) const
    {
        outPayloadBytes.clear();
        if ( _desc._compression == PackCompressionType::None || entry._bytes.empty() )
        {
            outPayloadBytes = entry._bytes;
            return true;
        }

        // 팩 종류 → 코덱 종류는 리더와 같은 표(`ResourcePakCompressionUtil::kArrCodecMapping`)다.
        ICompressionCodec* pCodec = ResourcePakCompressionUtil::findCodec( _desc._compression );
        if ( pCodec == nullptr )
        {
            SW_LOG_ERROR( "Pack compression type %# has no registered codec", static_cast<uint32>( _desc._compression ) );
            return false;
        }
        const size_t bound = pCodec->compressBound( entry._bytes.size() );
        if ( bound == 0 )
        {
            SW_LOG_ERROR( "%# cannot compress '%#' (%# bytes is beyond the codec limit)", pCodec->getCodecName(), entry._relativePath,
                          static_cast<uint64>( entry._bytes.size() ) );
            return false;
        }
        outPayloadBytes.resize( bound );
        size_t compressedSize{ 0 };
        if ( pCodec->compress( entry._bytes.data(), entry._bytes.size(), outPayloadBytes.data(), bound, compressedSize, _desc._compressionLevel ) == false )
        {
            SW_LOG_ERROR( "%# failed to compress '%#'", pCodec->getCodecName(), entry._relativePath );
            outPayloadBytes.clear();
            return false;
        }
        outPayloadBytes.resize( compressedSize );
        return true;
    }

    bool ResourcePakWriter::makePackBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();

        // FAT 은 경로 해시 오름차순이다. 같은 해시가 둘이면 리더가 하나만 들므로 실패로 멈춘다.
        vector<const Entry*> listSorted;
        listSorted.reserve( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            listSorted.push_back( &entry );
        }
        std::stable_sort( listSorted.begin(), listSorted.end(), []( const Entry* pLeft, const Entry* pRight )
        { return pLeft->_pathHash < pRight->_pathHash; } );
        for ( size_t index = 1; index < listSorted.size(); ++index )
        {
            if ( listSorted[index]->_pathHash == listSorted[index - 1]->_pathHash )
            {
                SW_LOG_ERROR( "Pack entry '%#' is added twice (same path hash as '%#')", listSorted[index]->_relativePath, listSorted[index - 1]->_relativePath );
                return false;
            }
        }

        const uint64 indexOffset     = alignToPackSector( sizeof( PackHeader ) );
        const uint64 indexSize       = static_cast<uint64>( listSorted.size() ) * sizeof( PackFileEntryOnDisk );
        const uint64 dataStartOffset = alignToPackSector( indexOffset + indexSize );

        vector<PackFileEntryOnDisk> listDiskEntry;
        listDiskEntry.reserve( listSorted.size() );
        vector<uint8> stringPoolBytes;
        vector<uint8> dataBytes;
        vector<uint8> payloadBytes;
        uint64        dataEndOffset = dataStartOffset;
        for ( const Entry* pEntry : listSorted )
        {
            const bool bFitsEntryFields = pEntry->_bytes.size() <= MathUtil::kMaxUInt32 && stringPoolBytes.size() <= MathUtil::kMaxUInt32;
            if ( bFitsEntryFields == false )
            {
                SW_LOG_ERROR( "Pack entry '%#' does not fit the 32-bit entry fields", pEntry->_relativePath );
                return false;
            }
            if ( compressEntry( *pEntry, payloadBytes ) == false )
                return false;
            if ( payloadBytes.size() > MathUtil::kMaxUInt32 )
            {
                SW_LOG_ERROR( "Compressed pack entry '%#' does not fit the 32-bit entry fields", pEntry->_relativePath );
                return false;
            }

            const uint64 payloadOffset = alignToPackSector( dataEndOffset );
            dataBytes.resize( static_cast<size_t>( payloadOffset - dataStartOffset ), 0 );
            dataBytes.insert( dataBytes.end(), payloadBytes.begin(), payloadBytes.end() );
            dataEndOffset = payloadOffset + payloadBytes.size();

            PackFileEntryOnDisk diskEntry{};
            diskEntry._pathHash         = pEntry->_pathHash;
            diskEntry._dataOffset       = payloadOffset;
            diskEntry._compressedSize   = static_cast<uint32>( payloadBytes.size() );
            diskEntry._uncompressedSize = static_cast<uint32>( pEntry->_bytes.size() );
            diskEntry._crc32            = StringUtil::computeCrc32( pEntry->_bytes.data(), pEntry->_bytes.size() );
            diskEntry._stringPoolOffset = static_cast<uint32>( stringPoolBytes.size() );
            listDiskEntry.push_back( diskEntry );

            // 항목 경로 목록은 늘 담는다 — 런타임 폴더 나열이 이것으로 항목 이름을 알고, 리더는 없는 팩을 거절한다.
            stringPoolBytes.insert( stringPoolBytes.end(), pEntry->_relativePath.begin(), pEntry->_relativePath.end() );
            stringPoolBytes.push_back( 0 );
        }

        const uint64 stringPoolOffset = stringPoolBytes.empty() ? 0 : alignToPackSector( dataEndOffset );

        PackHeader header{};
        header._magic            = kPackMagic;
        header._formatVersion    = kPackFormatVersion;
        header._dlcAppID         = _desc._dlcAppID;
        header._compressionType  = static_cast<uint8>( _desc._compression );
        header._encryptionType   = static_cast<uint8>( PackEncryptionType::None );
        header._sectorAlignment  = kPackSectorAlignment;
        header._flags            = static_cast<uint16>( PackFlag::HasCrc32 ) | static_cast<uint16>( PackFlag::HasStringPool );
        header._fileCount        = static_cast<uint32>( listDiskEntry.size() );
        header._indexOffset      = indexOffset;
        header._indexSize        = indexSize;
        header._stringPoolOffset = stringPoolOffset;
        header._stringPoolSize   = stringPoolBytes.size();
        header._totalDataSize    = dataEndOffset - dataStartOffset;

        const uint64 fileSize = stringPoolBytes.empty() ? dataEndOffset : stringPoolOffset + stringPoolBytes.size();
        outBytes.resize( static_cast<size_t>( fileSize ), 0 );
        Memory::copy( outBytes.data(), &header, sizeof( PackHeader ) );
        if ( listDiskEntry.empty() == false )
            Memory::copy( outBytes.data() + indexOffset, listDiskEntry.data(), static_cast<size_t>( indexSize ) );
        if ( dataBytes.empty() == false )
            Memory::copy( outBytes.data() + dataStartOffset, dataBytes.data(), dataBytes.size() );
        if ( stringPoolBytes.empty() == false )
            Memory::copy( outBytes.data() + stringPoolOffset, stringPoolBytes.data(), stringPoolBytes.size() );
        return true;
    }

    bool ResourcePakWriter::writePackFile( string_view packPath ) const
    {
        vector<uint8> bytes;
        if ( makePackBytes( bytes ) == false )
            return false;
        if ( FileUtil::ensureParentDirectoryExists( packPath ) == false || FileUtil::writeFile( packPath, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Could not write pack file '%#'", packPath );
            return false;
        }
        return true;
    }
} // namespace sw
