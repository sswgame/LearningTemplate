#include "pch.h"

#include "Engine/Content/Pack/AssetCookRoot.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    SW_DEFINE_REGISTRATION_LIST( AssetCookRootRegistry::getRegistrars, const AssetCookRootRegistrar, "AssetCookRootRegistry" )

    void AssetCookRootRegistry::appendRoots( vector<string>& outListAssetID )
    {
        for ( const AssetCookRootRegistrar* pRegistrar : getRegistrars().getItems() )
        {
            outListAssetID.emplace_back( pRegistrar->getAssetID() );
        }
    }

    bool AssetCookRootRegistry::hasRoot( string_view assetID )
    {
        for ( const AssetCookRootRegistrar* pRegistrar : getRegistrars().getItems() )
        {
            if ( pRegistrar->getAssetID() == assetID )
                return true;
        }
        return false;
    }

    AssetCookRootRegistrar::AssetCookRootRegistrar( string_view assetID )
        : _assetID{ assetID }
    {
        (void)AssetCookRootRegistry::getRegistrars().add( this );
    }

    AssetCookRootRegistrar::~AssetCookRootRegistrar()
    {
        (void)AssetCookRootRegistry::getRegistrars().remove( this );
    }
} // namespace sw
