#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

#include "Core/String/RegistrationList.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    namespace
    {
        SW_DEFINE_REGISTRATION_LIST( getEntries, const NetSimGameEntry, "NetSimGameRegistry", RegistrationOrder::Insertion, true )
    } // namespace

    bool NetSimGameRegistry::registerGame( const NetSimGameEntry& entry )
    {
        if ( entry._name.empty() || entry._pfnCreate == nullptr )
        {
            SW_LOG_ERROR_ID( "NetSimGameRegistry.NetSimGameRegistryGameNeedsName", "NetSimGameRegistry: a game needs a name and a create function ('%#')", entry._name );
            return false;
        }
        const RegistrationResult result = getEntries().addLogged( &entry, "Net simulation game", entry._name );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    bool NetSimGameRegistry::unregisterGame( const NetSimGameEntry& entry )
    {
        return getEntries().remove( &entry );
    }

    const vector<const NetSimGameEntry*>& NetSimGameRegistry::getGames()
    {
        return getEntries().getItems();
    }

    const NetSimGameEntry* NetSimGameRegistry::findGame( string_view name )
    {
        return getEntries().findByName( name );
    }
} // namespace sw
