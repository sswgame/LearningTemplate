#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    namespace
    {
        struct RHIMemoryLedgerInternal
        {
            /** @brief `RHIMemoryKind` 값 순서의 표시 이름입니다. */
            static constexpr const utf8* kArrKindName[] = {
                "Texture",
                "RenderTarget",
                "TransientPool",
                "Buffer",
                "Staging",
                "Descriptor",
            };
            static_assert( sizeof( kArrKindName ) / sizeof( kArrKindName[0] ) == static_cast<size_t>( RHIMemoryKind::MaxKinds ),
                           "kArrKindName must have one name per RHIMemoryKind" );

            /** @brief 깊이 첨부(D24S8)의 텍셀 바이트입니다. 네 백엔드 모두 24 비트 깊이 + 8 비트 스텐실 한 칸으로 만듭니다. */
            static constexpr uint64 kDepthStencilTexelBytes = 4;

            /** @brief 바이트를 MB 의 10 배로 바꿉니다(소수 한 자리를 정수로 찍기 위해). */
            static constexpr uint64 toMegabytesX10( uint64 bytes ) { return ( bytes * 10 ) / ( 1024ull * 1024ull ); }

            /** @brief 아는 값은 "12.3 MB", 모르는 값은 "모름" 으로 적습니다. */
            static void formatMegabytes( uint64 bytes, bool bKnown, utf8* pOut, uint32 capacity )
            {
                if ( bKnown == false )
                {
                    formatstring( pOut, capacity, "모름" );
                    return;
                }
                const uint64 megabytesX10 = toMegabytesX10( bytes );
                formatstring( pOut, capacity, "%#.%# MB", megabytesX10 / 10, megabytesX10 % 10 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RHIMemoryLedger" );

    RHIGpuMemoryBudget::RHIGpuMemoryBudget() noexcept
        : _usageBytes{ 0 }
        , _budgetBytes{ 0 }
        , _availableBytes{ 0 }
        , _scope{ RHIGpuMemoryScope::Process }
        , _bUsageKnown{ SW_FALSE }
        , _bBudgetKnown{ SW_FALSE }
        , _bAvailableKnown{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    const utf8* RHIMemoryLedger::getKindName( RHIMemoryKind kind )
    {
        const uint32 kindIndex = static_cast<uint32>( kind );
        if ( kindIndex >= kRHIMemoryKindCount )
            return "Invalid";
        return RHIMemoryLedgerInternal::kArrKindName[kindIndex];
    }

    const utf8* RHIMemoryLedger::getSizeBasisName( RHIMemorySizeBasis basis )
    {
        switch ( basis )
        {
            case RHIMemorySizeBasis::Allocation:
                return "allocation";
            case RHIMemorySizeBasis::Logical:
                return "logical";
        }
        return "Invalid";
    }

    RHIMemoryKind RHIMemoryLedger::classifyTexture( const RHITextureDesc& desc )
    {
        if ( desc._bIsTransient != SW_FALSE )
            return RHIMemoryKind::TransientPool;
        const bool bAttachment = desc._bIsRenderTarget != SW_FALSE || desc._bIsDepthStencil != SW_FALSE || desc._bIsUnorderedAccess != SW_FALSE;
        return bAttachment ? RHIMemoryKind::RenderTarget : RHIMemoryKind::Texture;
    }

    uint64 RHIMemoryLedger::computeTextureLogicalBytes( const RHITextureDesc& desc )
    {
        if ( desc._width == 0 || desc._height == 0 )
            return kRHIMemoryUnknownBytes;

        // 깊이 첨부는 서술의 포맷과 상관없이 D24S8 로 만든다(블록 표는 깊이를 "업로드 대상 아님" 으로 0 바이트라 한다).
        RHIFormatBlockInfo block = getRhiFormatBlockInfo( desc._format );
        if ( desc._bIsDepthStencil != SW_FALSE )
            block = RHIFormatBlockInfo{ 1, 1, static_cast<uint32>( RHIMemoryLedgerInternal::kDepthStencilTexelBytes ) };
        if ( block._blockBytes == 0 )
            return kRHIMemoryUnknownBytes;

        const uint32 mipCount   = desc._mipLevels > 0 ? desc._mipLevels : 1u;
        const uint32 sliceCount = desc._arraySize > 0 ? desc._arraySize : 1u;
        uint64       sliceBytes{ 0 };
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const uint32 mipWidth  = ( desc._width >> mip ) > 0 ? ( desc._width >> mip ) : 1u;
            const uint32 mipHeight = ( desc._height >> mip ) > 0 ? ( desc._height >> mip ) : 1u;
            const uint64 blocksX   = ( mipWidth + block._blockWidth - 1 ) / block._blockWidth;
            const uint64 blocksY   = ( mipHeight + block._blockHeight - 1 ) / block._blockHeight;
            sliceBytes += blocksX * blocksY * block._blockBytes;
        }
        return sliceBytes * sliceCount;
    }

    RHIMemoryLedger::RHIMemoryLedger()
        : _mutex{}
        , _arrMapIdToEntry{}
        , _arrStat{}
        , _driverBudget{}
        , _sizeBasis{ RHIMemorySizeBasis::Allocation }
    {
    }

    void RHIMemoryLedger::recordAllocation( const RHIMemoryKey& key, RHIMemoryKind kind, uint64 bytes )
    {
        const uint32 spaceIndex = static_cast<uint32>( key._space );
        const uint32 kindIndex  = static_cast<uint32>( kind );
        if ( spaceIndex >= kRHIMemoryKeySpaceCount || kindIndex >= kRHIMemoryKindCount || key._id == 0 )
            return;

        std::scoped_lock<mutex>           lock{ _mutex };
        unordered_map<uint64, LiveEntry>& mapIdToEntry = _arrMapIdToEntry[spaceIndex];
        const auto                        entryIt      = mapIdToEntry.find( key._id );
        if ( entryIt != mapIdToEntry.end() )
        {
            SW_LOG_WARNING( "GPU memory key %# (space %#) recorded twice without a free - replacing the old %# bytes", key._id, spaceIndex,
                            entryIt->second._bytes );
            subtractLocked( entryIt->second );
            mapIdToEntry.erase( entryIt );
        }

        LiveEntry entry{};
        entry._bytes = bytes;
        entry._kind  = kind;
        mapIdToEntry.emplace( key._id, entry );

        RHIMemoryKindStats& stat = _arrStat[kindIndex];
        if ( bytes == kRHIMemoryUnknownBytes )
            ++stat._unknownSizeCount;
        else
        {
            stat._liveBytes += bytes;
            ++stat._liveCount;
        }
    }

    void RHIMemoryLedger::recordFree( const RHIMemoryKey& key )
    {
        const uint32 spaceIndex = static_cast<uint32>( key._space );
        if ( spaceIndex >= kRHIMemoryKeySpaceCount )
            return;

        std::scoped_lock<mutex>           lock{ _mutex };
        unordered_map<uint64, LiveEntry>& mapIdToEntry = _arrMapIdToEntry[spaceIndex];
        const auto                        entryIt      = mapIdToEntry.find( key._id );
        if ( entryIt == mapIdToEntry.end() )
            return;
        subtractLocked( entryIt->second );
        mapIdToEntry.erase( entryIt );
    }

    RHIMemoryKindStats RHIMemoryLedger::getStats( RHIMemoryKind kind ) const
    {
        const uint32 kindIndex = static_cast<uint32>( kind );
        if ( kindIndex >= kRHIMemoryKindCount )
            return RHIMemoryKindStats{};
        std::scoped_lock<mutex> lock{ _mutex };
        return _arrStat[kindIndex];
    }

    uint64 RHIMemoryLedger::getTrackedBytes() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        uint64                  total{ 0 };
        for ( const RHIMemoryKindStats& stat : _arrStat )
            total += stat._liveBytes;
        return total;
    }

    array<RHIMemoryKind, kRHIMemoryKindCount> RHIMemoryLedger::makeKindOrderByLiveBytes() const
    {
        // 정렬하는 동안 다른 스레드가 값을 바꿔도 비교가 흔들리지 않게 한 번 읽어 둔다.
        array<uint64, kRHIMemoryKindCount>        arrLiveBytes{};
        array<RHIMemoryKind, kRHIMemoryKindCount> arrKind{};
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( uint32 kindIndex = 0; kindIndex < kRHIMemoryKindCount; ++kindIndex )
            {
                arrLiveBytes[kindIndex] = _arrStat[kindIndex]._liveBytes;
                arrKind[kindIndex]      = static_cast<RHIMemoryKind>( kindIndex );
            }
        }
        std::stable_sort( arrKind.begin(), arrKind.end(), [&arrLiveBytes]( RHIMemoryKind lhs, RHIMemoryKind rhs )
        { return arrLiveBytes[static_cast<uint32>( lhs )] > arrLiveBytes[static_cast<uint32>( rhs )]; } );
        return arrKind;
    }

    RHIGpuMemoryBudget RHIMemoryLedger::getDriverBudget() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _driverBudget;
    }

    void RHIMemoryLedger::setDriverBudget( const RHIGpuMemoryBudget& budget )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _driverBudget = budget;
    }

    RHIGpuMemorySummary RHIMemoryLedger::makeSummary() const
    {
        RHIGpuMemorySummary summary{};
        {
            std::scoped_lock<mutex> lock{ _mutex };
            summary._budget    = _driverBudget;
            summary._sizeBasis = _sizeBasis;
            for ( const RHIMemoryKindStats& stat : _arrStat )
            {
                summary._trackedBytes += stat._liveBytes;
                summary._unknownSizeCount += stat._unknownSizeCount;
            }
        }
        // 디바이스 전체 사용량에는 다른 프로세스 몫이 섞여 있어 빼 봐야 "엔진 밖" 이 아니다.
        const bool bProcessUsage = summary._budget._bUsageKnown != SW_FALSE && summary._budget._scope == RHIGpuMemoryScope::Process;
        if ( bProcessUsage )
        {
            summary._outsideBytes  = static_cast<int64>( summary._budget._usageBytes ) - static_cast<int64>( summary._trackedBytes );
            summary._bOutsideKnown = SW_TRUE;
        }
        return summary;
    }

    void RHIMemoryLedger::report( [[maybe_unused]] const utf8* pBackendName ) const
    {
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        const RHIGpuMemorySummary summary = makeSummary();
        uint32                    liveCount{ 0 };
        for ( uint32 kindIndex = 0; kindIndex < kRHIMemoryKindCount; ++kindIndex )
            liveCount += getStats( static_cast<RHIMemoryKind>( kindIndex ) )._liveCount;

        const uint64 trackedX10 = RHIMemoryLedgerInternal::toMegabytesX10( summary._trackedBytes );
        SW_LOG_INFO( "[Profile] GPU memory by kind (live, %# · %# size)  %#.%# MB in %# resources  + %# of unknown size", pBackendName,
                     getSizeBasisName( summary._sizeBasis ), trackedX10 / 10, trackedX10 % 10, liveCount, summary._unknownSizeCount );
        for ( const RHIMemoryKind kind : makeKindOrderByLiveBytes() )
        {
            const RHIMemoryKindStats stats = getStats( kind );
            if ( stats._liveBytes == 0 && stats._unknownSizeCount == 0 )
                continue;
            const uint64 megabytesX10 = RHIMemoryLedgerInternal::toMegabytesX10( stats._liveBytes );
            const uint64 sharePermill = summary._trackedBytes == 0 ? 0 : ( stats._liveBytes * 1000 ) / summary._trackedBytes;
            SW_LOG_INFO( "[Profile]   %#  %#.%# MB  %#.%#%%  %# resources  + %# of unknown size", getKindName( kind ), megabytesX10 / 10, megabytesX10 % 10,
                         sharePermill / 10, sharePermill % 10, stats._liveCount, stats._unknownSizeCount );
        }

        const RHIGpuMemoryBudget& budget = summary._budget;
        utf8                      arrUsage[constant::kMaxBuffer32]{};
        utf8                      arrBudget[constant::kMaxBuffer32]{};
        utf8                      arrAvailable[constant::kMaxBuffer32]{};
        RHIMemoryLedgerInternal::formatMegabytes( budget._usageBytes, budget._bUsageKnown != SW_FALSE, arrUsage, constant::kMaxBuffer32 );
        RHIMemoryLedgerInternal::formatMegabytes( budget._budgetBytes, budget._bBudgetKnown != SW_FALSE, arrBudget, constant::kMaxBuffer32 );
        RHIMemoryLedgerInternal::formatMegabytes( budget._availableBytes, budget._bAvailableKnown != SW_FALSE, arrAvailable, constant::kMaxBuffer32 );
        const utf8* pScope = budget._bUsageKnown == SW_FALSE             ? ""
                           : budget._scope == RHIGpuMemoryScope::Process ? " (이 프로세스)"
                                                                         : " (디바이스 전체 — 다른 프로세스 몫 포함)";
        SW_LOG_INFO( "[Profile]   드라이버: 사용량 %#%#  예산 %#  남은 양 %#", arrUsage, pScope, arrBudget, arrAvailable );
        if ( summary._bOutsideKnown == SW_FALSE )
        {
            SW_LOG_INFO( "[Profile]   (장부 밖 — 스왑체인 · 드라이버 내부 · 장부에 없는 디바이스 자원)  모름 — 이 프로세스의 드라이버 사용량이 없다" );
            return;
        }
        const bool   bNegative  = summary._outsideBytes < 0;
        const uint64 outsideX10 = RHIMemoryLedgerInternal::toMegabytesX10( static_cast<uint64>( bNegative ? -summary._outsideBytes : summary._outsideBytes ) );
        SW_LOG_INFO( "[Profile]   (장부 밖 — 스왑체인 · 드라이버 내부 · 장부에 없는 디바이스 자원)  %#%#.%# MB", bNegative ? "-" : "", outsideX10 / 10,
                     outsideX10 % 10 );
#endif
    }

    void RHIMemoryLedger::subtractLocked( const LiveEntry& entry )
    {
        RHIMemoryKindStats& stat = _arrStat[static_cast<uint32>( entry._kind )];
        if ( entry._bytes == kRHIMemoryUnknownBytes )
        {
            if ( stat._unknownSizeCount > 0 )
                --stat._unknownSizeCount;
            return;
        }
        stat._liveBytes -= entry._bytes <= stat._liveBytes ? entry._bytes : stat._liveBytes;
        if ( stat._liveCount > 0 )
            --stat._liveCount;
    }
} // namespace sw
