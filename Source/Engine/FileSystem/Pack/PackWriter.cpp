#include "pch.h"

#include "Engine/FileSystem/Pack/PackWriter.h"

#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/FileSystem/Pack/PackCompressionUtil.h"
#include "Engine/FileSystem/Pack/PackReader.h"
#include "Engine/Foundation/EngineParallel.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "PackWriter" );

    namespace
    {
        struct PackWriterInternal
        {
            /**
             * @brief 담은 원본이 이만큼 안 되면 압축 · CRC 를 나누지 않습니다(바이트). 디스패치 최소 비용(잠든 풀 ~34 us, 깨어 있으면 ~7 us)을 넘는 일만 병렬이 이긴다.
             *        가장 빠른 길(압축 없음, CRC 만)도 이 크기면 수십 us 라 손익분기 근처다.
             */
            static constexpr uint64 kMinParallelBytes = 64 * constant::kBytesPerKilobyte;
            /** @brief 항목이 이 수 미만이면 나누지 않습니다. 항목 하나는 쪼갤 수 없어 하나뿐이면 직렬과 같다. */
            static constexpr uint32 kMinParallelEntryCount = 2;

            /** @brief 오프셋을 섹터 경계로 올립니다. */
            static uint64 alignToPackSector( uint64 offset )
            {
                return MathUtil::align( offset, static_cast<uint64>( kPackSectorAlignment ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 항목 하나의 압축 결과 칸입니다. 병렬 구간에서 항목마다 자기 칸에만 쓰고, 읽는 일은 직렬 배치 단계가 합니다. */
    struct PackWriter::CompressSlot
    {
        vector<uint8> _listPayload;          ///< 코덱이 낸 바이트. 압축을 거치지 않은 항목(`None` · 빈 항목)은 비어 있고 원본을 그대로 담는다
        int64         _nanos{ 0 };           ///< 이 항목의 압축 · CRC 에 쓴 시간
        uint32        _crc32{ 0 };           ///< 원본의 CRC32
        bool          _bCompressed{ false }; ///< `_listPayload` 가 압축 결과다
        bool          _bFailed{ false };     ///< 코덱이 실패했다(이유는 오류 로그로 이미 남았다)
    };

    /**
     * @brief 항목들의 압축 · CRC 를 돕니다. 병렬 본문은 컨테이너가 아니라 포인터만 만지고, 항목 `i` 는 `_pSlot[i]` 에만 씁니다.
     * @details 코덱은 직렬 구간에서 한 번 찾아 넘긴다 — 병렬 구간은 레지스트리를 만지지 않는다. 코덱의 `compress` 는 호출마다 지역 상태를 쓰는 구현(zlib `z_stream` · zstd
     *          컨텍스트 · LZ4 상태 블록을 호출마다 만든다)이라 한 인스턴스를 여러 스레드가 불러도 된다.
     */
    struct PackWriter::CompressJob
    {
        const Entry* const* _ppEntry{ nullptr }; ///< 경로 해시 순으로 정렬된 항목
        const uint32*       _pOrder{ nullptr };  ///< 일을 나눠 주는 순서(원본이 큰 항목부터). 값은 `_ppEntry` 의 색인
        CompressSlot*       _pSlot{ nullptr };   ///< `_ppEntry` 와 같은 색인의 결과 칸
        ICompressionCodec*  _pCodec{ nullptr };  ///< nullptr 이면 압축하지 않는다(`PackCompressionType::None`)
        int32               _compressionLevel{ 0 };

        /** @brief 일 순서 [start, end) 를 돕니다. */
        void run( uint32 start, uint32 end )
        {
            for ( uint32 orderIndex = start; orderIndex < end; ++orderIndex )
            {
                const uint32 entryIndex = _pOrder[orderIndex];
                compressOne( *_ppEntry[entryIndex], _pSlot[entryIndex] );
            }
        }

        /** @brief 항목 하나의 CRC 를 구하고 팩 코덱으로 압축합니다. 빈 항목과 `None` 은 원본 그대로 둡니다. */
        void compressOne( const Entry& entry, CompressSlot& outListSlot ) const
        {
            // 32 비트 칸에 안 담기는 항목은 배치 단계가 이름과 함께 오류로 멈춘다 — 여기서 압축하느라 시간을 쓰지 않는다.
            if ( entry._bytes.size() > MathUtil::kMaxUInt32 )
                return;

            const Stopwatch stopwatch;
            outListSlot._crc32 = StringUtil::computeCRC32( entry._bytes.data(), entry._bytes.size() );
            if ( _pCodec != nullptr && entry._bytes.empty() == false )
            {
                outListSlot._bCompressed = true;
                outListSlot._bFailed     = compressPayload( entry, outListSlot._listPayload ) == false;
            }
            outListSlot._nanos = stopwatch.getElapsedNanoseconds();
        }

        /** @brief 항목 하나를 코덱으로 압축해 @p outPayloadBytes 에 담습니다. */
        [[nodiscard]] bool compressPayload( const Entry& entry, vector<uint8>& outPayloadBytes ) const
        {
            outPayloadBytes.clear();
            const size_t bound = _pCodec->compressBound( entry._bytes.size() );
            if ( bound == 0 )
            {
                SW_LOG_ERROR( "%# cannot compress '%#' (%# bytes is beyond the codec limit)", _pCodec->getCodecName(), entry._relativePath,
                              static_cast<uint64>( entry._bytes.size() ) );
                return false;
            }
            outPayloadBytes.resize( bound );
            size_t compressedSize{ 0 };
            if ( _pCodec->compress( entry._bytes.data(), entry._bytes.size(), outPayloadBytes.data(), bound, compressedSize, _compressionLevel ) == false )
            {
                SW_LOG_ERROR( "%# failed to compress '%#'", _pCodec->getCodecName(), entry._relativePath );
                outPayloadBytes.clear();
                return false;
            }
            outPayloadBytes.resize( compressedSize );
            return true;
        }
    };
} // namespace sw

namespace sw
{
    PackWriter::PackWriter( const PackWriterDesc& desc )
        : _listEntry{}
        , _desc{ desc }
    {
    }

    void PackWriter::addBytes( string_view relativePath, vector<uint8> bytes )
    {
        Entry entry;
        entry._relativePath = FileUtil::normalizeSeparators( relativePath );
        entry._pathHash     = HashUtil::computeStringHash64( entry._relativePath );
        entry._bytes        = std::move( bytes );
        _listEntry.push_back( std::move( entry ) );
    }

    bool PackWriter::sortEntriesByPathHash( vector<const Entry*>& outListSorted, uint64& outInputBytes ) const
    {
        // FAT 은 경로 해시 오름차순이다. 같은 해시가 둘이면 리더가 하나만 들므로 실패로 멈춘다.
        vector<const Entry*>& listSorted = outListSorted;
        listSorted.clear();
        listSorted.reserve( _listEntry.size() );
        outInputBytes = 0;
        for ( const Entry& entry : _listEntry )
        {
            listSorted.push_back( &entry );
            outInputBytes += entry._bytes.size();
        }
        VectorUtil::stableSort( listSorted, []( const Entry* pLeft, const Entry* pRight )
        { return pLeft->_pathHash < pRight->_pathHash; } );
        for ( size_t index = 1; index < listSorted.size(); ++index )
        {
            if ( listSorted[index]->_pathHash == listSorted[index - 1]->_pathHash )
            {
                SW_LOG_ERROR( "Pack entry '%#' is added twice (same path hash as '%#')", listSorted[index]->_relativePath, listSorted[index - 1]->_relativePath );
                return false;
            }
        }
        return true;
    }

    bool PackWriter::compressEntries( const vector<const Entry*>& listSorted, uint64 inputBytes, vector<CompressSlot>& outListSlot, int64& outWallNanos ) const
    {
        // 코덱은 직렬 구간에서 한 번 찾는다. 압축할 바이트가 하나도 없으면 코덱이 없어도 되는 팩이다(빈 항목은 코덱을 거치지 않는다).
        ICompressionCodec* pCodec = nullptr;
        if ( _desc._compression != PackCompressionType::None && inputBytes > 0 )
        {
            pCodec = PackCompressionUtil::findCodec( _desc._compression );
            if ( pCodec == nullptr )
            {
                SW_LOG_ERROR( "Pack compression type %# has no registered codec", static_cast<uint32>( _desc._compression ) );
                return false;
            }
        }

        // 압축 · CRC 는 항목마다 독립이다 — 병렬로 돌고 항목마다 자기 결과 칸에만 쓴다. 일은 큰 항목부터 나눠 주어 마지막에 큰 항목 하나가 남지 않게 한다.
        // 배치는 아래에서 정렬 순서(경로 해시)로 하므로 일을 나눠 주는 순서가 팩 바이트에 영향을 주지 않는다.
        const uint32   entryCount = static_cast<uint32>( listSorted.size() );
        vector<uint32> listOrder( entryCount );
        for ( uint32 index = 0; index < entryCount; ++index )
        {
            listOrder[index] = index;
        }
        VectorUtil::stableSort( listOrder, [&listSorted]( uint32 left, uint32 right )
        { return listSorted[left]->_bytes.size() > listSorted[right]->_bytes.size(); } );

        outListSlot.clear();
        outListSlot.resize( entryCount );
        CompressJob job;
        job._ppEntry          = listSorted.data();
        job._pOrder           = listOrder.data();
        job._pSlot            = outListSlot.data();
        job._pCodec           = pCodec;
        job._compressionLevel = _desc._compressionLevel;

        // 작은 팩은 나누지 않는다. 서비스가 없거나 워커가 없으면 `runParallel` 이 이 스레드에서 차례로 돈다.
        const bool      bWorthSplitting = inputBytes >= PackWriterInternal::kMinParallelBytes;
        const uint32    serialThreshold = bWorthSplitting ? PackWriterInternal::kMinParallelEntryCount : entryCount + 1;
        const Stopwatch compressWatch;
        engine::runParallel( entryCount, serialThreshold, SW_DELEGATE_METHOD( ParallelBlockDelegate, &CompressJob::run, &job ) );
        outWallNanos = compressWatch.getElapsedNanoseconds();
        return true;
    }

    /** @brief 직렬 배치 단계의 결과입니다. 조립 단계가 이 값으로 팩 바이트를 채웁니다. */
    struct PackWriter::PackLayout
    {
        vector<PackFileEntryOnDisk> _listDiskEntry;
        vector<uint8>               _stringPoolBytes;
        uint64                      _indexOffset{ 0 };
        uint64                      _indexSize{ 0 };
        uint64                      _dataStartOffset{ 0 };
        uint64                      _dataEndOffset{ 0 };
        uint64                      _stringPoolOffset{ 0 }; ///< 스트링 풀이 없으면 0
        uint64                      _fileSize{ 0 };
        int64                       _compressCPUNanos{ 0 }; ///< 항목마다 잰 압축 · CRC 시간의 합
    };

    /** @brief 앞 빌드 팩의 항목 하나가 차지한 자리를 항목 경로 해시로 찾는 표입니다. */
    struct PackWriter::PreviousSlotTable
    {
        struct Slot
        {
            uint64 _offset{ 0 };
            uint64 _capacity{ 0 }; ///< 이 자리에 담을 수 있는 최대 바이트(다음 페이로드 오프셋까지)
        };

        unordered_map<uint64, Slot> _mapHashToSlot;
        uint64                      _payloadEnd{ 0 }; ///< 앞 팩의 마지막 페이로드 끝(섹터 정렬). 새 항목은 이보다 뒤에 붙는다
    };

    bool PackWriter::loadPreviousSlots( PreviousSlotTable& outTable ) const
    {
        outTable               = PreviousSlotTable{};
        const bool bNoPrevious = _desc._layoutMode != PackLayoutMode::Slot || _desc._previousPackPath.empty() || FileUtil::exists( _desc._previousPackPath ) == false;
        if ( bNoPrevious )
            return true;
        PackReader reader;
        if ( reader.open( _desc._previousPackPath ) == false )
        {
            SW_LOG_WARNING( "Pack layout: previous pack '%#' could not be opened - the slot layout starts without it", _desc._previousPackPath );
            return true;
        }
        struct Placed
        {
            uint64 _hash{ 0 };
            uint64 _offset{ 0 };
            uint64 _size{ 0 };
        };
        vector<string> listPath;
        reader.collectAllEntryPaths( listPath );
        vector<Placed> listPlaced;
        listPlaced.reserve( listPath.size() );
        for ( const string& path : listPath )
        {
            PackFileEntry entry;
            if ( reader.getFileEntry( path, entry ) == false )
            {
                SW_LOG_WARNING( "Pack layout: previous pack '%#' lists '%#' without an index entry - the slot layout starts without it", _desc._previousPackPath, path );
                return true;
            }
            if ( entry._compressedSize != 0 ) // 빈 항목은 자리를 차지하지 않는다
                listPlaced.push_back( { entry._pathHash, entry._dataOffset, entry._compressedSize } );
        }
        VectorUtil::sort( listPlaced, []( const Placed& left, const Placed& right )
        { return left._offset < right._offset; } );
        for ( size_t index = 0; index < listPlaced.size(); ++index )
        {
            const Placed& placed    = listPlaced[index];
            const uint64  usedEnd   = PackWriterInternal::alignToPackSector( placed._offset + placed._size );
            const uint64  nextBegin = index + 1 < listPlaced.size() ? listPlaced[index + 1]._offset : usedEnd;
            if ( nextBegin < placed._offset + placed._size )
            {
                SW_LOG_WARNING( "Pack layout: previous pack '%#' has overlapping payloads - the slot layout starts without it", _desc._previousPackPath );
                outTable = PreviousSlotTable{};
                return true;
            }
            outTable._mapHashToSlot[placed._hash] = { placed._offset, nextBegin - placed._offset };
            outTable._payloadEnd                  = outTable._payloadEnd > usedEnd ? outTable._payloadEnd : usedEnd;
        }
        return true;
    }

    bool PackWriter::layoutPack( const vector<const Entry*>& listSorted, const vector<CompressSlot>& listSlot, const PreviousSlotTable& previousTable, PackLayout& outLayout ) const
    {
        // 배치는 직렬이다 — 오프셋이 앞 항목들의 압축 크기에 달려 있다.
        // `Sorted` 는 FAT 이 페이로드 앞이고, `Slot` 은 페이로드가 헤더 바로 뒤 섹터에서 시작해 FAT 이 페이로드 뒤다(FAT 이 자라도 페이로드가 밀리지 않게).
        const bool bSlotLayout     = _desc._layoutMode == PackLayoutMode::Slot;
        outLayout._indexOffset     = PackWriterInternal::alignToPackSector( sizeof( PackHeader ) );
        outLayout._indexSize       = static_cast<uint64>( listSorted.size() ) * sizeof( PackFileEntryOnDisk );
        outLayout._dataStartOffset = bSlotLayout ? outLayout._indexOffset : PackWriterInternal::alignToPackSector( outLayout._indexOffset + outLayout._indexSize );

        vector<PackFileEntryOnDisk>& listDiskEntry = outLayout._listDiskEntry;
        listDiskEntry.reserve( listSorted.size() );
        vector<uint8>& stringPoolBytes  = outLayout._stringPoolBytes;
        uint64&        dataEndOffset    = outLayout._dataEndOffset;
        int64&         compressCPUNanos = outLayout._compressCPUNanos;
        dataEndOffset                   = outLayout._dataStartOffset;
        vector<uint64> listPayloadSize;
        listPayloadSize.reserve( listSorted.size() );
        for ( size_t index = 0; index < listSorted.size(); ++index )
        {
            const Entry*        pEntry = listSorted[index];
            const CompressSlot& slot   = listSlot[index];
            compressCPUNanos += slot._nanos;

            const bool bFitsEntryFields = pEntry->_bytes.size() <= MathUtil::kMaxUInt32 && stringPoolBytes.size() <= MathUtil::kMaxUInt32;
            if ( bFitsEntryFields == false )
            {
                SW_LOG_ERROR( "Pack entry '%#' does not fit the 32-bit entry fields", pEntry->_relativePath );
                return false;
            }
            if ( slot._bFailed )
                return false; // 코덱이 이유를 오류로 이미 남겼다
            const size_t payloadSize = slot._bCompressed ? slot._listPayload.size() : pEntry->_bytes.size();
            if ( payloadSize > MathUtil::kMaxUInt32 )
            {
                SW_LOG_ERROR( "Compressed pack entry '%#' does not fit the 32-bit entry fields", pEntry->_relativePath );
                return false;
            }

            listPayloadSize.push_back( payloadSize );
            PackFileEntryOnDisk diskEntry{};
            diskEntry._pathHash         = pEntry->_pathHash;
            diskEntry._dataOffset       = 0; // 모든 크기를 안 뒤에 아래에서 정한다
            diskEntry._compressedSize   = static_cast<uint32>( payloadSize );
            diskEntry._uncompressedSize = static_cast<uint32>( pEntry->_bytes.size() );
            diskEntry._crc32            = slot._crc32;
            diskEntry._stringPoolOffset = static_cast<uint32>( stringPoolBytes.size() );
            listDiskEntry.push_back( diskEntry );

            // 항목 경로 목록은 늘 담는다 — 런타임 폴더 나열이 이것으로 항목 이름을 알고, 리더는 없는 팩을 거절한다.
            stringPoolBytes.insert( stringPoolBytes.end(), pEntry->_relativePath.begin(), pEntry->_relativePath.end() );
            stringPoolBytes.push_back( 0 );
        }

        if ( bSlotLayout )
            placeSlotPayloads( listPayloadSize, previousTable, outLayout );
        else
        {
            for ( size_t index = 0; index < listDiskEntry.size(); ++index )
            {
                const uint64 payloadOffset       = PackWriterInternal::alignToPackSector( dataEndOffset );
                listDiskEntry[index]._dataOffset = payloadOffset;
                dataEndOffset                    = payloadOffset + listPayloadSize[index];
            }
        }

        // FAT 이 페이로드 앞(`Sorted`)이면 스트링 풀은 페이로드 뒤이고, 뒤(`Slot`)면 FAT 뒤다.
        const uint64 indexEndOffset = outLayout._indexOffset + outLayout._indexSize;
        const uint64 afterPayload   = bSlotLayout ? indexEndOffset : dataEndOffset;
        outLayout._stringPoolOffset = stringPoolBytes.empty() ? 0 : PackWriterInternal::alignToPackSector( afterPayload );
        const uint64 coveredEnd     = dataEndOffset > indexEndOffset ? dataEndOffset : indexEndOffset;
        outLayout._fileSize         = stringPoolBytes.empty() ? coveredEnd : outLayout._stringPoolOffset + stringPoolBytes.size();
        return true;
    }

    void PackWriter::placeSlotPayloads( const vector<uint64>& listPayloadSize, const PreviousSlotTable& previousTable, PackLayout& inoutLayout ) const
    {
        const uint64 regionBegin = inoutLayout._dataStartOffset;
        // 앞 빌드의 자리를 지키는 배치. 크기가 앞 자리에 들어가면 그 오프셋을 쓰고 아니면(새 항목 · 커진 항목) 앞 팩의 끝 뒤에 붙인다.
        const auto place = [&]( const PreviousSlotTable* pPrevious, vector<uint64>& outListOffset, uint64& outEnd, uint64& outUsed )
        {
            outListOffset.assign( listPayloadSize.size(), regionBegin );
            outUsed             = 0;
            uint64 appendCursor = pPrevious != nullptr && pPrevious->_payloadEnd > regionBegin ? pPrevious->_payloadEnd : regionBegin;
            outEnd              = appendCursor;
            for ( size_t index = 0; index < listPayloadSize.size(); ++index )
            {
                const uint64 size = listPayloadSize[index];
                if ( size == 0 )
                    continue; // 빈 항목은 자리를 쓰지 않는다
                outUsed += PackWriterInternal::alignToPackSector( size );
                if ( pPrevious != nullptr )
                {
                    const auto slotIt = pPrevious->_mapHashToSlot.find( inoutLayout._listDiskEntry[index]._pathHash );
                    if ( slotIt != pPrevious->_mapHashToSlot.end() && size <= slotIt->second._capacity )
                    {
                        outListOffset[index] = slotIt->second._offset;
                        continue;
                    }
                }
                appendCursor         = PackWriterInternal::alignToPackSector( appendCursor );
                outListOffset[index] = appendCursor;
                appendCursor += size;
                outEnd = appendCursor;
            }
        };

        vector<uint64> listOffset;
        uint64         dataEnd{ 0 };
        uint64         usedBytes{ 0 };
        bool           bUsePrevious = previousTable._mapHashToSlot.empty() == false;
        if ( bUsePrevious )
        {
            place( &previousTable, listOffset, dataEnd, usedBytes );
            const uint64 regionBytes = dataEnd > regionBegin ? dataEnd - regionBegin : 0;
            const uint64 holeBytes   = regionBytes > usedBytes ? regionBytes - usedBytes : 0;
            // 빈 자리가 한도를 넘으면 앞 빌드를 버리고 촘촘히 다시 배치한다(한 번 밀리는 대신 낭비가 쌓이지 않는다).
            bUsePrevious = holeBytes * 100 <= regionBytes * _desc._maxHolePercent;
        }
        if ( bUsePrevious == false )
            place( nullptr, listOffset, dataEnd, usedBytes );

        for ( size_t index = 0; index < listOffset.size(); ++index )
        {
            inoutLayout._listDiskEntry[index]._dataOffset = listOffset[index];
        }
        inoutLayout._dataEndOffset = dataEnd;
        inoutLayout._indexOffset   = PackWriterInternal::alignToPackSector( dataEnd );
    }

    void PackWriter::assemblePack( const vector<const Entry*>& listSorted, vector<CompressSlot>& inoutListSlot, const PackLayout& layout, vector<uint8>& outBytes ) const
    {
        const vector<PackFileEntryOnDisk>& listDiskEntry   = layout._listDiskEntry;
        const vector<uint8>&               stringPoolBytes = layout._stringPoolBytes;
        const uint64                       indexOffset     = layout._indexOffset;
        const uint64                       indexSize       = layout._indexSize;
        const uint64                       dataStartOffset = layout._dataStartOffset;
        const uint64                       dataEndOffset   = layout._dataEndOffset;

        const uint64 stringPoolOffset = layout._stringPoolOffset;

        PackHeader header{};
        header._magic            = kPackMagic;
        header._formatVersion    = kPackFormatVersion;
        header._dlcAppID         = _desc._dlcAppID;
        header._compressionType  = static_cast<uint8>( _desc._compression );
        header._encryptionType   = static_cast<uint8>( PackEncryptionType::None );
        header._sectorAlignment  = kPackSectorAlignment;
        header._flags            = static_cast<uint16>( PackFlag::HasCRC32 ) | static_cast<uint16>( PackFlag::HasStringPool );
        header._fileCount        = static_cast<uint32>( listDiskEntry.size() );
        header._indexOffset      = indexOffset;
        header._indexSize        = indexSize;
        header._stringPoolOffset = stringPoolOffset;
        header._stringPoolSize   = stringPoolBytes.size();
        header._totalDataSize    = dataEndOffset - dataStartOffset;

        // 페이로드를 제자리에 바로 복사한다(섹터 정렬 빈틈은 0). 항목의 압축 바이트는 복사 직후 놓아 최고 메모리를 압축 결과 + 팩 바이트 하나로 묶는다.
        outBytes.resize( static_cast<size_t>( layout._fileSize ), 0 );
        Memory::copy( outBytes.data(), &header, sizeof( PackHeader ) );
        if ( listDiskEntry.empty() == false )
            Memory::copy( outBytes.data() + indexOffset, listDiskEntry.data(), static_cast<size_t>( indexSize ) );
        for ( size_t index = 0; index < listDiskEntry.size(); ++index )
        {
            const PackFileEntryOnDisk& diskEntry = listDiskEntry[index];
            CompressSlot&              slot      = inoutListSlot[index];
            if ( diskEntry._compressedSize != 0 )
            {
                const uint8* pPayload = slot._bCompressed ? slot._listPayload.data() : listSorted[index]->_bytes.data();
                Memory::copy( outBytes.data() + diskEntry._dataOffset, pPayload, diskEntry._compressedSize );
            }
            slot._listPayload = vector<uint8>{};
        }
        if ( stringPoolBytes.empty() == false )
            Memory::copy( outBytes.data() + stringPoolOffset, stringPoolBytes.data(), stringPoolBytes.size() );
    }

    bool PackWriter::makePackBytes( vector<uint8>& outBytes, PackWriteStats* pOutStats ) const
    {
        outBytes.clear();
        const Stopwatch totalWatch;

        vector<const Entry*> listSorted;
        uint64               inputBytes{ 0 };
        if ( sortEntriesByPathHash( listSorted, inputBytes ) == false )
            return false;

        vector<CompressSlot> listSlot;
        int64                compressWallNanos{ 0 };
        if ( compressEntries( listSorted, inputBytes, listSlot, compressWallNanos ) == false )
            return false;

        PreviousSlotTable previousTable;
        if ( loadPreviousSlots( previousTable ) == false )
            return false;
        PackLayout layout;
        if ( layoutPack( listSorted, listSlot, previousTable, layout ) == false )
            return false;

        assemblePack( listSorted, listSlot, layout, outBytes );

        if ( pOutStats != nullptr )
        {
            PackWriteStats stats;
            stats._compressWallNanos = compressWallNanos;
            stats._compressCPUNanos  = layout._compressCPUNanos;
            stats._layoutNanos       = totalWatch.getElapsedNanoseconds() - compressWallNanos;
            stats._inputBytes        = inputBytes;
            stats._entryCount        = static_cast<uint32>( listSorted.size() );
            *pOutStats               = stats;
        }
        return true;
    }

    bool PackWriter::writePackFile( string_view packPath, PackWriteStats* pOutStats ) const
    {
        vector<uint8> bytes;
        if ( makePackBytes( bytes, pOutStats ) == false )
            return false;
        const Stopwatch writeWatch;
        if ( FileUtil::ensureParentDirectoryExists( packPath ) == false || FileUtil::writeFile( packPath, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Could not write pack file '%#'", packPath );
            return false;
        }
        if ( pOutStats != nullptr )
            pOutStats->_writeNanos = writeWatch.getElapsedNanoseconds();
        return true;
    }
} // namespace sw
