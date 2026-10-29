#include "pch.h"

#include "ModuleManager/ModuleStateStore.h"

namespace sw
{
    void ModuleStateStore::store( string_view key, vector<uint8>&& bytes )
    {
        _mapStored[string{ key }] = std::move( bytes );
    }

    bool ModuleStateStore::take( string_view key, vector<uint8>& outBytes )
    {
        const auto it = _mapStored.find( string{ key } );
        if ( it == _mapStored.end() )
            return false;
        outBytes = std::move( it->second );
        _mapStored.erase( it );
        return true;
    }
} // namespace sw
