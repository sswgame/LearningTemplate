#include "pch.h"

#include "Engine/Resource/Pack/ResourcePakReader.h"

#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/array.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/Pack/ResourcePakCompressionUtil.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ResourcePakReader" );

        /** @brief 폴더 색인의 키입니다 — 아스키 소문자로 접습니다(폴더 비교가 대소문자를 무시하며 `StringUtil::startsWith` 와 같은 접기입니다). */
        string makeFolderKey( string_view folder )
        {
            string key( folder.data(), folder.size() );
            for ( utf8& ch : key )
            {
                ch = StringUtil::toLowerChar( ch );
            }
            return key;
        }
    } // namespace
} // namespace sw

namespace sw
{
    ResourcePakReader::ResourcePakReader()
        : _fileMutex{}
        , _file{}
        , _packFilePath{}
        , _header{}
        , _mapEntry{}
        , _mapFolderEntry{}
        , _stringPoolBytes{}
    {
    }

    ResourcePakReader::~ResourcePakReader()
    {
        close();
    }

    ResourcePakReader::ResourcePakReader( ResourcePakReader&& other ) noexcept
        : _fileMutex{}
        , _file{}
        , _packFilePath{}
        , _header{}
        , _mapEntry{}
        , _mapFolderEntry{}
        , _stringPoolBytes{}
    {
        std::scoped_lock<mutex> lock( other._fileMutex );
        takeFromLocked( other );
    }

    ResourcePakReader& ResourcePakReader::operator=( ResourcePakReader&& other ) noexcept
    {
        if ( this != &other )
        {
            std::scoped_lock<mutex, mutex> lock( _fileMutex, other._fileMutex );
            close();
            takeFromLocked( other );
        }
        return *this;
    }

    void ResourcePakReader::takeFromLocked( ResourcePakReader& other )
    {
        _file            = std::move( other._file );
        _packFilePath    = std::move( other._packFilePath );
        _header          = other._header;
        _mapEntry        = std::move( other._mapEntry );
        _mapFolderEntry  = std::move( other._mapFolderEntry );
        _stringPoolBytes = std::move( other._stringPoolBytes );
        other._file.close();
    }

    bool ResourcePakReader::open( string_view packFilePath )
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        close();

        if ( packFilePath.empty() )
            return false;

        const string          normalizedPath = FileUtil::normalizeSeparators( packFilePath );
        const AsyncFileHandle file           = AsyncFileHandle::open( normalizedPath );
        if ( file.isValid() == false )
        {
            SW_LOG_ERROR( "Failed to open resource pack file: %#", packFilePath );
            return false;
        }

        _file         = file;
        _packFilePath = normalizedPath;

        // 1. 헤더(64B)를 읽는다
        if ( _file.readAt( 0, &_header, sizeof( PackHeader ) ) == false )
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

        // 암호화한 팩은 받지 않는다. 풀 방법이 없으므로 플래그 · 방식을 보고 거절한다(평문으로 풀지 않는다).
        if ( ( _header._flags & static_cast<uint16>( PackFlag::Encrypted ) ) != 0 ||
             static_cast<PackEncryptionType>( _header._encryptionType ) != PackEncryptionType::None )
        {
            SW_LOG_ERROR( "Encrypted packs are not supported (encryption type %#): %#", static_cast<uint32>( _header._encryptionType ), packFilePath );
            close();
            return false;
        }

        // 2. FAT 인덱스와 스트링 풀을 읽는다
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
            const utf8* pCompressionName = engine::getSubsystem<TypeRegistry>().enumToString( compression );
            const utf8* pEncryptionName  = engine::getSubsystem<TypeRegistry>().enumToString( encryption );

            SW_LOG_INFO( "Opened pack %# (Files: %# | Compression: %# | Encryption: %#)",
                         packFilePath,
                         static_cast<uint32>( _header._fileCount ),
                         pCompressionName != nullptr ? pCompressionName : "Raw",
                         pEncryptionName != nullptr ? pEncryptionName : "None" );
        }
#endif

        return true;
    }

    void ResourcePakReader::close()
    {
        _file.close();
        _packFilePath.clear();
        _header = PackHeader{};
        _mapEntry.clear();
        _mapFolderEntry.clear();
        _stringPoolBytes.clear();
    }

    bool ResourcePakReader::isOpen() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _file.isValid();
    }

    bool ResourcePakReader::hasFile( uint64 pathHash ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _mapEntry.find( pathHash ) != _mapEntry.end();
    }

    bool ResourcePakReader::hasFile( string_view relativePath ) const
    {
        return hasFile( HashUtil::computeStringHash64( relativePath ) );
    }

    bool ResourcePakReader::getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        auto                    it = _mapEntry.find( pathHash );
        if ( it == _mapEntry.end() )
            return false;

        outEntry = it->second;
        return true;
    }

    bool ResourcePakReader::getFileEntry( string_view relativePath, PackFileEntry& outEntry ) const
    {
        return getFileEntry( HashUtil::computeStringHash64( relativePath ), outEntry );
    }

    void ResourcePakReader::collectAllEntryPaths( vector<string>& outListPath ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        outListPath.reserve( outListPath.size() + _mapEntry.size() );
        for ( const auto& mapItem : _mapEntry )
        {
            outListPath.emplace_back( mapItem.second._relativePath );
        }
    }

    void ResourcePakReader::collectFolderEntries( string_view folder, vector<string>& outListPath ) const
    {
        // 폴더 색인(`populateFolderIndex`)에서 그 폴더의 항목만 읽는다 — 팩 전체를 잠금 안에서 훑지 않는다(항목 N 개에 호출마다 O(N) 이었다).
        std::scoped_lock<mutex> lock( _fileMutex );
        const auto              itFolder = _mapFolderEntry.find( makeFolderKey( folder ) );
        if ( itFolder == _mapFolderEntry.end() )
            return;
        outListPath.reserve( outListPath.size() + itFolder->second.size() );
        for ( const uint64 pathHash : itFolder->second )
        {
            const auto itEntry = _mapEntry.find( pathHash );
            if ( itEntry != _mapEntry.end() )
                outListPath.emplace_back( itEntry->second._relativePath );
        }
    }

    void ResourcePakReader::populateFolderIndex()
    {
        _mapFolderEntry.clear();
        for ( const auto& mapItem : _mapEntry )
        {
            const string_view path( mapItem.second._relativePath );
            // 폴더는 마지막 '/' 앞까지다. '/' 가 없으면 팩 맨 위, 맨 앞 '/' 이거나 '/' 로 끝나는 경로(폴더 이름이 비거나 파일 이름이 빔)는 어느 폴더의 항목도 아니다.
            const size_t slash = path.rfind( '/' );
            if ( slash == string_view::npos )
            {
                _mapFolderEntry[string{}].push_back( mapItem.first );
                continue;
            }
            const bool bHasFolderAndName = 0 < slash && slash + 1 < path.size();
            if ( bHasFolderAndName )
                _mapFolderEntry[makeFolderKey( path.substr( 0, slash ) )].push_back( mapItem.first );
        }
    }

    bool ResourcePakReader::makeReadPlan( uint64 pathHash, PackReadPlan& outPlan, AsyncFileHandle& outFile ) const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        if ( _file.isValid() == false )
            return false;

        const auto it = _mapEntry.find( pathHash );
        if ( it == _mapEntry.end() )
            return false;

        const PackFileEntry& entry = it->second;
        outPlan._dataOffset        = entry._dataOffset;
        outPlan._compressedSize    = entry._compressedSize;
        outPlan._uncompressedSize  = entry._uncompressedSize;
        outPlan._crc32             = entry._crc32;
        outPlan._compression       = static_cast<PackCompressionType>( _header._compressionType );
        outPlan._bVerifyCrc        = ( _header._flags & static_cast<uint16>( PackFlag::HasCrc32 ) ) != 0;
        outFile                    = _file;
        return true;
    }

    bool ResourcePakReader::decodeStoredBytes( const PackReadPlan& plan, string_view packPath, vector<uint8>& storedBytes, vector<uint8>& outBytes )
    {
        // 1. 비압축(Raw) 항목은 읽은 버퍼가 곧 결과다(중간 버퍼 할당과 복사가 없다).
        if ( plan._compression == PackCompressionType::None )
        {
            if ( &storedBytes != &outBytes )
                outBytes.swap( storedBytes );
        }
        else
        {
            if ( decompressData( plan._compression, storedBytes.data(), storedBytes.size(), plan._uncompressedSize, outBytes ) == false )
            {
                SW_LOG_ERROR( "Decompression failed for entry in pack: %#", packPath );
                outBytes.clear();
                return false;
            }
        }

        // 2. CRC32 로 무결성을 검증한다. 플래그가 있으면 **늘** 대조한다. 쿠킹하는 쪽은 모든 항목의 CRC 를 적는다(빈 데이터의 CRC 가 0).
        //    저장된 값이 0 일 때 건너뛰면 그 칸을 0 으로 지우는 것만으로 검사가 꺼진다.
        if ( plan._bVerifyCrc )
        {
            const uint32 computedCrc = StringUtil::computeCrc32( outBytes.data(), outBytes.size() );
            if ( computedCrc != plan._crc32 )
            {
                SW_LOG_ERROR( "CRC32 checksum mismatch in pack %# (computed 0x%# vs expected 0x%#)", packPath,
                              Fmt( computedCrc, Format( 8, Format::Padding::Zero ).hex() ), Fmt( plan._crc32, Format( 8, Format::Padding::Zero ).hex() ) );
                outBytes.clear();
                return false;
            }
        }
        return true;
    }

    bool ResourcePakReader::readFile( uint64 pathHash, vector<uint8>& outBytes ) const
    {
        PackReadPlan    plan{};
        AsyncFileHandle file;
        if ( makeReadPlan( pathHash, plan, file ) == false )
            return false;

        // 읽기는 잠금 밖이다 — 위치 지정 읽기라 다른 스레드의 읽기와 겹쳐도 된다. 비압축 항목은 부른 쪽 버퍼로 바로 읽는다(그 버퍼의 용량을 다시 쓴다 —
        // 중간 버퍼를 잡지 않는다).
        vector<uint8>  compressedBytes;
        vector<uint8>& storedBytes = plan._compression == PackCompressionType::None ? outBytes : compressedBytes;
        storedBytes.resize( plan.getStoredSize() );
        if ( file.readAt( plan._dataOffset, storedBytes.data(), storedBytes.size() ) == false )
        {
            SW_LOG_ERROR( "Read error in pack %# (%# bytes at offset %#)", file.getPath(), storedBytes.size(), plan._dataOffset );
            outBytes.clear();
            return false;
        }
        return decodeStoredBytes( plan, file.getPath(), storedBytes, outBytes );
    }

    bool ResourcePakReader::readFile( string_view relativePath, vector<uint8>& outBytes ) const
    {
        return readFile( HashUtil::computeStringHash64( relativePath ), outBytes );
    }

    bool ResourcePakReader::readTextFile( string_view relativePath, string& outText ) const
    {
        vector<uint8> bytes;
        if ( readFile( relativePath, bytes ) == false )
            return false;

        const string_view text = FileUtil::skipUtf8Bom( string_view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() } );
        outText.assign( text.data(), text.size() );
        return true;
    }

    AsyncReadHandle ResourcePakReader::readFileAsync( AsyncFileIO& io, uint64 pathHash, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete ) const
    {
        PackReadPlan    plan{};
        AsyncFileHandle file;
        if ( makeReadPlan( pathHash, plan, file ) == false )
            return AsyncReadHandle{};

        // 완료는 리더가 아니라 계획 · 파일 핸들 사본을 든다 — 그 사이 팩을 내려도 끝까지 읽고 푼다.
        return io.readRange( file, plan._dataOffset, plan.getStoredSize(), priority,
                             SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [plan, file, onComplete]( AsyncReadResult& result )
        {
            vector<uint8> bytes;
            bool          bSuccess = false;
            if ( result.isSucceeded() )
                bSuccess = decodeStoredBytes( plan, file.getPath(), result._bytes, bytes );
            else if ( result._status != AsyncIOStatus::Canceled )
                SW_LOG_ERROR( "Read error in pack %# (%# at offset %#)", file.getPath(), AsyncFileIO::getStatusName( result._status ), plan._dataOffset );
            if ( onComplete.isBound() )
                onComplete( bSuccess, bytes );
        } ) );
    }

    const PackHeader& ResourcePakReader::getHeader() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header;
    }

    uint32 ResourcePakReader::getDlcAppID() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header._dlcAppID;
    }

    uint32 ResourcePakReader::getFileCount() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _header._fileCount;
    }

    const string& ResourcePakReader::getPackPath() const
    {
        std::scoped_lock<mutex> lock( _fileMutex );
        return _packFilePath;
    }

    bool ResourcePakReader::loadIndexTable()
    {
        if ( _file.isValid() == false || _header._fileCount == 0 )
            return true;

        uint64 fileSize{ 0 };
        if ( validateHeaderGeometry( fileSize ) == false )
            return false;

        // 항목 경로 목록(스트링 풀)은 필수다 — 폴더 나열(`collectFolderEntries`)이 이것 말고는 항목 이름을 알 길이 없다. 해시만 든 팩을
        // 받아 주면 그 팩의 폴더는 조용히 비어 보인다.
        const bool bHasStringPool = ( _header._flags & static_cast<uint16>( PackFlag::HasStringPool ) ) != 0;
        if ( bHasStringPool == false || _header._stringPoolSize == 0 )
        {
            SW_LOG_ERROR( "Pack has no entry path list (string pool) - cook it again with App --cook-packs: %#", _packFilePath );
            return false;
        }
        _stringPoolBytes.resize( _header._stringPoolSize );
        if ( _file.readAt( _header._stringPoolOffset, _stringPoolBytes.data(), _stringPoolBytes.size() ) == false )
        {
            SW_LOG_ERROR( "Failed to read string pool from pack: %#", _packFilePath );
            return false;
        }

        // FAT 인덱스 테이블(32B x FileCount)을 읽는다
        vector<PackFileEntryOnDisk> listDiskEntry;
        listDiskEntry.resize( _header._fileCount );

        const size_t expectedBytes = _header._fileCount * sizeof( PackFileEntryOnDisk );
        if ( _file.readAt( _header._indexOffset, listDiskEntry.data(), expectedBytes ) == false )
        {
            SW_LOG_ERROR( "Failed to read FAT index table from pack: %#", _packFilePath );
            return false;
        }

        _mapEntry.clear();
        _mapEntry.reserve( _header._fileCount );

        for ( const auto& diskEntry : listDiskEntry )
        {
            // **항목도 파일 안을 가리켜야 한다.** `validateHeaderGeometry` 는 헤더의 구역(인덱스 ·
            // 스트링 풀)만 측정한다. 그런데 `readFile` 은 항목이 적어 둔 크기를 그대로 `resize` 에 넣는다 —
            // 손상된 32바이트 항목 하나가 4GB 할당 요청이 된다.
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

            // **풀 안에서만 읽는다.** `const utf8*` 를 그대로 넘기면 string 이 NUL 을 찾아 풀 **밖까지** 훑는다. 종결자가 풀 안에 없거나
            // (끊긴 다운로드 · 손상) 경로의 해시가 항목과 다르면 이 팩의 나열이 읽을 수 없는 이름을 내놓으므로 팩을 거절한다.
            const string_view pool{ reinterpret_cast<const utf8*>( _stringPoolBytes.data() ), _stringPoolBytes.size() };
            const size_t      terminator = diskEntry._stringPoolOffset < pool.size() ? pool.find( '\0', diskEntry._stringPoolOffset ) : string_view::npos;
            if ( terminator == string_view::npos || terminator == diskEntry._stringPoolOffset )
            {
                SW_LOG_ERROR( "Pack entry path lies outside the string pool or is empty (offset %#): %#", static_cast<uint32>( diskEntry._stringPoolOffset ), _packFilePath );
                return false;
            }
            memEntry._relativePath.assign( pool.substr( diskEntry._stringPoolOffset, terminator - diskEntry._stringPoolOffset ) );
            if ( HashUtil::computeStringHash64( memEntry._relativePath ) != diskEntry._pathHash )
            {
                SW_LOG_ERROR( "Pack entry path '%#' does not match its path hash: %#", memEntry._relativePath, _packFilePath );
                return false;
            }

            _mapEntry.insert_or_assign( diskEntry._pathHash, std::move( memEntry ) );
        }

        populateFolderIndex();
        return true;
    }

    bool ResourcePakReader::validateHeaderGeometry( uint64& outFileSize ) const
    {
        outFileSize            = 0;
        const uint64 sizeBytes = _file.getSize();
        if ( sizeBytes == 0 )
        {
            SW_LOG_ERROR( "Cannot determine size of pack: %#", _packFilePath );
            return false;
        }
        outFileSize = sizeBytes;

        // 헤더가 인덱스 크기를 **두 번** 말한다. `_indexSize` 로 한 번, `_fileCount` 로 한 번.
        // 둘이 어긋난 팩은 리더와 쿠커가
        // 레이아웃을 다르게 보고 있다는 뜻이므로 여기서 멈춘다.
        const uint64 derivedIndexSize = static_cast<uint64>( _header._fileCount ) * sizeof( PackFileEntryOnDisk );
        if ( _header._indexSize != derivedIndexSize )
        {
            SW_LOG_ERROR( "Pack index size disagrees with file count in %# (header says %#, %# entries need %#)",
                          _packFilePath, static_cast<uint64>( _header._indexSize ), static_cast<uint32>( _header._fileCount ), derivedIndexSize );
            return false;
        }

        // 그리고 그 구역들이 실제 파일 안에 있어야 한다. 헤더의 수를 그대로 믿고 `resize` 하면
        // 잘린 팩 하나가 수십 기가짜리 할당 요청이 될 수 있다.
        if ( _header._indexOffset > sizeBytes || derivedIndexSize > sizeBytes - _header._indexOffset )
        {
            SW_LOG_ERROR( "Pack index table lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, static_cast<uint64>( _header._indexOffset ), derivedIndexSize, sizeBytes );
            return false;
        }

        if ( _header._stringPoolSize > 0 &&
             ( _header._stringPoolOffset > sizeBytes || _header._stringPoolSize > sizeBytes - _header._stringPoolOffset ) )
        {
            SW_LOG_ERROR( "Pack string pool lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, static_cast<uint64>( _header._stringPoolOffset ), static_cast<uint64>( _header._stringPoolSize ), sizeBytes );
            return false;
        }

        return true;
    }

    bool ResourcePakReader::validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 fileSize ) const
    {
        // 페이로드가 파일 안에 있어야 한다. **뺄셈으로 측정한다.** `offset + size` 는 넘칠 수 있다.
        if ( diskEntry._dataOffset > fileSize || diskEntry._compressedSize > fileSize - diskEntry._dataOffset )
        {
            SW_LOG_ERROR( "Pack entry payload lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, diskEntry._dataOffset, diskEntry._compressedSize, fileSize );
            return false;
        }

        // 비압축 팩은 `_uncompressedSize` 만큼을 **파일에서 그대로 읽는다.** 그것도 파일 안에 있어야 한다.
        if ( static_cast<PackCompressionType>( _header._compressionType ) == PackCompressionType::None &&
             diskEntry._uncompressedSize > fileSize - diskEntry._dataOffset )
        {
            SW_LOG_ERROR( "Pack entry payload lies outside the file %# (offset %#, size %#, file %#)",
                          _packFilePath, diskEntry._dataOffset, diskEntry._uncompressedSize, fileSize );
            return false;
        }

        // 압축 항목의 원본 크기는 파일 크기로 묶이지 않는다(그것이 압축의 요점이다). 상한은
        // `CompressionStream` 이 같은 이유로 이미 정해 둔 것을 쓴다. "이 컨테이너가 다루는
        // 가장 큰 조각" 의 답이 두 개일 이유가 없다.
        if ( static_cast<uint64>( diskEntry._uncompressedSize ) > CompressionStream::kMaxUncompressedSize )
        {
            SW_LOG_ERROR( "Pack entry claims an uncompressed size of %# bytes in %# — beyond what this container carries.",
                          diskEntry._uncompressedSize, _packFilePath );
            return false;
        }

        return true;
    }

    bool ResourcePakReader::decompressData( PackCompressionType type, const uint8* pSrc, size_t srcSize, size_t uncompressedSize, vector<uint8>& outBytes )
    {
        outBytes.clear();
        // 풀 것이 없으면 둘 다 0 이어야 한다. `srcSize == 0` 만 보면 압축 크기 0 · 원본 크기 N 인 항목이 0 바이트 N 개로 읽힌다.
        if ( uncompressedSize == 0 )
            return srcSize == 0;
        if ( srcSize == 0 )
            return false;

        if ( type == PackCompressionType::None )
        {
            if ( srcSize != uncompressedSize )
                return false;
            outBytes.assign( pSrc, pSrc + srcSize );
            return true;
        }

        // 팩 종류 → 코덱 종류는 표 한 곳(`ResourcePakCompressionUtil::kArrCodecMapping`), 구현은 코덱 레지스트리가 든다.
        ICompressionCodec* pCodec = ResourcePakCompressionUtil::findCodec( type );
        if ( pCodec == nullptr )
        {
            SW_LOG_ERROR( "Unsupported compression type %# in pack", static_cast<uint32>( type ) );
            return false;
        }

        // 엔트리의 원본 크기는 파일에서 온 값이다. 압축 바이트로 나올 수 없는 크기면 resize 전에 끊는다(B-5).
        if ( uncompressedSize > pCodec->decompressBound( srcSize ) )
        {
            SW_LOG_ERROR( "The original size of a %# entry exceeds the upper bound for its compressed size (%# > %#, compressed %#)", pCodec->getCodecName(),
                          static_cast<uint64>( uncompressedSize ), static_cast<uint64>( pCodec->decompressBound( srcSize ) ), static_cast<uint64>( srcSize ) );
            return false;
        }
        outBytes.resize( uncompressedSize );

        size_t decompressedSize{ 0 };
        if ( pCodec->decompress( pSrc, srcSize, outBytes.data(), uncompressedSize, decompressedSize ) == false )
            return false;

        // 엔트리 헤더가 말한 원본 크기와 실제로 푼 크기가 달라지면 그대로 쓰면 안 된다.
        if ( decompressedSize != uncompressedSize )
        {
            SW_LOG_ERROR( "The %# decompressed size differs from the entry (%# != %#)", pCodec->getCodecName(),
                          static_cast<uint64>( decompressedSize ), static_cast<uint64>( uncompressedSize ) );
            return false;
        }
        return true;
    }

} // namespace sw
