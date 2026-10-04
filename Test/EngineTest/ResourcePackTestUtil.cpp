#include "pch.h"

#include "EngineTest/ResourcePackTestUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/PackCompressionUtil.h"
#include "Engine/Resource/ResourcePackTypes.h"

#include <cstdio>

namespace sw
{
    bool createTestPackFile( const string& packPath, uint32 dlcAppId, PackCompressionType compression, const vector<sw::pair<string, string>>& listFileContent,
                             bool bIncludeDebugStringPool )
    {
        FileUtil::ensureParentDirectoryExists( packPath );

        FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        fopen_s( &pFile, packPath.c_str(), "wb" );
#else
        pFile = fopen( packPath.c_str(), "wb" );
#endif
        if ( pFile == nullptr )
            return false;

        PackHeader header{};
        header._magic           = kPackMagic;
        header._formatVersion   = kPackFormatVersion;
        header._dlcAppId        = dlcAppId;
        header._compressionType = static_cast<uint8>( compression );
        header._encryptionType  = static_cast<uint8>( PackEncryptionType::None );
        header._sectorAlignment = kPackSectorAlignment;
        header._flags           = static_cast<uint16>( PackFlag::HasCrc32 );
        if ( bIncludeDebugStringPool )
            header._flags |= static_cast<uint16>( PackFlag::HasStringPool );
        header._fileCount = static_cast<uint32>( listFileContent.size() );

        // 페이로드 임시 버퍼 준비
        vector<uint8>               dataStreamBytes;
        vector<PackFileEntryOnDisk> listDiskEntry;
        vector<utf8>                stringPoolBytes;

        uint64 curOffset = kPackSectorAlignment; // 헤더 이후 첫 데이터 블록은 4096에서 시작

        for ( const auto& [relPath, content] : listFileContent )
        {
            const uint64 pathHash   = StringUtil::computeHash64( relPath );
            const uint32 uncompSize = static_cast<uint32>( content.size() );
            const uint32 crc        = StringUtil::computeCrc32( content.data(), uncompSize );

            // 코덱은 리더와 같은 길로 찾는다(팩 종류 → 코덱 종류 표 + 등록부). None 은 그대로 싣는다.
            const bool         bCompressed = compression != PackCompressionType::None;
            ICompressionCodec* pCodec      = bCompressed ? PackCompressionUtil::findCodec( compression ) : nullptr;
            if ( bCompressed && pCodec == nullptr )
                return false;

            vector<uint8> compressedPayloadBytes;
            if ( pCodec != nullptr && uncompSize > 0 )
            {
                const size_t bound = pCodec->compressBound( uncompSize );
                compressedPayloadBytes.resize( bound );
                size_t compSize = 0;
                if ( pCodec->compress( content.data(), uncompSize, compressedPayloadBytes.data(), bound, compSize ) == false )
                    return false;
                compressedPayloadBytes.resize( compSize );
            }
            else
            {
                const auto* pContentBytes = reinterpret_cast<const uint8*>( content.data() );
                compressedPayloadBytes.assign( pContentBytes, pContentBytes + content.size() );
            }

            const uint32 compSize      = static_cast<uint32>( compressedPayloadBytes.size() );
            const uint64 alignedOffset = MathUtil::align( curOffset, static_cast<uint64>( kPackSectorAlignment ) );

            // 패딩 추가
            const size_t padding = static_cast<size_t>( alignedOffset - curOffset );
            if ( padding > 0 )
                dataStreamBytes.insert( dataStreamBytes.end(), padding, 0 );

            dataStreamBytes.insert( dataStreamBytes.end(), compressedPayloadBytes.begin(), compressedPayloadBytes.end() );
            curOffset = alignedOffset + compSize;

            PackFileEntryOnDisk diskEntry{};
            diskEntry._pathHash         = pathHash;
            diskEntry._dataOffset       = alignedOffset;
            diskEntry._compressedSize   = compSize;
            diskEntry._uncompressedSize = uncompSize;
            diskEntry._crc32            = crc;

            if ( bIncludeDebugStringPool )
            {
                diskEntry._stringPoolOffset = static_cast<uint32>( stringPoolBytes.size() );
                stringPoolBytes.insert( stringPoolBytes.end(), relPath.begin(), relPath.end() );
                stringPoolBytes.push_back( '\0' );
            }

            listDiskEntry.push_back( diskEntry );
        }

        header._totalDataSize = dataStreamBytes.size();
        header._indexOffset   = MathUtil::align( static_cast<uint64>( kPackSectorAlignment ) + header._totalDataSize, uint64{ 64 } );
        header._indexSize     = listDiskEntry.size() * sizeof( PackFileEntryOnDisk );

        header._stringPoolOffset = header._indexOffset + header._indexSize;
        header._stringPoolSize   = stringPoolBytes.size();

        // 1. 헤더(64B) 기록
        std::fwrite( &header, 1, sizeof( PackHeader ), pFile );

        // 2. 4096 섹터 경계까지 패딩
        const size_t headerPadding = kPackSectorAlignment - sizeof( PackHeader );
        const auto   zeroBuf       = vector<uint8>( headerPadding, 0 );
        std::fwrite( zeroBuf.data(), 1, headerPadding, pFile );

        // 3. 페이로드 기록
        if ( dataStreamBytes.empty() == false )
            std::fwrite( dataStreamBytes.data(), 1, dataStreamBytes.size(), pFile );

        // 4. FAT 인덱스까지 패딩
        const size_t fatPadding = static_cast<size_t>( header._indexOffset - ( kPackSectorAlignment + header._totalDataSize ) );
        if ( fatPadding > 0 )
        {
            const auto fatPadBuf = vector<uint8>( fatPadding, 0 );
            std::fwrite( fatPadBuf.data(), 1, fatPadding, pFile );
        }

        // 5. FAT 인덱스 테이블 기록
        if ( listDiskEntry.empty() == false )
            std::fwrite( listDiskEntry.data(), 1, listDiskEntry.size() * sizeof( PackFileEntryOnDisk ), pFile );

        // 6. 스트링 풀 기록
        if ( bIncludeDebugStringPool && stringPoolBytes.empty() == false )
            std::fwrite( stringPoolBytes.data(), 1, stringPoolBytes.size(), pFile );

        std::fclose( pFile );
        return true;
    }
} // namespace sw
