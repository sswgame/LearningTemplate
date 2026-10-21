#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    namespace
    {
        vector<NetSimGameEntry>& getEntries()
        {
            static vector<NetSimGameEntry> s_listEntry;
            return s_listEntry;
        }
    } // namespace

    bool NetSimGameRegistry::registerGame( string_view name, unique_ptr<INetSimGame> ( *pfnCreate )() )
    {
        if ( name.empty() || pfnCreate == nullptr )
        {
            SW_LOG_ERROR( "NetSimGameRegistry: a game needs a name and a create function ('%#')", string( name ) );
            return false;
        }
        if ( findGame( name ) != nullptr )
        {
            SW_LOG_ERROR( "NetSimGameRegistry: '%#' is already registered", string( name ) );
            return false;
        }
        getEntries().push_back( NetSimGameEntry{ string( name ), pfnCreate } );
        return true;
    }

    bool NetSimGameRegistry::unregisterGame( string_view name )
    {
        vector<NetSimGameEntry>& listEntry = getEntries();
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            if ( listEntry[index]._name != name )
                continue;
            listEntry.erase( listEntry.begin() + static_cast<std::ptrdiff_t>( index ) );
            return true;
        }
        return false;
    }

    const vector<NetSimGameEntry>& NetSimGameRegistry::getGames()
    {
        return getEntries();
    }

    const NetSimGameEntry* NetSimGameRegistry::findGame( string_view name )
    {
        for ( const NetSimGameEntry& entry : getEntries() )
        {
            if ( entry._name == name )
                return &entry;
        }
        return nullptr;
    }
} // namespace sw
