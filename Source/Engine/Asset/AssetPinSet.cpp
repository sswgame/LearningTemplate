#include "pch.h"

#include "Engine/Asset/AssetPinSet.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    AssetPinSet::AssetPinSet( uint64 budgetBytes )
        : _mapEntry{}
        , _budgetBytes{ budgetBytes }
        , _pinnedBytes{ 0 }
    {
    }

    AssetPinSet::~AssetPinSet() = default;

    bool AssetPinSet::add( string_view path, shared_ptr<const void> pin, uint64 costBytes )
    {
        if ( pin == nullptr )
            return false;
        const string key = FileUtil::normalizePath( path ).str();
        if ( _mapEntry.touch( key ) != nullptr )
            return true;
        if ( costBytes > _budgetBytes )
        {
            SW_LOG_WARNING_ID( "AssetPinSet.AssetBytesLargerPin", "Asset '%#' (%# bytes) is larger than the pin budget (%# bytes) - it is not pinned", path, costBytes, _budgetBytes );
            return false;
        }
        Entry entry{};
        entry._pin       = std::move( pin );
        entry._costBytes = costBytes;
        _mapEntry.insertOrAssign( key, std::move( entry ) );
        _pinnedBytes += costBytes;
        evictOverBudget();
        return true;
    }

    void AssetPinSet::touch( string_view path )
    {
        (void)_mapEntry.touch( FileUtil::normalizePath( path ).str() ); // 붙들지 않은 경로면 할 일이 없다
    }

    bool AssetPinSet::remove( string_view path )
    {
        const string key    = FileUtil::normalizePath( path ).str();
        const Entry* pEntry = _mapEntry.find( key );
        if ( pEntry == nullptr )
            return false;
        _pinnedBytes -= pEntry->_costBytes;
        return _mapEntry.erase( key );
    }

    void AssetPinSet::clear()
    {
        _mapEntry.clear();
        _pinnedBytes = 0;
    }

    bool AssetPinSet::contains( string_view path ) const
    {
        return _mapEntry.find( FileUtil::normalizePath( path ).str() ) != nullptr;
    }

    void AssetPinSet::evictOverBudget()
    {
        while ( _pinnedBytes > _budgetBytes )
        {
            // 방금 넣은 것(가장 최근) 혼자서는 예산 안이다(`add` 가 거른다) — 넘쳤으면 그 앞에 놓을 것이 반드시 있다.
            SW_ASSERT( _mapEntry.size() > 1 );
            const auto oldest = _mapEntry.begin();
            _pinnedBytes -= oldest->_value._costBytes;
            (void)_mapEntry.erase( oldest );
        }
    }
} // namespace sw
