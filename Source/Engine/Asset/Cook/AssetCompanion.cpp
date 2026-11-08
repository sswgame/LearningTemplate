#include "pch.h"

#include "Engine/Asset/Cook/AssetCompanion.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_DEFINE_REGISTRATION_LIST( AssetCompanionRegistry::getRegistrars, const AssetCompanionRegistrar, "AssetCompanionRegistry" )

    void AssetCompanionRegistry::appendCompanionPaths( string_view assetID, vector<string>& outListPath )
    {
        for ( const AssetCompanionRegistrar* pRegistrar : getRegistrars().getItems() )
        {
            string path = pRegistrar->getRule()._pMakePath( assetID );
            if ( path.empty() == false && string_view( path ) != assetID )
                outListPath.push_back( std::move( path ) );
        }
    }

    bool AssetCompanionRegistry::hasRule( string_view name )
    {
        for ( const AssetCompanionRegistrar* pRegistrar : getRegistrars().getItems() )
        {
            if ( StringUtil::equals( pRegistrar->getRule()._pName, name ) )
                return true;
        }
        return false;
    }

    AssetCompanionRegistrar::AssetCompanionRegistrar( const AssetCompanionRule& rule )
        : _rule{ rule }
    {
        (void)AssetCompanionRegistry::getRegistrars().add( this );
    }

    AssetCompanionRegistrar::~AssetCompanionRegistrar()
    {
        (void)AssetCompanionRegistry::getRegistrars().remove( this );
    }
} // namespace sw
