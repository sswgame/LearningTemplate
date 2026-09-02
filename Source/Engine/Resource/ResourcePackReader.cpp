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

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ResourcePackReader" );

        /**
         * @struct TinyInflate
         * @brief RFC 1950 / RFC 1951 (Zlib / Raw Deflate) 표준 압축 해제기
         */
        struct TinyInflate
        {
            struct BitStream
            {
                const uint8* _pData{ nullptr };
                size_t       _size{ 0 };
                size_t       _bytePos{ 0 };
                uint32       _bitBuffer{ 0 };
                uint32       _bitsLeft{ 0 };

                uint32 getBits( uint32 count )
                {
                    while ( _bitsLeft < count )
                    {
                        if ( _bytePos >= _size )
                            return 0;
                        _bitBuffer |= static_cast<uint32>( _pData[_bytePos++] ) << _bitsLeft;
                        _bitsLeft += 8;
                    }
                    const uint32 result = _bitBuffer & ( ( 1u << count ) - 1u );
                    _bitBuffer >>= count;
                    _bitsLeft -= count;
                    return result;
                }
            };

            struct HuffmanTree
            {
                array<int16, constant::kMaxBuffer1024> _arrTree{};

                void build( const uint8* pLengths, uint32 count )
                {
                    Memory::set( _arrTree.data(), 0xFF, _arrTree.size() * sizeof( int16 ) );
                    array<uint16, constant::kMaxBuffer16> arrCount{};
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        if ( pLengths[index] > 0 && pLengths[index] < constant::kMaxBuffer16 )
                            arrCount[pLengths[index]]++;
                    }

                    array<uint16, constant::kMaxBuffer16> arrNextCode{};
                    uint16                                code = 0;
                    for ( uint32 bitCount = 1; bitCount <= 15; ++bitCount )
                    {
                        code                  = static_cast<uint16>( ( code + arrCount[bitCount - 1] ) << 1 );
                        arrNextCode[bitCount] = code;
                    }

                    int16 nextAlloc = 0;
                    for ( uint32 symbol = 0; symbol < count; ++symbol )
                    {
                        const uint32 len = pLengths[symbol];
                        if ( len == 0 )
                            continue;

                        uint16 curCode = arrNextCode[len]++;
                        int16  node    = 0;
                        for ( int32 bitIndex = static_cast<int32>( len ) - 1; bitIndex >= 0; --bitIndex )
                        {
                            const uint32 b         = ( curCode >> bitIndex ) & 1;
                            const size_t slotIndex = static_cast<size_t>( static_cast<uint32>( node ) * 2u + b );
                            if ( slotIndex >= constant::kMaxBuffer1024 )
                                return;

                            if ( bitIndex == 0 )
                            {
                                _arrTree[slotIndex] = static_cast<int16>( symbol );
                            }
                            else
                            {
                                if ( _arrTree[slotIndex] < 0 )
                                {
                                    _arrTree[slotIndex] = ++nextAlloc;
                                }
                                node = _arrTree[slotIndex];
                            }
                        }
                    }
                }

                int32 decode( BitStream& bs ) const
                {
                    int16 node = 0;
                    while ( node >= 0 )
                    {
                        const uint32 b         = bs.getBits( 1 );
                        const size_t slotIndex = static_cast<size_t>( static_cast<uint32>( node ) * 2u + b );
                        if ( slotIndex >= constant::kMaxBuffer1024 )
                            return -1;
                        const int16 val = _arrTree[slotIndex];
                        if ( val < 0 )
                            return -1;
                        if ( val < static_cast<int16>( constant::kMaxBuffer512 ) &&
                             _arrTree[static_cast<size_t>( val * 2 )] < 0 &&
                             _arrTree[static_cast<size_t>( val * 2 + 1 )] < 0 )
                        {
                            // Symbol leaf
                            return val;
                        }
                        node = val;
                    }
                    return -1;
                }
            };

            static bool inflateZlib( const uint8* pSrc, size_t srcSize, uint8* pDst, size_t dstSize )
            {
                if ( pSrc == nullptr || pDst == nullptr || dstSize == 0 )
                    return false;

                size_t offset = 0;
                // Zlib header check (CMF / FLG)
                if ( srcSize >= 2 && ( ( pSrc[0] * 256 + pSrc[1] ) % 31 == 0 ) && ( ( pSrc[0] & 0x0F ) == 8 ) )
                {
                    offset = 2; // Skip 2-byte zlib header
                }

                BitStream bs{ pSrc + offset, srcSize - offset, 0, 0, 0 };
                size_t    outPos = 0;

                while ( outPos < dstSize )
                {
                    const uint32 bFinal = bs.getBits( 1 );
                    const uint32 bType  = bs.getBits( 2 );

                    if ( bType == 0 )
                    {
                        // Uncompressed block
                        bs._bitsLeft  = 0; // Align to byte boundary
                        bs._bitBuffer = 0;
                        if ( bs._bytePos + 4 > bs._size )
                            return false;

                        const uint16 len = static_cast<uint16>( bs._pData[bs._bytePos] | ( bs._pData[bs._bytePos + 1] << 8 ) );
                        bs._bytePos += 4; // Skip LEN and NLEN
                        if ( bs._bytePos + len > bs._size || outPos + len > dstSize )
                            return false;

                        Memory::copy( pDst + outPos, bs._pData + bs._bytePos, len );
                        outPos += len;
                        bs._bytePos += len;
                    }
                    else if ( bType == 1 )
                    {
                        // Fixed Huffman block
                        array<uint8, 288> arrLitLength{};
                        for ( size_t index = 0; index <= 143; ++index )
                            arrLitLength[index] = 8;
                        for ( size_t index = 144; index <= 255; ++index )
                            arrLitLength[index] = 9;
                        for ( size_t index = 256; index <= 279; ++index )
                            arrLitLength[index] = 7;
                        for ( size_t index = 280; index <= 287; ++index )
                            arrLitLength[index] = 8;

                        HuffmanTree litTree;
                        litTree.build( arrLitLength.data(), 288 );

                        while ( outPos < dstSize )
                        {
                            const int32 symbol = litTree.decode( bs );
                            if ( symbol < 0 || symbol == 256 )
                                break;

                            if ( symbol < 256 )
                            {
                                pDst[outPos++] = static_cast<uint8>( symbol );
                            }
                            else
                            {
                                // Match length & distance
                                static constexpr uint16 kArrLenBase[29] = {
                                    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
                                static constexpr uint8 kArrLenExtra[29] = {
                                    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
                                const uint32 lenIndex = static_cast<uint32>( symbol - 257 );
                                if ( lenIndex >= 29 )
                                    return false;
                                const uint32 matchLen = kArrLenBase[lenIndex] + bs.getBits( kArrLenExtra[lenIndex] );

                                // Distance (fixed 5-bit)
                                static constexpr uint16 kArrDistBase[30] = {
                                    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
                                static constexpr uint8 kArrDistExtra[30] = {
                                    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
                                const uint32 distCode = bs.getBits( 5 );
                                if ( distCode >= 30 )
                                    return false;
                                const uint32 matchDist = kArrDistBase[distCode] + bs.getBits( kArrDistExtra[distCode] );

                                if ( matchDist > outPos )
                                    return false;

                                for ( uint32 copyIndex = 0; copyIndex < matchLen && outPos < dstSize; ++copyIndex )
                                {
                                    pDst[outPos] = pDst[outPos - matchDist];
                                    outPos++;
                                }
                            }
                        }
                    }
                    else
                    {
                        // Dynamic block or unsupported format
                        return false;
                    }

                    if ( bFinal != 0 )
                        break;
                }

                return outPos == dstSize;
            }
        };
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
            SW_LOG_ERROR( "Invalid pack magic (0x%# vs expected 0x%#): %#", _header._magic, kPackMagic, packFilePath );
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
                SW_LOG_ERROR( "CRC32 checksum mismatch in pack %# (computed 0x%# vs expected 0x%#)", _packFilePath, computedCrc, entry._crc32 );
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

        string_view sv{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() };
        // UTF-8 BOM(0xEF, 0xBB, 0xBF) 제거
        if ( sv.size() >= 3 && static_cast<uint8>( sv[0] ) == 0xEF && static_cast<uint8>( sv[1] ) == 0xBF && static_cast<uint8>( sv[2] ) == 0xBF )
        {
            sv.remove_prefix( 3 );
        }

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
            return TinyInflate::inflateZlib( pSrc, srcSize, static_cast<uint8*>( pDst ), dstSize );
        }

        SW_LOG_ERROR( "Unsupported compression type %# in pack", static_cast<uint32>( type ) );
        return false;
    }

} // namespace sw
