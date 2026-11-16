#include "pch.h"

#include "Engine/Asset/AssetPinSet.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    AssetPinSet::AssetPinSet( uint64 budgetBytes )
        : _listEntryByUse{}
        , _mapPathToEntry{}
        , _budgetBytes{ budgetBytes }
        , _pinnedBytes{ 0 }
    {
    }

    AssetPinSet::~AssetPinSet() = default;

    bool AssetPinSet::add( string_view path, shared_ptr<const void> pin, uint64 costBytes )
    {
        if ( pin == nullptr )
            return false;
        string     key      = FileUtil::normalizePath( path ).str();
        const auto existing = _mapPathToEntry.find( key );
        if ( existing != _mapPathToEntry.end() )
        {
            markUsed( existing->second );
            return true;
        }
        if ( costBytes > _budgetBytes )
        {
            SW_LOG_WARNING_ID( "AssetPinSet.AssetBytesLargerPin", "Asset '%#' (%# bytes) is larger than the pin budget (%# bytes) - it is not pinned", path, costBytes, _budgetBytes );
            return false;
        }
        Entry entry{};
        entry._path      = key;
        entry._pin       = std::move( pin );
        entry._costBytes = costBytes;
        _listEntryByUse.push_back( std::move( entry ) );
        _mapPathToEntry.emplace( std::move( key ), std::prev( _listEntryByUse.end() ) );
        _pinnedBytes += costBytes;
        evictOverBudget();
        return true;
    }

    void AssetPinSet::touch( string_view path )
    {
        const auto found = _mapPathToEntry.find( FileUtil::normalizePath( path ).str() );
        if ( found != _mapPathToEntry.end() )
            markUsed( found->second );
    }

    bool AssetPinSet::remove( string_view path )
    {
        const auto found = _mapPathToEntry.find( FileUtil::normalizePath( path ).str() );
        if ( found == _mapPathToEntry.end() )
            return false;
        _pinnedBytes -= found->second->_costBytes;
        _listEntryByUse.erase( found->second );
        _mapPathToEntry.erase( found );
        return true;
    }

    void AssetPinSet::clear()
    {
        _mapPathToEntry.clear();
        _listEntryByUse.clear();
        _pinnedBytes = 0;
    }

    bool AssetPinSet::contains( string_view path ) const
    {
        return _mapPathToEntry.find( FileUtil::normalizePath( path ).str() ) != _mapPathToEntry.end();
    }

    void AssetPinSet::markUsed( EntryList::iterator entry )
    {
        _listEntryByUse.splice( _listEntryByUse.end(), _listEntryByUse, entry );
    }

    void AssetPinSet::evictOverBudget()
    {
        while ( _pinnedBytes > _budgetBytes )
        {
            // 방금 넣은 것(끝) 혼자서는 예산 안이다(`add` 가 거른다) — 넘쳤으면 그 앞에 놓을 것이 반드시 있다.
            SW_ASSERT( _listEntryByUse.size() > 1 );
            const Entry& oldest = _listEntryByUse.front();
            _pinnedBytes -= oldest._costBytes;
            _mapPathToEntry.erase( oldest._path );
            _listEntryByUse.erase( _listEntryByUse.begin() );
        }
    }
} // namespace sw
