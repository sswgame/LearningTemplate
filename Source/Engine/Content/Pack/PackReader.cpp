#include "pch.h"

#include "Engine/Content/Pack/PackReader.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/array.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/Pack/PackCompressionUtil.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/FormatDef.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PackReaderInternal
        {
            /**
             * @brief 항목 경로의 폴더(마지막 '/' 앞)입니다. '/' 가 없으면 팩 맨 위(빈 글)이고, 맨 앞 '/' 이거나 '/' 로 끝나는 경로(폴더 이름이 비거나 파일 이름이 빔)는
             *        어느 폴더의 항목도 아니라 nullopt 입니다.
             */
            static optional<string_view> findFolder( string_view path )
            {
                const size_t slash = path.rfind( '/' );
                if ( slash == string_view::npos )
                    return string_view{};
                const bool bHasFolderAndName = 0 < slash && slash + 1 < path.size();
                if ( bHasFolderAndName == false )
                    return nullopt;
                return path.substr( 0, slash );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PackReader::PackReader()
        : _fileMutex{}
        , _file{}
        , _packFilePath{}
        , _header{}
        , _listEntry{}
        , _listBlock{}
        , _listFolderEntry{}
        , _mountDomain{}
        , _stringPoolBytes{}
        , _listBlockDigest{}
        , _pDigestProvider{ nullptr }
        , _bDigestRequired{ SW_FALSE }
    {
    }

    PackReader::~PackReader()
    {
        close();
    }

    PackReader::PackReader( PackReader&& other ) noexcept
        : _fileMutex{}
        , _file{}
        , _packFilePath{}
        , _header{}
        , _listEntry{}
        , _listBlock{}
        , _listFolderEntry{}
        , _mountDomain{}
        , _stringPoolBytes{}
        , _listBlockDigest{}
        , _pDigestProvider{ nullptr }
        , _bDigestRequired{ SW_FALSE }
    {
        // 새 객체의 잠금도 잡는다 — 아직 아무도 보지 않지만, 그래야 넘겨받는 칸이 `_fileMutex` 아래라는 선언과 맞는다.
        ScopedLockPair<mutex> lock{ _fileMutex, other._fileMutex };
        takeFromLocked( other );
    }

    PackReader& PackReader::operator=( PackReader&& other ) noexcept
    {
        if ( this != &other )
        {
            ScopedLockPair<mutex> lock{ _fileMutex, other._fileMutex };
            closeLocked();
            takeFromLocked( other );
        }
        return *this;
    }

    void PackReader::takeFromLocked( PackReader& other )
    {
        _file            = std::move( other._file );
        _packFilePath    = std::move( other._packFilePath );
        _header          = other._header;
        _listEntry       = std::move( other._listEntry );
        _listBlock       = std::move( other._listBlock );
        _listFolderEntry = std::move( other._listFolderEntry );
        _mountDomain     = std::move( other._mountDomain );
        _stringPoolBytes = std::move( other._stringPoolBytes );
        _listBlockDigest = std::move( other._listBlockDigest );
        _pDigestProvider = other._pDigestProvider;
        _bDigestRequired = other._bDigestRequired;
        other._file.close();
    }

    bool PackReader::open( string_view packFilePath, const PackIndexTrust* pTrust )
    {
        // 잠금은 잎이다 — 오류 · 안내 로그는 잠금을 놓은 뒤에 쓴다(실패 경로는 먼저 닫고 놓고, 콘텐츠 소스가 남기는 줄은 미룬다).
        const ScopedDeferredLog deferredLog;
        UniqueLock<mutex>       lock{ _fileMutex };
        closeLocked();

        if ( packFilePath.empty() )
            return false;

        const string          normalizedPath = FileUtil::normalizeSeparators( packFilePath );
        const AsyncFileHandle file           = ContentFileSource::getCurrent().open( normalizedPath );
        if ( file.isValid() == false )
        {
            lock.unlock();
            SW_LOG_ERROR_ID( "PackReader.FailedOpenResourcePack", "Failed to open resource pack file: %#", packFilePath );
            return false;
        }

        _file         = file;
        _packFilePath = normalizedPath;

        // 1. 헤더(96B)를 읽는다 — 파일의 마지막 96 바이트다(`PackTypes.h` 머리말)
        const uint64 fileSize = _file.getSize();
        if ( fileSize < sizeof( PackHeader ) || _file.readAt( fileSize - sizeof( PackHeader ), &_header, sizeof( PackHeader ) ) == false )
        {
            closeLocked();
            lock.unlock();
            SW_LOG_ERROR_ID( "PackReader.CorruptedPackHeader", "Corrupted pack header: %#", packFilePath );
            return false;
        }

        if ( const expected<void> head = kPackFormat.check( _header._magic, _header._formatVersion ); head.has_value() == false )
        {
            closeLocked();
            lock.unlock();
            return FormatDef::report( head, packFilePath );
        }

        // 암호화한 팩은 받지 않는다. 풀 방법이 없으므로 플래그 · 방식을 보고 거절한다(평문으로 풀지 않는다).
        if ( ( _header._flags & static_cast<uint16>( PackFlag::Encrypted ) ) != 0 ||
             static_cast<PackEncryptionKind>( _header._encryptionType ) != PackEncryptionKind::None )
        {
            const auto encryptionType = static_cast<uint32>( _header._encryptionType );
            closeLocked();
            lock.unlock();
            SW_LOG_ERROR_ID( "PackReader.EncryptedPacksNotSupported", "Encrypted packs are not supported (encryption type %#): %#", encryptionType, packFilePath );
            return false;
        }

        // 2. FAT 인덱스와 스트링 풀을 읽는다
        if ( loadIndexTable( pTrust ) == false )
        {
            closeLocked();
            lock.unlock();
            SW_LOG_ERROR_ID( "PackReader.FailedLoadFATIndex", "Failed to load FAT index table from pack: %#", packFilePath );
            return false;
        }

#if defined( SW_DO_CHECKS )
        const auto compression = static_cast<PackCompressionKind>( _header._compressionType ); // 헤더는 잠금 안에서 복사한다
        const auto encryption  = static_cast<PackEncryptionKind>( _header._encryptionType );
        const auto fileCount   = static_cast<uint32>( _header._fileCount );
#endif
        lock.unlock(); // 잠금은 잎이다 — 안내 로그는 놓은 뒤에 쓴다
#if defined( SW_DO_CHECKS )
        if ( engine::areEngineServicesBound() )
        {
            const utf8* pCompressionName = engine::getSubsystem<TypeRegistry>().enumToString( compression );
            const utf8* pEncryptionName  = engine::getSubsystem<TypeRegistry>().enumToString( encryption );

            SW_LOG_INFO_ID( "PackReader.OpenedPackFilesCompression", "Opened pack %# (Files: %# | Compression: %# | Encryption: %#)",
                            packFilePath,
                            fileCount,
                            pCompressionName != nullptr ? pCompressionName : "Raw",
                            pEncryptionName != nullptr ? pEncryptionName : "None" );
        }
#endif

        return true;
    }

    void PackReader::close()
    {
        // 읽기 함수들은 `_fileMutex` 를 쥐고 상태를 읽는다 — 닫기도 같은 잠금 아래에서 지운다(관리자의 내리기는 다른 스레드의 읽기와 겹친다).
        ScopedLock<mutex> lock{ _fileMutex };
        closeLocked();
    }

    void PackReader::closeLocked()
    {
        _file.close();
        _packFilePath.clear();
        _header = PackHeader{};
        _listEntry.clear();
        _listBlock.clear();
        _listFolderEntry.clear();
        _mountDomain.clear();
        _stringPoolBytes.clear();
        _listBlockDigest.clear();
        _pDigestProvider = nullptr;
        _bDigestRequired = SW_FALSE;
    }

    void PackReader::releaseDigestProvider()
    {
        ScopedLock<mutex> lock{ _fileMutex };
        _pDigestProvider = nullptr;
    }

    bool PackReader::isOpen() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _file.isValid();
    }

    optional<PackReader::EntryIndex> PackReader::findEntryIndex( uint64 pathHash ) const
    {
        // FAT 은 경로 해시 오름차순이고 같은 해시가 없다(열 때 확인했다) — 이진 탐색이다. 30 만 항목에 19 단계.
        const auto it = std::lower_bound( _listEntry.begin(), _listEntry.end(), pathHash, []( const PackFileEntryOnDisk& entry, uint64 hash )
        { return entry._pathHash < hash; } );
        if ( it == _listEntry.end() || it->_pathHash != pathHash )
            return nullopt;
        return static_cast<EntryIndex>( it - _listEntry.begin() );
    }

    string_view PackReader::getEntryPath( EntryIndex entryIndex ) const
    {
        const string_view pool{ _stringPoolBytes.data(), _stringPoolBytes.size() };
        const uint32      offset = _listEntry[entryIndex]._stringPoolOffset;
        return pool.substr( offset, pool.find( '\0', offset ) - offset );
    }

    bool PackReader::hasFile( uint64 pathHash ) const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return findEntryIndex( pathHash ).has_value();
    }

    bool PackReader::hasFile( string_view relativePath ) const
    {
        return hasFile( HashUtil::computeStringHash64( relativePath ) );
    }

    bool PackReader::getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const
    {
        ScopedLock<mutex>          lock{ _fileMutex };
        const optional<EntryIndex> entryIndex = findEntryIndex( pathHash );
        if ( entryIndex.has_value() == false )
            return false;
        const PackFileEntryOnDisk& diskEntry = _listEntry[*entryIndex];
        outEntry._pathHash                   = diskEntry._pathHash;
        outEntry._dataOffset                 = diskEntry.getDataOffset();
        outEntry._storage                    = static_cast<PackEntryStorage>( diskEntry.getStorageValue() ); // 열 때 범위를 확인했다
        outEntry._compressedSize             = diskEntry._compressedSize;
        outEntry._uncompressedSize           = diskEntry._uncompressedSize;
        outEntry._firstBlock                 = diskEntry._firstBlock;
        outEntry._relativePath.assign( getEntryPath( *entryIndex ) );
        return true;
    }

    bool PackReader::getFileEntry( string_view relativePath, PackFileEntry& outEntry ) const
    {
        return getFileEntry( HashUtil::computeStringHash64( relativePath ), outEntry );
    }

    void PackReader::collectAllEntryPaths( vector<string>& outListPath ) const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        outListPath.reserve( outListPath.size() + _listEntry.size() );
        for ( EntryIndex entryIndex = 0; entryIndex < _listEntry.size(); ++entryIndex )
        {
            outListPath.emplace_back( getEntryPath( entryIndex ) );
        }
    }

    void PackReader::collectPathHashes( vector<uint64>& outListPathHash ) const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        outListPathHash.reserve( outListPathHash.size() + _listEntry.size() );
        for ( const PackFileEntryOnDisk& entry : _listEntry )
        {
            outListPathHash.push_back( entry._pathHash );
        }
    }

    void PackReader::collectFolderEntries( string_view folder, vector<string>& outListPath ) const
    {
        // 폴더 색인(`populateFolderIndex`)에서 그 폴더의 항목만 읽는다 — 팩 전체를 잠금 안에서 훑지 않는다. 경로 해시는 대소문자를 무시하므로 폴더 비교도 그렇다.
        const uint64      folderHash = HashUtil::computeStringHash64( folder );
        ScopedLock<mutex> lock{ _fileMutex };
        const auto        range = std::equal_range( _listFolderEntry.begin(), _listFolderEntry.end(), FolderEntry{ folderHash, 0 },
                                                    []( const FolderEntry& left, const FolderEntry& right )
               { return left._folderHash < right._folderHash; } );
        for ( auto it = range.first; it != range.second; ++it )
        {
            // 해시가 같은 다른 폴더(충돌)는 글로 거른다.
            const string_view           path      = getEntryPath( it->_entryIndex );
            const optional<string_view> entryRoot = PackReaderInternal::findFolder( path );
            if ( entryRoot.has_value() && StringUtil::equals( *entryRoot, folder, true ) )
                outListPath.emplace_back( path );
        }
    }

    void PackReader::populateFolderIndex()
    {
        _listFolderEntry.clear();
        _listFolderEntry.reserve( _listEntry.size() );
        for ( EntryIndex entryIndex = 0; entryIndex < _listEntry.size(); ++entryIndex )
        {
            const optional<string_view> folder = PackReaderInternal::findFolder( getEntryPath( entryIndex ) );
            if ( folder.has_value() )
                _listFolderEntry.push_back( FolderEntry{ HashUtil::computeStringHash64( *folder ), entryIndex } );
        }
        VectorUtil::sort( _listFolderEntry, []( const FolderEntry& left, const FolderEntry& right )
        { return left._folderHash != right._folderHash ? left._folderHash < right._folderHash : left._entryIndex < right._entryIndex; } );
    }

    optional<uint64> PackReader::findFileSize( uint64 pathHash ) const
    {
        ScopedLock<mutex>          lock{ _fileMutex };
        const optional<EntryIndex> entryIndex = findEntryIndex( pathHash );
        if ( entryIndex.has_value() == false )
            return nullopt;
        return static_cast<uint64>( _listEntry[*entryIndex]._uncompressedSize );
    }

    PackReader::ReadPlanResult PackReader::makeReadPlan( uint64 pathHash, uint64 offset, uint64 size, PackReadPlan& outPlan, AsyncFileHandle& outFile ) const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        if ( _file.isValid() == false )
            return ReadPlanResult::Missing;
        const optional<EntryIndex> entryIndex = findEntryIndex( pathHash );
        if ( entryIndex.has_value() == false )
            return ReadPlanResult::Missing;

        const PackFileEntryOnDisk& entry   = _listEntry[*entryIndex];
        const uint64               rawSize = entry._uncompressedSize;
        const uint64               length  = size == kToEntryEnd && offset <= rawSize ? rawSize - offset : size;
        if ( offset > rawSize || length > rawSize - offset )
            return ReadPlanResult::OutsideEntry;

        // 걸친 블록 [first, last] 만 읽는다. 열 때 항목의 블록 줄이 표 안이고 누적 끝이 오르는 것을 확인했다.
        const uint32 blockSize    = _header._blockSize;
        const uint32 firstBlock   = static_cast<uint32>( offset / blockSize );
        const uint32 endBlock     = length == 0 ? firstBlock : static_cast<uint32>( ( offset + length - 1 ) / blockSize + 1 );
        const size_t tableBegin   = static_cast<size_t>( entry._firstBlock ) + firstBlock;
        outPlan._storedBegin      = firstBlock == 0 || length == 0 ? 0 : _listBlock[tableBegin - 1]._compressedEnd;
        outPlan._storedOffset     = entry.getDataOffset() + outPlan._storedBegin;
        outPlan._firstBlockIndex  = firstBlock;
        outPlan._uncompressedSize = entry._uncompressedSize;
        outPlan._blockSize        = blockSize;
        outPlan._sliceOffset      = static_cast<uint32>( offset - static_cast<uint64>( firstBlock ) * blockSize );
        outPlan._sliceSize        = static_cast<uint32>( length );
        // `Stored` 항목은 팩 코덱과 상관없이 블록의 저장 바이트가 원본이다 — 풀기 없이 복사만 한다.
        const bool bStored   = static_cast<PackEntryStorage>( entry.getStorageValue() ) == PackEntryStorage::Stored;
        outPlan._compression = bStored ? PackCompressionKind::None : static_cast<PackCompressionKind>( _header._compressionType );
        outPlan._bVerifyCRC  = ( _header._flags & static_cast<uint16>( PackFlag::HasCRC32 ) ) != 0;
        outPlan._listBlock.assign( _listBlock.begin() + tableBegin, _listBlock.begin() + tableBegin + ( endBlock - firstBlock ) );
        outPlan._pDigestProvider = _pDigestProvider;
        if ( _bDigestRequired == SW_TRUE ) // 신뢰를 받아 열었으면 블록 해시 표가 있다(`loadIndexTable`)
            outPlan._listBlockDigest.assign( _listBlockDigest.begin() + tableBegin, _listBlockDigest.begin() + tableBegin + ( endBlock - firstBlock ) );
        outFile = _file;
        return ReadPlanResult::Ready;
    }

    bool PackReader::decodeStoredBytes( const PackReadPlan& plan, string_view packPath, const vector<uint8>& storedBytes, vector<uint8>& outBytes )
    {
        outBytes.clear();
        // 블록 k(항목 안 번호 first + k)의 저장 바이트는 [끝(k-1), 끝(k)) 에서 계획의 시작(`_storedBegin`)을 뺀 자리다.
        ICompressionCodec* pCodec = nullptr;
        if ( plan._compression != PackCompressionKind::None )
        {
            // 팩 종류 → 코덱 종류는 표 한 곳(`PackCompressionUtil::kArrCodecMapping`), 구현은 코덱 레지스트리가 든다.
            pCodec = PackCompressionUtil::findCodec( plan._compression );
            if ( pCodec == nullptr )
            {
                SW_LOG_ERROR_ID( "PackReader.UnsupportedCompressionTypePack", "Unsupported compression type %# in pack: %#", static_cast<uint32>( plan._compression ), packPath );
                return false;
            }
        }
        const auto getRawSize = [&plan]( size_t blockOrdinal ) -> size_t
        {
            const uint64 rawBegin = static_cast<uint64>( plan._firstBlockIndex + blockOrdinal ) * plan._blockSize;
            return static_cast<size_t>( plan._uncompressedSize - rawBegin < plan._blockSize ? plan._uncompressedSize - rawBegin : plan._blockSize );
        };
        // 원본 크기는 파일에서 온 값이다. 블록마다 저장 바이트로 나올 수 있는 크기인지 본 뒤에만 resize 한다(B-5).
        uint32 storedBegin = plan._storedBegin;
        size_t decodedSize = 0;
        for ( size_t ordinal = 0; ordinal < plan._listBlock.size(); ++ordinal )
        {
            const uint32 storedSize = plan._listBlock[ordinal]._compressedEnd - storedBegin;
            const size_t rawSize    = getRawSize( ordinal );
            const bool   bFits      = pCodec != nullptr ? rawSize <= pCodec->decompressBound( storedSize ) : rawSize == storedSize;
            if ( bFits == false )
            {
                SW_LOG_ERROR_ID( "PackReader.OriginalSizeEntryExceeds", "A block's original size does not fit its stored size in pack %# (%# from %# stored bytes)", packPath,
                                 static_cast<uint64>( rawSize ), storedSize );
                return false;
            }
            storedBegin = plan._listBlock[ordinal]._compressedEnd;
            decodedSize += rawSize;
        }
        if ( storedBytes.size() != storedBegin - plan._storedBegin )
            return false;

        // 통째 읽기는 결과 버퍼에 바로 풀고, 범위 읽기는 걸친 블록을 푼 뒤 그 범위만 남긴다.
        outBytes.resize( decodedSize );
        storedBegin = plan._storedBegin;
        for ( size_t ordinal = 0; ordinal < plan._listBlock.size(); ++ordinal )
        {
            const PackBlockEntryOnDisk& block      = plan._listBlock[ordinal];
            const uint8*                pStored    = storedBytes.data() + ( storedBegin - plan._storedBegin );
            const size_t                storedSize = block._compressedEnd - storedBegin;
            uint8*                      pRaw       = outBytes.data() + ordinal * plan._blockSize;
            const size_t                rawSize    = getRawSize( ordinal );
            storedBegin                            = block._compressedEnd;
            // 서명 채널이면 풀기 전에 저장 바이트의 요약을 견준다 — 바뀐 바이트가 코덱에 닿지 않는다.
            if ( plan._listBlockDigest.empty() == false )
            {
                if ( plan._pDigestProvider == nullptr )
                {
                    SW_LOG_ERROR_ID( "PackReader.DigestProviderRetired", "A block of a signed pack cannot be checked - the crypto provider is gone: %#", packPath );
                    outBytes.clear();
                    return false;
                }
                PackDigest actualDigest;
                const bool bHashed = plan._pDigestProvider->computeSHA256( pStored, static_cast<int32>( storedSize ), actualDigest._arrByte );
                if ( bHashed == false || actualDigest != plan._listBlockDigest[ordinal] )
                {
                    SW_LOG_ERROR_ID( "PackReader.BlockDigestMismatch", "Block %# of a signed pack does not match its digest - the pack was changed after signing: %#",
                                     static_cast<uint64>( plan._firstBlockIndex + ordinal ), packPath );
                    outBytes.clear();
                    return false;
                }
            }
            size_t decoded{ rawSize };
            if ( pCodec == nullptr )
                Memory::copy( pRaw, pStored, rawSize );
            else if ( pCodec->decompress( pStored, storedSize, pRaw, rawSize, decoded ) == false || decoded != rawSize )
            {
                SW_LOG_ERROR_ID( "PackReader.DecompressionFailedEntryPack", "Decompression failed for block %# of an entry in pack: %#", static_cast<uint64>( plan._firstBlockIndex + ordinal ), packPath );
                outBytes.clear();
                return false;
            }
            // CRC32 로 블록마다 검증한다. 플래그가 있으면 **늘** 대조한다 — 저장된 값이 0 일 때 건너뛰면 그 칸을 0 으로 지우는 것만으로 검사가 꺼진다.
            const uint32 expectedCRC = block._crc32; // 묶음(pack 1) 칸은 참조로 넘기지 않는다
            const uint32 computedCRC = plan._bVerifyCRC ? HashUtil::computeCRC32( pRaw, rawSize ) : expectedCRC;
            if ( computedCRC != expectedCRC )
            {
                SW_LOG_ERROR_ID( "PackReader.CRC32ChecksumMismatchPack", "CRC32 checksum mismatch in block %# of pack %# (computed 0x%# vs expected 0x%#)",
                                 static_cast<uint64>( plan._firstBlockIndex + ordinal ), packPath, Fmt( computedCRC, Format( 8, Format::Padding::Zero ).hex() ),
                                 Fmt( expectedCRC, Format( 8, Format::Padding::Zero ).hex() ) );
                outBytes.clear();
                return false;
            }
        }
        if ( plan._sliceOffset != 0 || plan._sliceSize != outBytes.size() )
        {
            // 빈 조각은 옮기지 않는다 — 빈 버퍼의 data() 는 nullptr 일 수 있어 오프셋을 더하면 UB 다(UBSan, Linux sanitize).
            if ( plan._sliceSize != 0 )
                Memory::move( outBytes.data(), outBytes.data() + plan._sliceOffset, plan._sliceSize );
            outBytes.resize( plan._sliceSize );
        }
        return true;
    }

    bool PackReader::readPlanned( uint64 pathHash, uint64 offset, uint64 size, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        PackReadPlan         plan{};
        AsyncFileHandle      file;
        const ReadPlanResult planned = makeReadPlan( pathHash, offset, size, plan, file );
        if ( planned == ReadPlanResult::OutsideEntry )
            SW_LOG_ERROR_ID( "PackReader.RangeOutsideEntry", "Pack range read [%#, +%#) lies outside the entry with path hash %#", offset, size, pathHash );
        if ( planned != ReadPlanResult::Ready )
            return false;

        // 읽기는 잠금 밖이다 — 위치 지정 읽기라 다른 스레드의 읽기와 겹쳐도 된다.
        vector<uint8> storedBytes;
        storedBytes.resize( plan.getStoredSize() );
        if ( file.readAt( plan._storedOffset, storedBytes.data(), storedBytes.size() ) == false )
        {
            SW_LOG_ERROR_ID( "PackReader.ReadErrorPackBytes", "Read error in pack %# (%# bytes at offset %#)", file.getPath(), storedBytes.size(), plan._storedOffset );
            return false;
        }
        return decodeStoredBytes( plan, file.getPath(), storedBytes, outBytes );
    }

    bool PackReader::readFile( uint64 pathHash, vector<uint8>& outBytes ) const
    {
        return readPlanned( pathHash, 0, kToEntryEnd, outBytes );
    }

    bool PackReader::readRange( uint64 pathHash, uint64 offset, uint64 size, vector<uint8>& outBytes ) const
    {
        return readPlanned( pathHash, offset, size, outBytes );
    }

    bool PackReader::readFile( string_view relativePath, vector<uint8>& outBytes ) const
    {
        return readFile( HashUtil::computeStringHash64( relativePath ), outBytes );
    }

    bool PackReader::readTextFile( string_view relativePath, string& outText ) const
    {
        vector<uint8> bytes;
        if ( readFile( relativePath, bytes ) == false )
            return false;

        const string_view text = FileUtil::skipUTF8Bom( string_view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() } );
        outText.assign( text.data(), text.size() );
        return true;
    }

    AsyncReadHandle PackReader::readFileAsync( AsyncFileIO& io, uint64 pathHash, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const
    {
        return readPlannedAsync( io, pathHash, 0, kToEntryEnd, priority, onComplete );
    }

    AsyncReadHandle PackReader::readRangeAsync( AsyncFileIO& io, uint64 pathHash, uint64 offset, uint64 size, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const
    {
        return readPlannedAsync( io, pathHash, offset, size, priority, onComplete );
    }

    AsyncReadHandle PackReader::readPlannedAsync( AsyncFileIO& io, uint64 pathHash, uint64 offset, uint64 size, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const
    {
        PackReadPlan         plan{};
        AsyncFileHandle      file;
        const ReadPlanResult planned = makeReadPlan( pathHash, offset, size, plan, file );
        if ( planned == ReadPlanResult::OutsideEntry )
            SW_LOG_ERROR_ID( "PackReader.RangeOutsideEntry", "Pack range read [%#, +%#) lies outside the entry with path hash %#", offset, size, pathHash );
        if ( planned != ReadPlanResult::Ready )
            return AsyncReadHandle{};

        // 완료는 리더가 아니라 계획 · 파일 핸들 사본을 든다 — 그 사이 팩을 내려도 끝까지 읽고 푼다.
        return io.readRange( file, plan._storedOffset, plan.getStoredSize(), priority,
                             SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [plan, file, onComplete]( AsyncReadResult& result )
        {
            vector<uint8> bytes;
            bool          bSuccess = false;
            if ( result.isSucceeded() )
                bSuccess = decodeStoredBytes( plan, file.getPath(), result._bytes, bytes );
            else if ( result._status != AsyncIOStatus::Canceled )
                SW_LOG_ERROR_ID( "PackReader.ReadErrorPackOffset", "Read error in pack %# (%# at offset %#)", file.getPath(), AsyncFileIO::getStatusName( result._status ), plan._storedOffset );
            if ( onComplete.isBound() )
                onComplete( bSuccess, bytes );
        } ) );
    }

    PackHeader PackReader::getHeader() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _header;
    }

    uint32 PackReader::getDLCAppID() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _header._dlcAppID;
    }

    uint32 PackReader::getFileCount() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _header._fileCount;
    }

    string PackReader::getPackPath() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _packFilePath;
    }

    string PackReader::getMountDomain() const
    {
        ScopedLock<mutex> lock{ _fileMutex };
        return _mountDomain;
    }

    bool PackReader::loadIndexTable( const PackIndexTrust* pTrust )
    {
        // 항목이 없는 팩도 풀(마운트 도메인)은 읽는다.
        if ( _file.isValid() == false )
            return true;

        const uint64 fileSize = _file.getSize();
        if ( validateHeaderGeometry( fileSize ) == false )
            return false;

        // 항목 경로 목록(스트링 풀)은 필수다 — 폴더 나열(`collectFolderEntries`)이 이것 말고는 항목 이름을 알 길이 없다. 해시만 든 팩을
        // 받아 주면 그 팩의 폴더는 조용히 비어 보인다.
        const bool bHasStringPool = ( _header._flags & static_cast<uint16>( PackFlag::HasStringPool ) ) != 0;
        if ( bHasStringPool == false || _header._stringPoolSize == 0 )
        {
            SW_LOG_ERROR_ID( "PackReader.PackNoEntryPath", "Pack has no entry path list (string pool) - cook it again with App --cook-packs: %#", _packFilePath );
            return false;
        }

        // 색인 구역(FAT · 블록 표 · 블록 해시 표 · 스트링 풀 · 헤더)은 `_indexOffset` 부터 파일 끝까지 빈틈 없는 한 구간이다(`validateHeaderGeometry`).
        // 한 번 읽어 요약을 견주고 그 바이트에서 색인을 꺼낸다 — 검증한 바이트가 곧 쓰는 바이트다(확인과 사용 사이의 틈이 없다).
        vector<uint8> listIndexByte;
        listIndexByte.resize( static_cast<size_t>( fileSize - _header._indexOffset ) );
        if ( _file.readAt( _header._indexOffset, listIndexByte.data(), listIndexByte.size() ) == false )
        {
            SW_LOG_ERROR_ID( "PackReader.FailedReadFATIndex", "Failed to read FAT index table from pack: %#", _packFilePath );
            return false;
        }
        const bool bHasBlockHash = ( _header._flags & static_cast<uint16>( PackFlag::HasBlockHash ) ) != 0;
        if ( pTrust != nullptr )
        {
            PackDigest actualDigest;
            const bool bHashed = listIndexByte.size() <= static_cast<size_t>( MathUtil::kMaxInt32 ) &&
                                 pTrust->_pProvider->computeSHA256( listIndexByte.data(), static_cast<int32>( listIndexByte.size() ), actualDigest._arrByte );
            if ( bHashed == false || actualDigest != pTrust->_indexDigest )
            {
                SW_LOG_ERROR_ID( "PackReader.IndexDigestMismatch", "Pack index does not match its signed digest - the pack was changed after signing: %#", _packFilePath );
                return false;
            }
            // 헤더는 요약보다 먼저 따로 읽었다 — 요약한 바이트의 헤더와 같아야 그 헤더를 믿는다.
            if ( Memory::compare( listIndexByte.data() + listIndexByte.size() - sizeof( PackHeader ), &_header, sizeof( PackHeader ) ) != 0 )
            {
                SW_LOG_ERROR_ID( "PackReader.IndexChangedWhileOpening", "Pack header changed while the pack was opened: %#", _packFilePath );
                return false;
            }
            // 서명 채널의 팩은 블록마다 요약을 든다 — 없으면 색인만 믿고 페이로드는 CRC(손상 검사)로만 보게 된다.
            if ( bHasBlockHash == false )
            {
                SW_LOG_ERROR_ID( "PackReader.TrustedPackWithoutBlockHashes", "Signed pack has no block hash table - make the patch again with a signer: %#", _packFilePath );
                return false;
            }
            _pDigestProvider = pTrust->_pProvider;
            _bDigestRequired = SW_TRUE;
        }
        // 잠금을 쥔 이 함수에서 값을 떠 넘긴다 — 람다 본문은 쥔 잠금을 물려받지 않는다(스레드 안전성 분석).
        const uint64 indexOffset   = _header._indexOffset;
        const auto   copyIndexPart = [indexOffset, &listIndexByte]( uint64 offset, void* pDest, size_t size )
        {
            if ( size > 0 )
                Memory::copy( pDest, listIndexByte.data() + ( offset - indexOffset ), size );
        };
        vector<PackFileEntryOnDisk> listDiskEntry( _header._fileCount );
        _listBlock.resize( _header._blockCount );
        _listBlockDigest.resize( bHasBlockHash ? _header._blockCount : 0 );
        _stringPoolBytes.resize( static_cast<size_t>( _header._stringPoolSize ) );
        copyIndexPart( _header._indexOffset, listDiskEntry.data(), listDiskEntry.size() * sizeof( PackFileEntryOnDisk ) );
        copyIndexPart( _header._blockTableOffset, _listBlock.data(), _listBlock.size() * sizeof( PackBlockEntryOnDisk ) );
        copyIndexPart( _header._blockHashOffset, _listBlockDigest.data(), _listBlockDigest.size() * sizeof( PackDigest ) );
        copyIndexPart( _header._stringPoolOffset, _stringPoolBytes.data(), _stringPoolBytes.size() );

        const string_view pool{ _stringPoolBytes.data(), _stringPoolBytes.size() };
        const size_t      domainEnd = pool.find( '\0' );
        if ( domainEnd == string_view::npos )
        {
            SW_LOG_ERROR_ID( "PackReader.PackNoMountDomain", "Pack has no mount domain at the start of its string pool: %#", _packFilePath );
            return false;
        }
        _mountDomain.assign( pool.substr( 0, domainEnd ) );

        // FAT 은 경로 해시 오름차순이어야 한다 — 리더는 정렬을 믿고 이진 탐색한다. 같은 해시 둘도 거절한다(어느 쪽이 이길지 조용히 정하지 않는다).
        // 항목 검사보다 먼저 FAT 전체를 본다 — 순서가 바뀐 FAT 은 블록 줄도 어긋나므로, 뒤에서 보면 원인 대신 블록 불일치로 보고된다.
        for ( size_t entryIndex = 1; entryIndex < listDiskEntry.size(); ++entryIndex )
        {
            if ( listDiskEntry[entryIndex]._pathHash <= listDiskEntry[entryIndex - 1]._pathHash )
            {
                SW_LOG_ERROR_ID( "PackReader.UnsortedIndex", "Pack index is not in ascending path hash order (entry %#) - cook it again: %#", static_cast<uint64>( entryIndex ), _packFilePath );
                return false;
            }
        }
        uint32 expectedFirstBlock = 0;
        for ( size_t entryIndex = 0; entryIndex < listDiskEntry.size(); ++entryIndex )
        {
            const PackFileEntryOnDisk& diskEntry = listDiskEntry[entryIndex];
            // **항목도 페이로드 구역 안을 가리켜야 하고 블록 줄이 맞아야 한다.** `readFile` 은 항목 · 블록이 적어 둔 크기를 그대로 `resize` 에 넣는다 —
            // 손상된 32바이트 항목 하나가 4GB 할당 요청이 된다. 여기서 한 번 걸러 두면 `readFile` 은 그 값을 믿어도 된다.
            if ( validateFileEntry( diskEntry, _header._indexOffset, expectedFirstBlock ) == false )
                return false;
            expectedFirstBlock += static_cast<uint32>( ( static_cast<uint64>( diskEntry._uncompressedSize ) + _header._blockSize - 1 ) / _header._blockSize );

            // **풀 안에서만 읽는다.** `const utf8*` 를 그대로 넘기면 string 이 NUL 을 찾아 풀 **밖까지** 훑는다. 종결자가 풀 안에 없거나
            // (끊긴 다운로드 · 손상) 경로의 해시가 항목과 다르면 이 팩의 나열이 읽을 수 없는 이름을 내놓으므로 팩을 거절한다.
            const size_t terminator = diskEntry._stringPoolOffset < pool.size() ? pool.find( '\0', diskEntry._stringPoolOffset ) : string_view::npos;
            if ( terminator == string_view::npos || terminator == diskEntry._stringPoolOffset )
            {
                SW_LOG_ERROR_ID( "PackReader.PackEntryPathLies", "Pack entry path lies outside the string pool or is empty (offset %#): %#", static_cast<uint32>( diskEntry._stringPoolOffset ), _packFilePath );
                return false;
            }
            const string_view path = pool.substr( diskEntry._stringPoolOffset, terminator - diskEntry._stringPoolOffset );
            if ( HashUtil::computeStringHash64( path ) != diskEntry._pathHash )
            {
                SW_LOG_ERROR_ID( "PackReader.PackEntryPathDoes", "Pack entry path '%#' does not match its path hash: %#", path, _packFilePath );
                return false;
            }
        }
        // 블록 표는 항목들의 블록을 FAT 순서로 빈틈 없이 잇는다 — 남는 줄이 있으면 쓰는 쪽과 읽는 쪽이 형식을 다르게 본다.
        if ( expectedFirstBlock != _header._blockCount )
        {
            SW_LOG_ERROR_ID( "PackReader.PackBlockTableDisagrees", "Pack block table has %# blocks but its entries use %#: %#", static_cast<uint32>( _header._blockCount ), expectedFirstBlock, _packFilePath );
            return false;
        }

        _listEntry = std::move( listDiskEntry );
        populateFolderIndex();
        return true;
    }

    bool PackReader::validateHeaderGeometry( uint64 fileSize ) const
    {
        // 헤더가 인덱스 크기를 **두 번** 말한다. `_indexSize` 로 한 번, `_fileCount` 로 한 번.
        // 둘이 어긋난 팩은 리더와 쿠커가 레이아웃을 다르게 보고 있다는 뜻이므로 여기서 멈춘다.
        const uint64 derivedIndexSize = static_cast<uint64>( _header._fileCount ) * sizeof( PackFileEntryOnDisk );
        if ( _header._indexSize != derivedIndexSize )
        {
            SW_LOG_ERROR_ID( "PackReader.PackIndexSizeDisagrees", "Pack index size disagrees with file count in %# (header says %#, %# entries need %#)",
                             _packFilePath, static_cast<uint64>( _header._indexSize ), static_cast<uint32>( _header._fileCount ), derivedIndexSize );
            return false;
        }

        const bool bBlockSizeValid = _header._blockSize >= kPackMinBlockSize && _header._blockSize <= kPackMaxBlockSize && ( _header._blockSize & ( _header._blockSize - 1 ) ) == 0;
        if ( bBlockSizeValid == false )
        {
            SW_LOG_ERROR_ID( "PackReader.PackBlockSizeInvalid", "Pack block size %# is not a power of two within the format limits: %#", static_cast<uint32>( _header._blockSize ), _packFilePath );
            return false;
        }

        // 그리고 그 구역들이 헤더 앞(파일 안)에 있어야 한다. 헤더의 수를 그대로 믿고 `resize` 하면 잘린 팩 하나가 수십 기가짜리 할당 요청이 될 수 있다.
        // **뺄셈으로 측정한다.** `offset + size` 는 넘칠 수 있다.
        const uint64 headerOffset = fileSize - sizeof( PackHeader ); // `open` 이 파일이 헤더보다 큰 것을 확인했다
        const auto   isOutside    = [headerOffset]( uint64 offset, uint64 size )
        { return offset > headerOffset || size > headerOffset - offset; };
        if ( isOutside( _header._indexOffset, derivedIndexSize ) )
        {
            SW_LOG_ERROR_ID( "PackReader.PackIndexTableLies", "Pack index table lies outside the file %# (offset %#, size %#, file %#)",
                             _packFilePath, static_cast<uint64>( _header._indexOffset ), derivedIndexSize, fileSize );
            return false;
        }
        const uint64 blockTableSize = static_cast<uint64>( _header._blockCount ) * sizeof( PackBlockEntryOnDisk );
        if ( isOutside( _header._blockTableOffset, blockTableSize ) )
        {
            SW_LOG_ERROR_ID( "PackReader.PackBlockTableLies", "Pack block table lies outside the file %# (offset %#, size %#, file %#)",
                             _packFilePath, static_cast<uint64>( _header._blockTableOffset ), blockTableSize, fileSize );
            return false;
        }
        if ( isOutside( _header._stringPoolOffset, _header._stringPoolSize ) )
        {
            SW_LOG_ERROR_ID( "PackReader.PackStringPoolLies", "Pack string pool lies outside the file %# (offset %#, size %#, file %#)",
                             _packFilePath, static_cast<uint64>( _header._stringPoolOffset ), static_cast<uint64>( _header._stringPoolSize ), fileSize );
            return false;
        }
        // 색인 구역은 FAT · 블록 표 · (블록 해시 표) · 스트링 풀 · 헤더가 빈틈 없이 잇는다 — 서명이 거는 구간이 정확히 이 구조들이고, 한 번에 읽는 크기가 색인에 묶인다.
        const bool   bHasBlockHash  = ( _header._flags & static_cast<uint16>( PackFlag::HasBlockHash ) ) != 0;
        const uint64 blockTableEnd  = _header._indexOffset + derivedIndexSize + blockTableSize;
        const uint64 blockHashSize  = bHasBlockHash ? static_cast<uint64>( _header._blockCount ) * sizeof( PackDigest ) : 0;
        const bool   bBlockHashFits = bHasBlockHash ? _header._blockHashOffset == blockTableEnd : _header._blockHashOffset == 0;
        const bool   bContiguous    = _header._blockTableOffset == _header._indexOffset + derivedIndexSize && bBlockHashFits &&
                                 _header._stringPoolOffset == blockTableEnd + blockHashSize && _header._stringPoolOffset + _header._stringPoolSize == headerOffset;
        if ( bContiguous == false )
        {
            SW_LOG_ERROR_ID( "PackReader.IndexNotContiguous", "Pack index sections do not follow each other up to the header: %#", _packFilePath );
            return false;
        }
        return true;
    }

    bool PackReader::validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 payloadEnd, uint32 expectedFirstBlock ) const
    {
        // 페이로드가 페이로드 구역(FAT 앞) 안에 있어야 한다. **뺄셈으로 측정한다.** `offset + size` 는 넘칠 수 있다.
        // 저장 방식은 디스크 값이다 — 모르는 값을 어느 방식으로도 읽지 않는다.
        if ( diskEntry.getStorageValue() >= kPackEntryStorageCount )
        {
            SW_LOG_ERROR_ID( "PackReader.UnknownEntryStorage", "Pack entry has an unknown storage method %#: %#", static_cast<uint32>( diskEntry.getStorageValue() ), _packFilePath );
            return false;
        }
        const uint64 dataOffset = diskEntry.getDataOffset();
        if ( dataOffset > payloadEnd || diskEntry._compressedSize > payloadEnd - dataOffset )
        {
            SW_LOG_ERROR_ID( "PackReader.PackEntryPayloadLies", "Pack entry payload lies outside the payload region %# (offset %#, size %#, region end %#)",
                             _packFilePath, dataOffset, diskEntry._compressedSize, payloadEnd );
            return false;
        }

        // 원본 크기는 파일 크기로 묶이지 않는다(그것이 압축의 요점이다). 상한은 `CompressionStream` 이 같은 이유로 이미 정해 둔 것을 쓴다.
        // "이 컨테이너가 다루는 가장 큰 조각" 의 답이 두 개일 이유가 없다.
        if ( static_cast<uint64>( diskEntry._uncompressedSize ) > CompressionStream::kMaxUncompressedSize )
        {
            SW_LOG_ERROR_ID( "PackReader.PackEntryClaimsUncompressed", "Pack entry claims an uncompressed size of %# bytes in %# — beyond what this container carries.",
                             diskEntry._uncompressedSize, _packFilePath );
            return false;
        }

        // 블록 줄은 앞 항목의 줄에 바로 이어지고, 누적 끝은 0 이 아닌 크기로 오르며 마지막이 항목의 저장 크기다(블록 i 의 자리를 그대로 믿는다).
        const uint64 blockCount = ( static_cast<uint64>( diskEntry._uncompressedSize ) + _header._blockSize - 1 ) / _header._blockSize;
        bool         bBlocksFit = diskEntry._firstBlock == expectedFirstBlock && expectedFirstBlock <= _listBlock.size() && blockCount <= _listBlock.size() - expectedFirstBlock;
        uint32       storedEnd  = 0;
        for ( uint64 index = 0; bBlocksFit && index < blockCount; ++index )
        {
            const uint32 blockEnd = _listBlock[static_cast<size_t>( expectedFirstBlock + index )]._compressedEnd;
            bBlocksFit            = blockEnd > storedEnd;
            storedEnd             = blockEnd;
        }
        if ( bBlocksFit == false || storedEnd != diskEntry._compressedSize )
        {
            SW_LOG_ERROR_ID( "PackReader.PackEntryBlocksDisagree", "Pack entry blocks do not match the entry (first block %#, stored size %#): %#", static_cast<uint32>( diskEntry._firstBlock ),
                             static_cast<uint32>( diskEntry._compressedSize ), _packFilePath );
            return false;
        }
        return true;
    }
} // namespace sw
