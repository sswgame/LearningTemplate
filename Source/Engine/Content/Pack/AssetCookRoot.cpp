#include "pch.h"

#include "Engine/Content/Pack/AssetCookRoot.h"

namespace sw
{
    namespace
    {
        SW_DEFINE_REGISTRATION_LIST( getAssetCookRoots, const AssetCookRoot, "AssetCookRoot", RegistrationOrder::Insertion, true )
    } // namespace

    bool AssetCookRoot::add( const AssetCookRoot* pRoot )
    {
        return getAssetCookRoots().addLogged( pRoot, "Asset cook root", pRoot->_assetID ) == RegistrationResult::Added;
    }

    void AssetCookRoot::remove( const AssetCookRoot* pRoot )
    {
        (void)getAssetCookRoots().remove( pRoot ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    void AssetCookRoot::appendRoots( vector<string>& outListAssetID )
    {
        for ( const AssetCookRoot* pRoot : getAssetCookRoots().getItems() )
        {
            outListAssetID.emplace_back( pRoot->_assetID );
        }
    }

    bool AssetCookRoot::hasRoot( string_view assetID )
    {
        return getAssetCookRoots().findByName( assetID ) != nullptr;
    }
} // namespace sw
