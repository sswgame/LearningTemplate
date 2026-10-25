#include "pch.h"

#include "Engine/Resource/Cache/CacheStatsUtil.h"

#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    SW_LOG_CALLER( "CacheStats" );

    namespace
    {
        struct CacheStatsUtilInternal
        {
            /** @brief 보고 한 줄입니다. 계측 칸이 있으면 `_bCounted`, 없으면 레지스트리에만 있는 `IAssetCache` 입니다. */
            struct Row
            {
                string _name{};
                uint64 _hitCount{ 0 };
                uint64 _missCount{ 0 };
                uint64 _clearCount{ 0 };
                int64  _itemCount{ 0 };
                bool   _bItemCountKnown{ false };
                bool   _bCounted{ false };
                string _uncountedReason{}; ///< `_bCounted` 가 아닐 때 캐시가 선언한 사유(`CacheTracking::uncounted`). 칸이 없는데 선언도 없으면 비어 있다(오류)
            };

            static bool isNameBefore( const Row& lhs, const Row& rhs ) { return lhs._name < rhs._name; }

            /** @brief 그 이름의 줄이 이미 있는지 봅니다. */
            static bool containsName( const vector<Row>& listRow, string_view name )
            {
                for ( const Row& row : listRow )
                {
                    if ( string_view{ row._name } == name )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void CacheStatsUtil::logReport( [[maybe_unused]] const utf8* pTitle )
    {
        // 보고는 Info 로그로만 나간다. 그것이 사라지는 구성(Shipping)에는 본문도 두지 않는다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        using Internal = CacheStatsUtilInternal;

        vector<CacheStatsSnapshot> listSnapshot;
        CacheStats::collect( listSnapshot );
        AssetManager*         pAssetManager = engine::findSubsystem<AssetManager>(); // 바인딩 전이면 nullptr — 항목 수만 모른다
        vector<Internal::Row> listRow;
        listRow.reserve( listSnapshot.size() );
        for ( const CacheStatsSnapshot& snapshot : listSnapshot )
        {
            Internal::Row row{};
            row._name            = snapshot._name;
            row._hitCount        = snapshot._hitCount;
            row._missCount       = snapshot._missCount;
            row._clearCount      = snapshot._clearCount;
            row._itemCount       = snapshot._itemCount;
            row._bItemCountKnown = snapshot._bItemCountKnown;
            row._bCounted        = true;
            if ( row._bItemCountKnown == false && pAssetManager != nullptr )
            {
                const IAssetCache* pCache = pAssetManager->findAssetCache( row._name );
                if ( pCache != nullptr )
                {
                    row._itemCount       = static_cast<int64>( pCache->getCachedCount() );
                    row._bItemCountKnown = true;
                }
            }
            listRow.push_back( std::move( row ) );
        }
        // 계측 칸이 없는 캐시도 보인다 — 세지 않는 캐시가 보고에서 조용히 빠지지 않게.
        if ( pAssetManager != nullptr )
        {
            for ( const IAssetCache* pCache : pAssetManager->getAllAssetCache() )
            {
                const utf8* pKindName = pCache->getAssetKindName();
                if ( pKindName == nullptr || Internal::containsName( listRow, pKindName ) )
                    continue;
                Internal::Row row{};
                row._name            = pKindName;
                row._itemCount       = static_cast<int64>( pCache->getCachedCount() );
                row._bItemCountKnown = true;
                if ( pCache->getUncountedReason() != nullptr )
                    row._uncountedReason = pCache->getUncountedReason();
                listRow.push_back( std::move( row ) );
            }
        }
        std::sort( listRow.begin(), listRow.end(), &Internal::isNameBefore );

        SW_LOG_INFO( "[CacheStats] %# - %# caches (hit / miss / clear counted since the measure window started; 'no counters' = the cache has no lookup to count)", pTitle,
                     listRow.size() );
        for ( const Internal::Row& row : listRow )
        {
            utf8 arrItemCount[24]{};
            formatstring( arrItemCount, static_cast<uint32>( sizeof( arrItemCount ) ), "%#", row._itemCount );
            const utf8* pItemCount = row._bItemCountKnown ? arrItemCount : "?";
            if ( row._bCounted == false )
            {
                SW_LOG_INFO( "[CacheStats]   %#  items %#  no counters (%#)", row._name, pItemCount,
                             row._uncountedReason.empty() ? "declared counted but has no counter slot" : row._uncountedReason.c_str() );
                continue;
            }
            const uint64 lookupCount  = row._hitCount + row._missCount;
            const uint64 ratePermille = lookupCount == 0 ? 0 : ( row._hitCount * 1000 ) / lookupCount;
            if ( lookupCount == 0 )
            {
                SW_LOG_INFO( "[CacheStats]   %#  items %#  hit %#  miss %#  rate -  clear %#", row._name, pItemCount, row._hitCount, row._missCount, row._clearCount );
                continue;
            }
            SW_LOG_INFO( "[CacheStats]   %#  items %#  hit %#  miss %#  rate %#.%#%%  clear %#", row._name, pItemCount, row._hitCount, row._missCount, ratePermille / 10,
                         ratePermille % 10, row._clearCount );
        }
#endif
    }
} // namespace sw
