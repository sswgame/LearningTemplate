#include "pch.h"

#include "Engine/Resource/ResourcePackReader.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Container/array.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZlibCompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ResourcePackReader" );
    } // namespace
} // namespace sw

namespace sw
{
    ResourcePackReader::ResourcePackReader()
        : _fileMutex{}
        , _pFileHandle{ nullptr }
        , _packFilePath{}
        , _header{}
        , _mapEntry{}
        , _stringPoolBytes{}
    {
    }

    ResourcePackReader::~ResourcePackReader()
    {
        close();
    }

    ResourcePackReader::ResourcePackReader( ResourcePackReader&& other ) noexcept
    {
        std::scoped_lock<mutex> lock( other._fileMutex );
        _pFileHandle       = other._pFileHandle;
        _packFilePath      = std::move( other._packFilePath );
        _header            = other._header;
        _mapEntry          = std::move( other._mapEntry );
        _stringPoolBytes   = std::move( other._stringPoolBytes );
        other._pFileHandle = nullptr;
    }

    ResourcePackReader& ResourcePackReader::operator=( ResourcePackReader&& other ) noexcept
    {
        if ( this != &other )
        {
            std::scoped_lock<mutex, mutex> lock( _fileMutex, other._fileMutex );
            close();
            _pFileHandle       = other._pFileHandle;
            _packFilePath      = std::move( other._packFilePath );
            _header            = other._header;
            _mapEntry          = std::move( other._mapEntry );
            _stringPoolBytes   = std::move( other._stringPoolBytes );
            other._pFileHandle = nullptr;
        }
        return *this;
    }

    bool ResourcePackReader::open( string_view packFilePath )
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        close();

        if ( packFilePath.empty() )
            return false;

        const string normalizedPath = FileUtil::normalizeSeparators( packFilePath );
        FILE*        pFile          = PlatformFileUtil::openFile( normalizedPath.c_str(), "rb" );

        if ( pFile == nullptr )
        {
            SW_LOG_ERROR( "Failed to open resource pack file: %#", packFilePath );
            return false;
        }

        _pFileHandle  = pFile;
        _packFilePath = normalizedPath;

        // 1. 헤더(64B) 로드
        if ( std::fread( &_header, 1, sizeof( PackHeader ), pFile ) != sizeof( PackHeader ) )
        {
            SW_LOG_ERROR( "Corrupted pack header: %#", packFilePath );
            close();
            return false;
        }

        if ( _header._magic != kPackMagic )
        {
            SW_LOG_ERROR( "Invalid pack magic (0x%# vs expected 0x%#): %#",
                          Fmt( _header._magic, Format( 8, Format::Padding::Zero ).hex() ), Fmt( kPackMagic, Format( 8, Format::Padding::Zero ).hex() ), packFilePath );
            close();
            return false;
        }

        if ( _header._formatVersion != kPackFormatVersion )
        {
            SW_LOG_ERROR( "Unsupported pack format version %#: %#", _header._formatVersion, packFilePath );
            close();
            return false;
        }

        // 2. FAT 인덱스 및 스트링 풀 로드
        if ( loadIndexTable() == false )
        {
            SW_LOG_ERROR( "Failed to load FAT index table from pack: %#", packFilePath );
            close();
            return false;
        }

#if defined( SW_DEBUG )
        if ( engine::areEngineServicesBound() )
        {
            const auto  compression      = static_cast<PackCompressionType>( _header._compressionType );
            const auto  encryption       = static_cast<PackEncryptionType>( _header._encryptionType );
            const utf8* pCompressionName = engine::getTypeRegistry().enumToString( compression );
            const utf8* pEncryptionName  = engine::getTypeRegistry().enumToString( encryption );

            SW_LOG_INFO( "Opened pack %# (Files: %# | Compression: %# | Encryption: %#)",
                         packFilePath,
                         _header._fileCount,
                         pCompressionName != nullptr ? pCompressionName : "Raw",
                         pEncryptionName != nullptr ? pEncryptionName : "None" );
        }
#endif

        return true;
    }

    void ResourcePackReader::close()
    {
        if ( _pFileHandle != nullptr )
        {
            std::fclose( static_cast<FILE*>( _pFileHandle ) );
            _pFileHandle = nullptr;
        }
        _packFilePath.clear();
        _header = PackHeader{};
        _mapEntry.clear();
        _stringPoolBytes.clear();
    }

    bool ResourcePackReader::isOpen() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _pFileHandle != nullptr;
    }

    bool ResourcePackReader::hasFile( uint64 pathHash ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _mapEntry.find( pathHash ) != _mapEntry.end();
    }

    bool ResourcePackReader::hasFile( string_view relativePath ) const
    {
        return hasFile( StringUtil::computeHash64( relativePath ) );
    }

    bool ResourcePackReader::getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        auto                    it = _mapEntry.find( pathHash );
        if ( it == _mapEntry.end() )
            return false;

        outEntry = it->second;
        return true;
    }

    bool ResourcePackReader::getFileEntry( string_view relativePath, PackFileEntry& outEntry ) const
    {
        return getFileEntry( StringUtil::computeHash64( relativePath ), outEntry );
    }

    bool ResourcePackReader::readFile( uint64 pathHash, vector<uint8>& outBytes ) const
    {
        PackFileEntry entry{};
        {
            std::scoped_lock<mutex> lock( _fileMutex );
            if ( _pFileHandle == nullptr )
                return false;

            auto it = _mapEntry.find( pathHash );
            if ( it == _mapEntry.end() )
                return false;

            entry = it->second;
        }

        outBytes.resize( entry._uncompressedSize );
        const auto compression = static_cast<PackCompressionType>( _header._compressionType );

        // 1. 비압축(Raw) 에셋인 경우 outBytes 버퍼로 직접 I/O (중간 버퍼 할당 및 복사 방지)
        if ( compression == PackCompressionType::None )
        {
            std::scoped_lock<mutex> lock( _fileMutex );
            if ( _pFileHandle == nullptr )
                return false;

            auto* pFile = static_cast<FILE*>( _pFileHandle );

            PlatformFileUtil::seekTo( pFile, static_cast<int64>( entry._dataOffset ), SEEK_SET );

            const size_t readBytes = std::fread( outBytes.data(), 1, entry._uncompressedSize, pFile );
            if ( readBytes != entry._uncompressedSize )
            {
                SW_LOG_ERROR( "Read error in pack %# (read %# of %# bytes)", _packFilePath, readBytes, entry._uncompressedSize );
                outBytes.clear();
                return false;
            }
        }
        else
        {
            vector<uint8> compressedBytes;
            compressedBytes.resize( entry._compressedSize );

            {
                std::scoped_lock<mutex> lock( _fileMutex );
                if ( _pFileHandle == nullptr )
                    return false;

                auto* pFile = static_cast<FILE*>( _pFileHandle );

                PlatformFileUtil::seekTo( pFile, static_cast<int64>( entry._dataOffset ), SEEK_SET );

                const size_t readBytes = std::fread( compressedBytes.data(), 1, entry._compressedSize, pFile );
                if ( readBytes != entry._compressedSize )
                {
                    SW_LOG_ERROR( "Read error in pack %# (read %# of %# bytes)", _packFilePath, readBytes, entry._compressedSize );
                    outBytes.clear();
                    return false;
                }
            }

            // 압축 해제
            if ( decompressData( compression, compressedBytes.data(), entry._compressedSize, outBytes.data(), entry._uncompressedSize ) == false )
            {
                SW_LOG_ERROR( "Decompression failed for entry in pack: %#", _packFilePath );
                outBytes.clear();
                return false;
            }
        }

        // CRC32 무결성 검증
        const bool bHasCrc32 = ( ( _header._flags & static_cast<uint16>( PackFlag::HasCrc32 ) ) != 0 );

        if ( bHasCrc32 && entry._crc32 != 0 )
        {
            const uint32 computedCrc = StringUtil::computeCrc32( outBytes.data(), outBytes.size() );
            if ( computedCrc != entry._crc32 )
            {
                SW_LOG_ERROR( "CRC32 checksum mismatch in pack %# (computed 0x%# vs expected 0x%#)", _packFilePath,
                              Fmt( computedCrc, Format( 8, Format::Padding::Zero ).hex() ), Fmt( entry._crc32, Format( 8, Format::Padding::Zero ).hex() ) );
                outBytes.clear();
                return false;
            }
        }

        return true;
    }

    bool ResourcePackReader::readFile( string_view relativePath, vector<uint8>& outBytes ) const
    {
        return readFile( StringUtil::computeHash64( relativePath ), outBytes );
    }

    bool ResourcePackReader::readTextFile( string_view relativePath, string& outText ) const
    {
        vector<uint8> bytes;
        if ( readFile( relativePath, bytes ) == false )
            return false;

        const string_view text = FileUtil::skipUtf8Bom( string_view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() } );
        outText.assign( text.data(), text.size() );
        return true;
    }

    const PackHeader& ResourcePackReader::getHeader() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header;
    }

    uint32 ResourcePackReader::getDlcAppId() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header._dlcAppId;
    }

    uint32 ResourcePackReader::getFileCount() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header._fileCount;
    }

    const string& ResourcePackReader::getPackPath() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _packFilePath;
    }

    bool ResourcePackReader::loadIndexTable()
    {
        if ( _pFileHandle == nullptr || _header._fileCount == 0 )
            return true;

        auto* pFile = static_cast<FILE*>( _pFileHandle );

        uint64 fileSize{ 0 };
        if ( validateHeaderGeometry( fileSize ) == false )
            return false;

        // 스트링 풀 로드 (포함된 경우)
        const bool bHasStringPool = engine::areEngineServicesBound()
                                      ? engine::getTypeRegistry().hasFlag( static_cast<PackFlag>( _header._flags ), PackFlag::HasStringPool )
                                      : ( ( _header._flags & static_cast<uint16>( PackFlag::HasStringPool ) ) != 0 );

        if ( bHasStringPool && _header._stringPoolSize > 0 )
        {
            _stringPoolBytes.resize( _header._stringPoolSize );
            PlatformFileUtil::seekTo( pFile, static_cast<int64>( _header._stringPoolOffset ), SEEK_SET );
            if ( std::fread( _stringPoolBytes.data(), 1, _header._stringPoolSize, pFile ) != _header._stringPoolSize )
            {
                SW_LOG_ERROR( "Failed to read string pool from pack: %#", _packFilePath );
                return false;
            }
        }

        // FAT 인덱스 테이블(32B x FileCount) 로드
        vector<PackFileEntryOnDisk> listDiskEntry;
        listDiskEntry.resize( _header._fileCount );

        PlatformFileUtil::seekTo( pFile, static_cast<int64>( _header._indexOffset ), SEEK_SET );

        const size_t expectedBytes = _header._fileCount * sizeof( PackFileEntryOnDisk );
        if ( std::fread( listDiskEntry.data(), 1, expectedBytes, pFile ) != expectedBytes )
        {
            SW_LOG_ERROR( "Failed to read FAT index table from pack: %#", _packFilePath );
            return false;
        }

        _mapEntry.clear();
        _mapEntry.reserve( _header._fileCount );

        for ( const auto& diskEntry : listDiskEntry )
        {
            // **항목도 파일 안을 가리켜야 한다.** `validateHeaderGeometry` 가 헤더의 구역(인덱스 ·
            // 스트링 풀)을 재면서 **항목은 재지 않고 있었다** — 그런데 `readFile` 은 항목이 적어 둔
            // 크기를 그대로 `resize` 에 넣는다. 손상된 32바이트 항목 하나가 4GB 할당 요청이 된다.
            // 여기서 한 번 걸러 두면 `readFile` 은 그 값을 믿어도 된다.
            if ( validateFileEntry( diskEntry, fileSize ) == false )
                return false;

            PackFileEntry memEntry{};
            memEntry._pathHash         = diskEntry._pathHash;
            memEntry._dataOffset       = diskEntry._dataOffset;
            memEntry._compressedSize   = diskEntry._compressedSize;
            memEntry._uncompressedSize = diskEntry._uncompressedSize;
            memEntry._crc32            = diskEntry._crc32;
            memEntry._stringPoolOffset = diskEntry._stringPoolOffset;

            if ( _stringPoolBytes.empty() == false && diskEntry._stringPoolOffset < _stringPoolBytes.size() )
            {
                // **풀 안에서만 읽는다.** `const utf8*` 를 그대로 넘기면 string 이 NUL 을 찾아
                // 풀 **밖까지** 훑는다 — 마지막 문자열이 잘린 팩(끊긴 다운로드·손상)이면
                // 버퍼 밖 읽기다. 시작 오프셋만 검사해서는 끝을 보장하지 못한다.
                const utf8*       pPool = reinterpret_cast<const utf8*>( _stringPoolBytes.data() );
                const string_view raw{ pPool + diskEntry._stringPoolOffset, _stringPoolBytes.size() - diskEntry._stringPoolOffset };
                const size_t      terminator = raw.find( '\0' );
                memEntry._debugRelativePath.assign( raw.data(), terminator == string_view::npos ? raw.size() : terminator );
            }

            _mapEntry.insert_or_assign( diskEntry._pathHash, std::move( memEntry ) );
        }

        return true;
    }

    bool ResourcePackReader::validateHeaderGeometry( uint64& outFileSize ) const
    {
        outFileSize          = 0;
        const int64 fileSize = PlatformFileUtil::getOpenFileSizeAndRewind( static_cast<FILE*>( _pFileHandle ) );
        if ( fileSize <= 0 )
        {
            SW_LOG_ERROR( "Cannot determine size of pack: %#", _packFilePath );
            return false;
        }
        const uint64 sizeBytes = static_cast<uint64>( fileSize );
        outFileSize            = sizeBytes;

        // 헤더가 인덱스 크기를 **두 번** 말한다 — `_indexSize` 로 한 번, `_fileCount` 로 한 번.
        // 리더는 예전에 뒤엣것만 쓰고 앞엣것은 읽지도 않았다. 둘이 어긋난 팩은 리더와 쿠커가
        // 레이아웃을 다르게 보고 있다는 뜻이므로 여기서 멈춘다.
        const uint64 derivedIndexSize = static_cast<uint64>( _header._fileCount ) * sizeof( PackFileEntryOnDisk );
        if ( _header._indexSize != derivedIndexSize )
        {
            SW_LOG_ERROR( "Pack index size disagrees with file count in %# (header says %#, %# entries need %#)",
                          _packFilePath, _header._indexSize, _header._fileCount, derivedIndexSize );
            return false;
        }

        // 그리고 그 구역들이 실제 파일 안에 있어야 한다. 예전에는 헤더의 수를 그대로 믿고
        // `resize` 했다 — 잘린 팩 하나가 수십 기가짜리 할당 요청이 될 수 있었다.
        if ( _header._indexOffset > sizeBytes || derivedIndexSize > sizeBytes - _header._indexOffset )
        {
            SW_LOG_ERROR( "Pack index table lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, _header._indexOffset, derivedIndexSize, sizeBytes );
            return false;
        }

        if ( _header._stringPoolSize > 0 &&
             ( _header._stringPoolOffset > sizeBytes || _header._stringPoolSize > sizeBytes - _header._stringPoolOffset ) )
        {
            SW_LOG_ERROR( "Pack string pool lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, _header._stringPoolOffset, _header._stringPoolSize, sizeBytes );
            return false;
        }

        return true;
    }

    bool ResourcePackReader::validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 fileSize ) const
    {
        // 페이로드가 파일 안에 있어야 한다. **뺄셈으로 잰다** — `offset + size` 는 넘칠 수 있다.
        if ( diskEntry._dataOffset > fileSize || diskEntry._compressedSize > fileSize - diskEntry._dataOffset )
        {
            SW_LOG_ERROR( "Pack entry payload lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, diskEntry._dataOffset, diskEntry._compressedSize, fileSize );
            return false;
        }

        // 비압축 팩은 `_uncompressedSize` 만큼을 **파일에서 그대로 읽는다** — 그것도 안에 있어야 한다.
        if ( static_cast<PackCompressionType>( _header._compressionType ) == PackCompressionType::None &&
             diskEntry._uncompressedSize > fileSize - diskEntry._dataOffset )
        {
            SW_LOG_ERROR( "Pack entry payload lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, diskEntry._dataOffset, diskEntry._uncompressedSize, fileSize );
            return false;
        }

        // 압축 항목의 원본 크기는 파일 크기로 묶이지 않는다(그것이 압축의 요점이다). 상한은
        // `CompressionStream` 이 같은 이유로 이미 정해 둔 것을 쓴다 — "이 컨테이너가 다루는
        // 가장 큰 조각" 의 답이 두 개일 이유가 없다.
        if ( static_cast<uint64>( diskEntry._uncompressedSize ) > CompressionStream::kMaxUncompressedSize )
        {
            SW_LOG_ERROR( "Pack entry claims an uncompressed size of %# bytes in %# — beyond what this container carries.",
                          diskEntry._uncompressedSize, _packFilePath );
            return false;
        }

        return true;
    }

    bool ResourcePackReader::decompressData( PackCompressionType type, const uint8* pSrc, size_t srcSize, void* pDst, size_t dstSize ) const
    {
        if ( srcSize == 0 || dstSize == 0 )
            return true;

        if ( type == PackCompressionType::None )
        {
            if ( srcSize != dstSize )
                return false;
            Memory::copy( pDst, pSrc, dstSize );
            return true;
        }

        // **코덱 구현은 공유하고, 고르는 것은 팩이 자기 enum 으로 한다.**
        // 팩의 `PackCompressionType` 과 스트림의 `CompressionCodecType` 은 서로 다른 파일의 독립된
        // on-disk 포맷이라 값이 다르다 — 숫자를 건너다니지 않고 여기서 직접 고른다.
        // (예전에는 RLE 은 코덱 클래스를, zlib 은 `uncompress` 를 이 함수 안에서 직접 불렀다.)
        RleCompressionCodec  rleCodec;
        ZlibCompressionCodec zlibCodec;
        Lz4CompressionCodec  lz4Codec;
        ZstdCompressionCodec zstdCodec;

        ICompressionCodec* pCodec{ nullptr };
        switch ( type )
        {
            case PackCompressionType::RLE:
            {
                pCodec = &rleCodec;
                break;
            }
            case PackCompressionType::Zlib:
            {
                pCodec = &zlibCodec;
                break;
            }
            case PackCompressionType::LZ4:
            {
                pCodec = &lz4Codec;
                break;
            }
            case PackCompressionType::Zstd:
            {
                pCodec = &zstdCodec;
                break;
            }
            case PackCompressionType::None:   // 위에서 이미 돌려보냈다
            case PackCompressionType::Custom: // 팩을 구운 쪽이 정의하는 것 — 엔진은 모른다
            default:
                break;
        }

        if ( pCodec == nullptr )
        {
            SW_LOG_ERROR( "Unsupported compression type %# in pack", static_cast<uint32>( type ) );
            return false;
        }

        size_t decompressedSize{ 0 };
        if ( pCodec->decompress( pSrc, srcSize, pDst, dstSize, decompressedSize ) == false )
            return false;

        // 엔트리 헤더가 말한 원본 크기와 실제로 푼 크기가 달라지면 그대로 쓰면 안 된다.
        if ( decompressedSize != dstSize )
        {
            SW_LOG_ERROR( "%# 해제 크기가 엔트리와 다릅니다 (%# != %#)", pCodec->getCodecName(),
                          static_cast<uint64>( decompressedSize ), static_cast<uint64>( dstSize ) );
            return false;
        }
        return true;
    }

} // namespace sw
