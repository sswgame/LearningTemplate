#include "pch.h"

#include "Engine/Resource/AssetPinSet.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "AssetPinSet" );

    AssetPinSet::AssetPinSet( uint64 budgetBytes )
        : _listEntry{}
        , _budgetBytes{ budgetBytes }
        , _pinnedBytes{ 0 }
        , _useClock{ 0 }
    {
    }

    AssetPinSet::~AssetPinSet() = default;

    bool AssetPinSet::add( string_view path, shared_ptr<const void> pin, uint64 costBytes )
    {
        if ( pin == nullptr )
            return false;
        const size_t existing = findIndex( path );
        if ( existing < _listEntry.size() )
        {
            _listEntry[existing]._lastUse = ++_useClock;
            return true;
        }
        if ( costBytes > _budgetBytes )
        {
            SW_LOG_WARNING( "Asset '%#' (%# bytes) is larger than the pin budget (%# bytes) - it is not pinned", path, costBytes, _budgetBytes );
            return false;
        }
        Entry entry{};
        entry._path      = FileUtil::normalizePath( path );
        entry._pin       = std::move( pin );
        entry._costBytes = costBytes;
        entry._lastUse   = ++_useClock;
        _listEntry.push_back( std::move( entry ) );
        _pinnedBytes += costBytes;
        evictOverBudget( _listEntry.size() - 1 );
        return true;
    }

    void AssetPinSet::touch( string_view path )
    {
        const size_t index = findIndex( path );
        if ( index < _listEntry.size() )
            _listEntry[index]._lastUse = ++_useClock;
    }

    bool AssetPinSet::remove( string_view path )
    {
        const size_t index = findIndex( path );
        if ( index >= _listEntry.size() )
            return false;
        _pinnedBytes -= _listEntry[index]._costBytes;
        _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
        return true;
    }

    void AssetPinSet::clear()
    {
        _listEntry.clear();
        _pinnedBytes = 0;
    }

    bool AssetPinSet::contains( string_view path ) const
    {
        return findIndex( path ) < _listEntry.size();
    }

    size_t AssetPinSet::findIndex( string_view path ) const
    {
        const string key = FileUtil::normalizePath( path );
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._path == key )
                return index;
        }
        return _listEntry.size();
    }

    void AssetPinSet::evictOverBudget( size_t keepIndex )
    {
        const string keepPath = _listEntry[keepIndex]._path;
        while ( _pinnedBytes > _budgetBytes )
        {
            size_t oldest = _listEntry.size();
            for ( size_t index = 0; index < _listEntry.size(); ++index )
            {
                if ( _listEntry[index]._path == keepPath )
                    continue;
                const bool bOlder = oldest == _listEntry.size() || _listEntry[index]._lastUse < _listEntry[oldest]._lastUse;
                if ( bOlder )
                    oldest = index;
            }
            // 넣은 것 혼자서는 예산 안이다(`add` 가 거른다) — 남은 것이 있으면 반드시 놓을 것이 있다.
            SW_ASSERT( oldest < _listEntry.size() );
            _pinnedBytes -= _listEntry[oldest]._costBytes;
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( oldest ) );
        }
    }
} // namespace sw
