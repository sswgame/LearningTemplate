#include "pch.h"

#include "Engine/Resource/ResourcePackReader.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Container/array.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <zlib.h>

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
        std::lock_guard<mutex> lock( other._fileMutex );
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
        std::lock_guard<mutex> lock( _fileMutex );
        close();

        if ( packFilePath.empty() )
            return false;

        const string normalizedPath = FileUtil::normalizeSeparators( packFilePath );
        FILE*        pFile{ nullptr };

#if defined( SW_PLATFORM_WINDOWS )
        fopen_s( &pFile, normalizedPath.c_str(), "rb" );
#else
        pFile = fopen( normalizedPath.c_str(), "rb" );
#endif

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
        std::lock_guard<mutex> lock( _fileMutex );
        return _pFileHandle != nullptr;
    }

    bool ResourcePackReader::hasFile( uint64 pathHash ) const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _mapEntry.find( pathHash ) != _mapEntry.end();
    }

    bool ResourcePackReader::hasFile( string_view relativePath ) const
    {
        return hasFile( StringUtil::computeHash64( relativePath ) );
    }

    bool ResourcePackReader::getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        auto                   it = _mapEntry.find( pathHash );
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
            std::lock_guard<mutex> lock( _fileMutex );
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
            std::lock_guard<mutex> lock( _fileMutex );
            if ( _pFileHandle == nullptr )
                return false;

            auto* pFile = static_cast<FILE*>( _pFileHandle );

#if defined( SW_PLATFORM_WINDOWS )
            _fseeki64( pFile, static_cast<int64>( entry._dataOffset ), SEEK_SET );
#else
            fseeko( pFile, static_cast<off_t>( entry._dataOffset ), SEEK_SET );
#endif

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
                std::lock_guard<mutex> lock( _fileMutex );
                if ( _pFileHandle == nullptr )
                    return false;

                auto* pFile = static_cast<FILE*>( _pFileHandle );

#if defined( SW_PLATFORM_WINDOWS )
                _fseeki64( pFile, static_cast<int64>( entry._dataOffset ), SEEK_SET );
#else
                fseeko( pFile, static_cast<off_t>( entry._dataOffset ), SEEK_SET );
#endif

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

        const string_view sv = FileUtil::skipUtf8Bom( string_view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() } );
        outText.assign( sv.data(), sv.size() );
        return true;
    }

    const PackHeader& ResourcePackReader::getHeader() const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _header;
    }

    uint32 ResourcePackReader::getDlcAppId() const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _header._dlcAppId;
    }

    uint32 ResourcePackReader::getFileCount() const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _header._fileCount;
    }

    const string& ResourcePackReader::getPackPath() const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _packFilePath;
    }

    const unordered_map<uint64, PackFileEntry>& ResourcePackReader::getMapEntry() const
    {
        std::lock_guard<mutex> lock( _fileMutex );
        return _mapEntry;
    }

    bool ResourcePackReader::loadIndexTable()
    {
        if ( _pFileHandle == nullptr || _header._fileCount == 0 )
            return true;

        auto* pFile = static_cast<FILE*>( _pFileHandle );

        // 스트링 풀 로드 (포함된 경우)
        const bool bHasStringPool = engine::areEngineServicesBound()
                                      ? engine::getTypeRegistry().hasFlag( static_cast<PackFlag>( _header._flags ), PackFlag::HasStringPool )
                                      : ( ( _header._flags & static_cast<uint16>( PackFlag::HasStringPool ) ) != 0 );

        if ( bHasStringPool && _header._stringPoolSize > 0 )
        {
            _stringPoolBytes.resize( _header._stringPoolSize );
#if defined( SW_PLATFORM_WINDOWS )
            _fseeki64( pFile, static_cast<int64>( _header._stringPoolOffset ), SEEK_SET );
#else
            fseeko( pFile, static_cast<off_t>( _header._stringPoolOffset ), SEEK_SET );
#endif
            if ( std::fread( _stringPoolBytes.data(), 1, _header._stringPoolSize, pFile ) != _header._stringPoolSize )
            {
                SW_LOG_ERROR( "Failed to read string pool from pack: %#", _packFilePath );
                return false;
            }
        }

        // FAT 인덱스 테이블(32B x FileCount) 로드
        vector<PackFileEntryOnDisk> listDiskEntry;
        listDiskEntry.resize( _header._fileCount );

#if defined( SW_PLATFORM_WINDOWS )
        _fseeki64( pFile, static_cast<int64>( _header._indexOffset ), SEEK_SET );
#else
        fseeko( pFile, static_cast<off_t>( _header._indexOffset ), SEEK_SET );
#endif

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
            PackFileEntry memEntry{};
            memEntry._pathHash         = diskEntry._pathHash;
            memEntry._dataOffset       = diskEntry._dataOffset;
            memEntry._compressedSize   = diskEntry._compressedSize;
            memEntry._uncompressedSize = diskEntry._uncompressedSize;
            memEntry._crc32            = diskEntry._crc32;
            memEntry._stringPoolOffset = diskEntry._stringPoolOffset;

            if ( _stringPoolBytes.empty() == false && diskEntry._stringPoolOffset < _stringPoolBytes.size() )
            {
                memEntry._debugRelativePath = reinterpret_cast<const utf8*>( _stringPoolBytes.data() + diskEntry._stringPoolOffset );
            }

            _mapEntry.insert_or_assign( diskEntry._pathHash, std::move( memEntry ) );
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

        if ( type == PackCompressionType::RLE )
        {
            RleCompressionCodec codec;
            size_t              decompressedSize{ 0 };
            return codec.decompress( pSrc, srcSize, pDst, dstSize, decompressedSize );
        }

        if ( type == PackCompressionType::Zlib )
        {
            // 예전엔 이 파일 안에 자체 인플레이터를 들고 있었는데, 동적 허프만(BTYPE=2)을 아예
            // 거부하고 fixed 경로도 거리 코드를 LSB-first 로 읽는 등 온전하지 않았다. 팩을 한 번도
            // 못 읽던 동안 검증될 기회가 없던 코드다 — 표준 zlib 으로 대체했다.
            uLongf      destLen = static_cast<uLongf>( dstSize );
            const int32 result  = uncompress( static_cast<Bytef*>( pDst ), &destLen,
                                              reinterpret_cast<const Bytef*>( pSrc ), static_cast<uLong>( srcSize ) );
            if ( result != Z_OK )
            {
                SW_LOG_ERROR( "zlib uncompress 실패 (code %#, src %# → dst %# bytes)",
                              result, static_cast<uint32>( srcSize ), static_cast<uint32>( dstSize ) );
                return false;
            }
            return static_cast<size_t>( destLen ) == dstSize;
        }

        SW_LOG_ERROR( "Unsupported compression type %# in pack", static_cast<uint32>( type ) );
        return false;
    }

} // namespace sw
