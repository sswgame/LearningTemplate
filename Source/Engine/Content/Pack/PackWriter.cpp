#include "pch.h"

#include "Engine/Content/Pack/PackWriter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Content/ContentHash.h"
#include "Engine/Content/Pack/PackCompressionUtil.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Foundation/EngineParallel.h"

namespace sw
{
    namespace
    {
        struct PackWriterInternal
        {
            /**
             * @brief 창에 모인 원본이 이만큼 안 되면 압축 · CRC 를 나누지 않습니다(바이트). 디스패치 최소 비용(잠든 풀 ~34 us, 깨어 있으면 ~7 us)을 넘는 일만 병렬이 이긴다.
             *        가장 빠른 길(압축 없음, CRC 만)도 이 크기면 수십 us 라 손익분기 근처다.
             */
            static constexpr uint64 kMinParallelBytes = 64 * constant::memory::kBytesPerKilobyte;
            /** @brief 블록이 이 수 미만이면 나누지 않습니다. */
            static constexpr uint32 kMinParallelBlockCount = 2;
            /** @brief 슬롯 배치의 빈 자리를 걷어 낼 때(`compactCommitted`) 한 번에 옮기는 바이트입니다. */
            static constexpr uint64 kCompactChunkBytes = 4 * constant::memory::kBytesPerMegabyte;

            /** @brief 오프셋을 섹터 경계로 올립니다. */
            static uint64 alignToPackSector( uint64 offset ) { return MathUtil::align( offset, static_cast<uint64>( kPackSectorAlignment ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 블록 하나의 압축 결과 칸입니다. 병렬 구간에서 블록마다 자기 칸에만 쓰고, 읽는 일은 직렬 쓰기 단계가 합니다. */
    struct PackStreamWriter::BlockSlot
    {
        const uint8*       _pSource{ nullptr }; ///< 창에 든 원본 안 이 블록의 시작
        uint32             _rawSize{ 0 };
        uint32             _crc32{ 0 };
        ICompressionCodec* _pCodec{ nullptr }; ///< 이 블록의 코덱. 팩 코덱이 `None` 이거나 항목이 `Stored` 면 nullptr 이고 원본을 그대로 쓴다
        vector<uint8>      _listPayload;       ///< 코덱이 낸 바이트. 코덱이 없으면 비어 있다. 코덱이 있는데 비면 압축이 실패한 것이다(빈 블록은 없다)
        int64              _nanos{ 0 };

        /** @brief 파일에 쓸 바이트입니다. */
        const uint8* getStoredData() const { return _pCodec != nullptr ? _listPayload.data() : _pSource; }
        /** @brief 파일에 쓸 크기입니다. */
        size_t getStoredSize() const { return _pCodec != nullptr ? _listPayload.size() : _rawSize; }
    };

    /**
     * @brief 블록들의 압축 · CRC 를 돕니다. 블록 `i` 는 `_pSlot[i]` 에만 씁니다.
     * @details 코덱은 `open` 에서 한 번 찾는다 — 병렬 구간은 레지스트리를 만지지 않는다. 코덱의 `compress` 는 호출마다 지역 상태를 쓰는 구현이라
     *          한 인스턴스를 여러 스레드가 불러도 된다(`ICompressionCodec` 머리말).
     */
    struct PackStreamWriter::CompressJob
    {
        BlockSlot* _pSlot{ nullptr };
        int32      _compressionLevel{ 0 };

        /** @brief 블록 [start, end) 를 돕니다. */
        void run( uint32 start, uint32 end )
        {
            for ( uint32 index = start; index < end; ++index )
            {
                compressOne( _pSlot[index] );
            }
        }

        void compressOne( BlockSlot& inoutSlot ) const
        {
            const Stopwatch stopwatch;
            inoutSlot._crc32          = HashUtil::computeCRC32( inoutSlot._pSource, inoutSlot._rawSize );
            ICompressionCodec* pCodec = inoutSlot._pCodec;
            if ( pCodec != nullptr )
            {
                const size_t bound = pCodec->compressBound( inoutSlot._rawSize );
                size_t       compressedSize{ 0 };
                inoutSlot._listPayload.resize( bound );
                const bool bCompressed = bound != 0 && pCodec->compress( inoutSlot._pSource, inoutSlot._rawSize, inoutSlot._listPayload.data(), bound, compressedSize, _compressionLevel );
                inoutSlot._listPayload.resize( bCompressed ? compressedSize : 0 );
            }
            inoutSlot._nanos = stopwatch.getElapsedNanoseconds();
        }
    };
} // namespace sw

namespace sw
{
    PackStreamWriter::PackStreamWriter()
        : _desc{}
        , _file{}
        , _packPath{}
        , _pCodec{ nullptr }
        , _listPending{}
        , _pendingBytes{ 0 }
        , _listDiskEntry{}
        , _listBlock{}
        , _listBlockDigest{}
        , _stringPoolBytes{}
        , _mapPreviousSlot{}
        , _payloadEnd{ 0 }
        , _usedPayloadBytes{ 0 }
        , _contentHash{ ContentHash::kInitialState }
        , _lastPathHash{ 0 }
        , _entryCount{ 0 }
        , _stats{}
        , _openWatch{}
        , _state{ State::Closed }
    {
    }

    PackStreamWriter::~PackStreamWriter() = default;

    uint64 PackStreamWriter::makePathHash( string_view relativePath )
    {
        return HashUtil::computeStringHash64( FileUtil::normalizeSeparators( relativePath ) );
    }

    unexpected<Error> PackStreamWriter::fail( Error error )
    {
        _state = State::Failed;
        _listPending.clear();
        return unexpected( std::move( error ) );
    }

    expected<void> PackStreamWriter::open( string_view packPath, const PackWriterDesc& desc )
    {
        if ( _state != State::Closed || _packPath.empty() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack stream writer writes one pack - make a new writer for the next", packPath } );
        const bool bBlockSizeValid = desc._blockSize >= kPackMinBlockSize && desc._blockSize <= kPackMaxBlockSize && ( desc._blockSize & ( desc._blockSize - 1 ) ) == 0;
        if ( bBlockSizeValid == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack block size must be a power of two within the format limits (PackConfig _blockSize)", packPath } );
        if ( desc._writeWindowBytes == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack write window must not be zero (PackConfig _writeWindowBytes)", packPath } );
        ICompressionCodec* pCodec = nullptr;
        if ( desc._compression != PackCompressionKind::None )
        {
            pCodec = PackCompressionUtil::findCodec( desc._compression );
            if ( pCodec == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "pack compression type has no registered codec", packPath } );
        }

        _desc     = desc;
        _packPath = string( packPath );
        _pCodec   = pCodec;
        _openWatch.restart();
        _state = State::Open;
        if ( FileUtil::ensureParentDirectoryExists( packPath ) == false )
            return fail( Error{ ErrorKind::ExternalFailure, "pack parent folder cannot be made", packPath } );
        // 앞 빌드 팩은 바꿔 끼우기 전까지 그대로라(임시 파일에 쓴다) 열기 뒤에 읽어도 된다.
        const expected<void> opened = _file.open( packPath );
        if ( opened.has_value() == false )
            return fail( opened.error() );
        // 풀의 맨 앞은 마운트 도메인이다(`kPackFormatVersion` 2) - 항목이 없어도 담아 풀은 늘 있다.
        _stringPoolBytes.assign( _desc._mountDomain.begin(), _desc._mountDomain.end() );
        _stringPoolBytes.push_back( 0 );
        const expected<void> loaded = loadPreviousSlots();
        if ( loaded.has_value() == false )
            return fail( loaded.error() );
        return {};
    }

    expected<void> PackStreamWriter::loadPreviousSlots()
    {
        const bool bNoPrevious = _desc._layoutMode != PackLayoutMode::Slot || _desc._previousPackPath.empty() || FileUtil::exists( _desc._previousPackPath ) == false;
        if ( bNoPrevious )
            return {};
        struct Placed
        {
            uint64 _hash{ 0 };
            uint64 _offset{ 0 };
            uint64 _size{ 0 };
        };
        vector<Placed> listPlaced;
        {
            PackReader reader; // 이 블록을 나가며 닫는다 — 바꿔 끼울 때 앞 팩을 쥐고 있지 않게
            if ( reader.open( _desc._previousPackPath ) == false )
            {
                SW_LOG_WARNING_ID( "PackWriter.PackLayoutPreviousPack", "Pack layout: previous pack '%#' could not be opened - the slot layout starts without it", _desc._previousPackPath );
                return {};
            }
            vector<string> listPath;
            reader.collectAllEntryPaths( listPath );
            listPlaced.reserve( listPath.size() );
            for ( const string& path : listPath )
            {
                PackFileEntry entry;
                if ( reader.getFileEntry( path, entry ) && entry._compressedSize != 0 )                    // 빈 항목은 자리를 차지하지 않는다
                    listPlaced.push_back( { entry._pathHash, entry._dataOffset, entry._compressedSize } ); // 조회 결과(`PackFileEntry`)의 오프셋이다
            }
        }
        VectorUtil::sort( listPlaced, []( const Placed& left, const Placed& right )
        { return left._offset < right._offset; } );
        uint64 payloadEnd = 0;
        uint64 usedBytes  = 0;
        for ( size_t index = 0; index < listPlaced.size(); ++index )
        {
            const Placed& placed    = listPlaced[index];
            const uint64  usedEnd   = PackWriterInternal::alignToPackSector( placed._offset + placed._size );
            const uint64  nextBegin = index + 1 < listPlaced.size() ? listPlaced[index + 1]._offset : usedEnd;
            if ( nextBegin < placed._offset + placed._size )
            {
                SW_LOG_WARNING_ID( "PackWriter.PackLayoutPreviousPack3", "Pack layout: previous pack '%#' has overlapping payloads - the slot layout starts without it", _desc._previousPackPath );
                _mapPreviousSlot.clear();
                return {};
            }
            _mapPreviousSlot[placed._hash] = { placed._offset, nextBegin - placed._offset };
            payloadEnd                     = payloadEnd > usedEnd ? payloadEnd : usedEnd;
            usedBytes += PackWriterInternal::alignToPackSector( placed._size );
        }
        if ( _mapPreviousSlot.empty() )
            return {};
        (void)usedBytes; // 빈 자리 판정은 이번 빌드가 다 쓴 뒤에 한다(`close`)
        // 앞 자리를 덮어쓸 구역을 0 으로 늘린다(성긴 파일의 구멍 — 디스크를 쓰지 않는다). 이 뒤의 쓰기는 앞 자리면 덮고 아니면 끝에 붙는다.
        // shortcut: 앞 자리를 덮어쓰므로 내용 해시는 닫은 뒤 파일을 조각으로 다시 읽어 센다 — 내용 해시(FNV-1a)는 바이트를 자리 순으로 섞어 덮어쓴 블록의 해시를 이을 수 없다.
        //           슬롯 배치를 배포 쿠킹에 켜면 쿠킹 기록의 팩 출력 해시를 블록 해시 목록(언리얼 IoStore 청크 해시 자리)으로 바꾼다.
        const expected<void> extended = _file.appendZeros( payloadEnd );
        if ( extended.has_value() == false )
            return extended;
        _payloadEnd = payloadEnd;
        return {};
    }

    expected<void> PackStreamWriter::append( string_view relativePath, PackEntryStorage storage, vector<uint8> bytes )
    {
        if ( _state != State::Open )
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack stream writer is not open (or failed earlier)", relativePath } );
        PendingEntry entry;
        entry._storage      = storage;
        entry._relativePath = FileUtil::normalizeSeparators( relativePath );
        entry._pathHash     = HashUtil::computeStringHash64( entry._relativePath );
        entry._bytes        = std::move( bytes );
        // FAT 은 경로 해시 오름차순이고 리더는 해시 하나에 항목 하나만 든다 — 같은 해시도 오류다(어느 쪽이 이길지 조용히 정하지 않는다).
        if ( _entryCount > 0 && entry._pathHash == _lastPathHash )
            return fail( Error{ ErrorKind::AlreadyExists, "pack entry is added twice (same path hash as the previous entry)", entry._relativePath } );
        if ( _entryCount > 0 && entry._pathHash < _lastPathHash )
            return fail( Error{ ErrorKind::InvalidArgument, "pack entries must be appended in ascending path hash order", entry._relativePath } );
        // 읽는 쪽(PackReader)이 받는 항목 크기 한도와 같은 상수를 본다(감사 A-3).
        if ( entry._bytes.size() > CompressionStream::kMaxUncompressedSize )
            return fail( Error{ ErrorKind::InvalidArgument, "pack entry is over the entry size limit the pack reader accepts", entry._relativePath } );
        if ( _stringPoolBytes.size() + entry._relativePath.size() + 1 > MathUtil::kMaxUInt32 )
            return fail( Error{ ErrorKind::InvalidArgument, "pack string pool does not fit the 32-bit entry fields", entry._relativePath } );

        _lastPathHash = entry._pathHash;
        ++_entryCount;
        _stats._inputBytes += entry._bytes.size();
        _pendingBytes += entry._bytes.size();
        _listPending.push_back( std::move( entry ) );
        if ( _pendingBytes < _desc._writeWindowBytes )
            return {};
        const expected<void> flushed = flushPending();
        if ( flushed.has_value() == false )
            return fail( flushed.error() );
        return {};
    }

    expected<void> PackStreamWriter::flushPending()
    {
        // 1. 블록 칸을 만든다 — 항목마다 `_blockSize` 로 자른다. 빈 항목은 블록이 없다.
        vector<BlockSlot> listSlot;
        for ( const PendingEntry& entry : _listPending )
        {
            for ( size_t offset = 0; offset < entry._bytes.size(); offset += _desc._blockSize )
            {
                BlockSlot slot;
                slot._pSource = entry._bytes.data() + offset;
                slot._rawSize = static_cast<uint32>( entry._bytes.size() - offset < _desc._blockSize ? entry._bytes.size() - offset : _desc._blockSize );
                slot._pCodec  = entry._storage == PackEntryStorage::Stored ? nullptr : _pCodec;
                listSlot.push_back( std::move( slot ) );
            }
        }
        if ( _listBlock.size() + listSlot.size() > MathUtil::kMaxUInt32 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack block count does not fit the 32-bit entry fields", _packPath } );

        // 2. 압축 · CRC 는 블록마다 독립이라 병렬이다. 작은 창은 나누지 않는다 — 서비스가 없거나 워커가 없으면 `runParallel` 이 이 스레드에서 차례로 돈다.
        CompressJob job;
        job._pSlot                = listSlot.data();
        job._compressionLevel     = _desc._compressionLevel;
        const uint32    slotCount = static_cast<uint32>( listSlot.size() );
        const uint32    threshold = _pendingBytes >= PackWriterInternal::kMinParallelBytes ? PackWriterInternal::kMinParallelBlockCount : slotCount + 1;
        const Stopwatch compressWatch;
        engine::runParallel( slotCount, threshold, SW_DELEGATE_METHOD( ParallelBlockDelegate, &CompressJob::run, &job ) );
        _stats._compressWallNanos += compressWatch.getElapsedNanoseconds();

        // 3. 받은 순서(경로 해시 순)로 쓴다. 블록 칸은 항목 순서 그대로 이어져 있다.
        size_t slotIndex = 0;
        for ( const PendingEntry& entry : _listPending )
        {
            const size_t blockCount = ( entry._bytes.size() + _desc._blockSize - 1 ) / _desc._blockSize;
            uint64       storedSize = 0;
            for ( size_t index = slotIndex; index < slotIndex + blockCount; ++index )
            {
                const BlockSlot& slot = listSlot[index];
                _stats._compressCPUNanos += slot._nanos;
                if ( slot._pCodec != nullptr && slot._listPayload.empty() )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "pack codec failed to compress a block", entry._relativePath } );
                storedSize += slot.getStoredSize();
            }
            if ( storedSize > MathUtil::kMaxUInt32 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "compressed pack entry does not fit the 32-bit entry fields", entry._relativePath } );

            // 슬롯 배치는 앞 빌드의 자리에 들어가면 그 자리를 덮고, 아니면 끝(섹터 정렬)에 붙인다.
            uint64     dataOffset = _payloadEnd;
            const auto slotIt     = _mapPreviousSlot.find( entry._pathHash );
            const bool bKeepSlot  = storedSize > 0 && slotIt != _mapPreviousSlot.end() && storedSize <= slotIt->second._capacity;
            if ( bKeepSlot )
                dataOffset = slotIt->second._offset;
            else if ( storedSize > 0 )
            {
                const expected<void> padded = padToSector();
                if ( padded.has_value() == false )
                    return padded;
                dataOffset = _file.getSize();
            }

            if ( dataOffset > PackFileEntryOnDisk::kMaxDataOffset )
                return unexpected( Error{ ErrorKind::InvalidArgument, "pack payload offset does not fit the 56-bit placement field", entry._relativePath } );
            PackFileEntryOnDisk diskEntry{};
            diskEntry._pathHash = entry._pathHash;
            diskEntry.setPlacement( dataOffset, entry._storage );
            diskEntry._compressedSize   = static_cast<uint32>( storedSize );
            diskEntry._uncompressedSize = static_cast<uint32>( entry._bytes.size() );
            diskEntry._firstBlock       = static_cast<uint32>( _listBlock.size() );
            diskEntry._stringPoolOffset = static_cast<uint32>( _stringPoolBytes.size() );
            uint32 compressedEnd        = 0;
            for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex, ++slotIndex )
            {
                const BlockSlot&     slot    = listSlot[slotIndex];
                const uint8*         pStored = slot.getStoredData();
                const size_t         size    = slot.getStoredSize();
                const expected<void> written = writeBytes( dataOffset + compressedEnd, pStored, size );
                if ( written.has_value() == false )
                    return written;
                compressedEnd += static_cast<uint32>( size );
                _listBlock.push_back( PackBlockEntryOnDisk{ compressedEnd, slot._crc32 } );
                if ( _desc._pDigestProvider != nullptr )
                {
                    // 블록 요약은 저장 바이트에 건다 — 읽는 쪽이 풀기 전에 견준다(바뀐 바이트가 코덱에 닿지 않는다). 블록은 256 KiB 이하라 32 비트 크기에 든다.
                    PackDigest blockDigest;
                    if ( _desc._pDigestProvider->computeSHA256( pStored, static_cast<int32>( size ), blockDigest._arrByte ) == false )
                        return unexpected( Error{ ErrorKind::ExternalFailure, "pack block digest could not be computed", entry._relativePath } );
                    _listBlockDigest.push_back( blockDigest );
                }
            }
            if ( bKeepSlot == false && storedSize > 0 )
                _payloadEnd = _file.getSize();
            _usedPayloadBytes += PackWriterInternal::alignToPackSector( storedSize );
            _listDiskEntry.push_back( diskEntry );
            // 항목 경로 목록은 늘 담는다 — 런타임 폴더 나열이 이것으로 항목 이름을 알고, 리더는 없는 팩을 거절한다.
            _stringPoolBytes.insert( _stringPoolBytes.end(), entry._relativePath.begin(), entry._relativePath.end() );
            _stringPoolBytes.push_back( 0 );
        }
        _listPending.clear();
        _pendingBytes = 0;
        return {};
    }

    expected<void> PackStreamWriter::writeBytes( uint64 offset, const uint8* pData, size_t size )
    {
        const Stopwatch writeWatch;
        expected<void>  written{};
        if ( offset == _file.getSize() )
        {
            written = _file.append( pData, size );
            if ( _mapPreviousSlot.empty() )
                _contentHash = ContentHash::appendBytes( _contentHash, pData, size );
        }
        else
            written = _file.writeAt( offset, pData, size );
        _stats._writeNanos += writeWatch.getElapsedNanoseconds();
        return written;
    }

    expected<void> PackStreamWriter::padToSector()
    {
        static const uint8 arrZero[kPackSectorAlignment]{};
        const uint64       fileSize = _file.getSize();
        const uint64       padding  = PackWriterInternal::alignToPackSector( fileSize ) - fileSize;
        return padding == 0 ? expected<void>{} : writeBytes( fileSize, arrZero, static_cast<size_t>( padding ) );
    }

    expected<PackWriteResult> PackStreamWriter::close()
    {
        if ( _state != State::Open )
        {
            _state = State::Closed;
            return unexpected( Error{ ErrorKind::InvalidArgument, "pack stream writer is not open (or failed earlier)", _packPath } );
        }
        const expected<void> flushed = flushPending();
        if ( flushed.has_value() == false )
            return fail( flushed.error() );

        // 슬롯 배치의 빈 자리는 이번 빌드가 다 쓴 뒤에 판정한다 — 앞 빌드의 빈 자리와 이번에 지운 · 옮긴 항목의 자리를 함께 센다.
        const bool   bSlotLayout = _mapPreviousSlot.empty() == false;
        const uint64 regionBytes = _payloadEnd;
        const uint64 holeBytes   = regionBytes > _usedPayloadBytes ? regionBytes - _usedPayloadBytes : 0;
        const bool   bCompact    = bSlotLayout && holeBytes * 100 > regionBytes * _desc._maxHolePercent;

        expected<PackWriteResult> written = writeIndexAndCommit();
        if ( written.has_value() && bCompact )
        {
            SW_LOG_INFO_ID( "PackWriter.SlotLayoutCompacted", "Pack layout: %# has %# hole bytes in %# payload bytes (over %# percent) - payloads are packed densely again", _packPath,
                            holeBytes, regionBytes, _desc._maxHolePercent );
            written = compactCommitted();
        }
        else if ( written.has_value() && bSlotLayout )
        {
            const expected<uint64> fileHash = ContentHash::computeFileStream( _packPath );
            if ( fileHash.has_value() == false )
                return fail( fileHash.error() );
            written->_contentHash = *fileHash;
        }
        if ( written.has_value() == false )
            return fail( written.error() );
        _state              = State::Closed;
        _stats._entryCount  = _entryCount;
        const int64 total   = _openWatch.getElapsedNanoseconds();
        const int64 counted = _stats._compressWallNanos + _stats._writeNanos;
        _stats._layoutNanos = total > counted ? total - counted : 0;
        written->_stats     = _stats;
        return written;
    }

    expected<PackWriteResult> PackStreamWriter::writeIndexAndCommit()
    {
        const uint64         payloadEnd = _payloadEnd;
        const expected<void> padded     = padToSector();
        if ( padded.has_value() == false )
            return unexpected( padded.error() );

        // 색인 구역은 FAT · 블록 표 · 블록 해시 표 · 스트링 풀 · 헤더 순으로 빈틈 없이 잇는다 — `_indexOffset` 부터 파일 끝까지 한 구간이다(색인 서명의 자리).
        PackHeader header{};
        header._magic              = kPackMagic;
        header._formatVersion      = kPackFormatVersion;
        header._dlcAppID           = _desc._dlcAppID;
        header._compressionType    = static_cast<uint8>( _desc._compression );
        header._encryptionType     = static_cast<uint8>( PackEncryptionKind::None );
        header._sectorAlignment    = kPackSectorAlignment;
        const bool bBlockHash      = _desc._pDigestProvider != nullptr;
        header._flags              = static_cast<uint16>( static_cast<uint16>( PackFlag::HasCRC32 ) | static_cast<uint16>( PackFlag::HasStringPool ) |
                                                          ( bBlockHash ? static_cast<uint16>( PackFlag::HasBlockHash ) : uint16{ 0 } ) );
        header._fileCount          = static_cast<uint32>( _listDiskEntry.size() );
        header._indexOffset        = _file.getSize();
        header._indexSize          = static_cast<uint64>( _listDiskEntry.size() ) * sizeof( PackFileEntryOnDisk );
        header._blockTableOffset   = header._indexOffset + header._indexSize;
        header._blockCount         = static_cast<uint32>( _listBlock.size() );
        header._blockSize          = _desc._blockSize;
        const uint64 blockTableEnd = header._blockTableOffset + static_cast<uint64>( _listBlock.size() ) * sizeof( PackBlockEntryOnDisk );
        header._blockHashOffset    = bBlockHash ? blockTableEnd : 0;
        header._stringPoolOffset   = blockTableEnd + static_cast<uint64>( _listBlockDigest.size() ) * sizeof( PackDigest );
        header._stringPoolSize     = _stringPoolBytes.size();
        header._totalDataSize      = payloadEnd;
        // 색인 구역을 한 벡터로 이어 한 번에 쓴다 — 서명 채널은 이 바이트의 SHA-256 이 색인 요약이다(다시 읽지 않는다).
        vector<uint8> listIndexByte;
        const auto    appendIndex = [&listIndexByte]( const void* pData, size_t size )
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            listIndexByte.insert( listIndexByte.end(), pByte, pByte + size );
        };
        appendIndex( _listDiskEntry.data(), static_cast<size_t>( header._indexSize ) );
        appendIndex( _listBlock.data(), _listBlock.size() * sizeof( PackBlockEntryOnDisk ) );
        appendIndex( _listBlockDigest.data(), _listBlockDigest.size() * sizeof( PackDigest ) );
        appendIndex( _stringPoolBytes.data(), _stringPoolBytes.size() );
        appendIndex( &header, sizeof( PackHeader ) );
        if ( writeBytes( _file.getSize(), listIndexByte.data(), listIndexByte.size() ).has_value() == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "pack index could not be written (disk full?)", _packPath } );

        PackWriteResult result;
        if ( bBlockHash )
        {
            PackDigest indexDigest;
            if ( listIndexByte.size() > static_cast<size_t>( MathUtil::kMaxInt32 ) ||
                 _desc._pDigestProvider->computeSHA256( listIndexByte.data(), static_cast<int32>( listIndexByte.size() ), indexDigest._arrByte ) == false )
                return unexpected( Error{ ErrorKind::ExternalFailure, "pack index digest could not be computed", _packPath } );
            result._indexDigest = indexDigest;
        }
        result._fileSize    = _file.getSize();
        result._indexOffset = header._indexOffset;
        result._contentHash = _contentHash; // 덧붙이기만 했을 때의 값 — 앞 자리를 덮어쓴 슬롯 배치는 `close` 가 다시 센다
        const Stopwatch      commitWatch;
        const expected<void> committed = _file.commit();
        _stats._writeNanos += commitWatch.getElapsedNanoseconds();
        if ( committed.has_value() == false )
            return unexpected( committed.error() );
        return result;
    }

    expected<PackWriteResult> PackStreamWriter::compactCommitted()
    {
        // 바꿔 끼운 슬롯 배치 팩에서 페이로드를 FAT 순서로 촘촘히 옮겨 같은 자리에 다시 쓴다. 블록 표 · 요약은 항목 기준이라 그대로이고 오프셋만 바뀐다.
        // 원본은 조각으로 읽고(한 벡터로 들지 않는다) 새 파일은 임시 파일에 써서 바꿔 끼운다. 모두 덧붙이기라 내용 해시를 쓰면서 센다.
        _mapPreviousSlot.clear();
        _contentHash                = ContentHash::kInitialState;
        const expected<void> opened = _file.open( _packPath );
        if ( opened.has_value() == false )
            return unexpected( opened.error() );
        vector<uint8> chunk;
        for ( PackFileEntryOnDisk& diskEntry : _listDiskEntry )
        {
            const PackEntryStorage storage    = static_cast<PackEntryStorage>( diskEntry.getStorageValue() );
            const uint64           oldOffset  = diskEntry.getDataOffset();
            const uint64           storedSize = diskEntry._compressedSize;
            if ( storedSize > 0 )
            {
                const expected<void> padded = padToSector();
                if ( padded.has_value() == false )
                    return unexpected( padded.error() );
            }
            const uint64 newOffset = _file.getSize();
            for ( uint64 copied = 0; copied < storedSize; copied += chunk.size() )
            {
                const uint64 size = storedSize - copied < PackWriterInternal::kCompactChunkBytes ? storedSize - copied : PackWriterInternal::kCompactChunkBytes;
                if ( FileUtil::readFile( _packPath, chunk, oldOffset + copied, size ) == false || chunk.size() != size )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "slot layout pack cannot be read back for compaction", _packPath } );
                const expected<void> appended = writeBytes( _file.getSize(), chunk.data(), chunk.size() );
                if ( appended.has_value() == false )
                    return unexpected( appended.error() );
            }
            diskEntry.setPlacement( newOffset, storage );
        }
        _payloadEnd = _file.getSize();
        return writeIndexAndCommit();
    }
} // namespace sw
