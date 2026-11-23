#include "pch.h"

#include "Engine/Asset/Cook/AssetCompanion.h"

namespace sw
{
    namespace
    {
        SW_DEFINE_REGISTRATION_LIST( getAssetCompanionRules, const AssetCompanionRule, "AssetCompanionRule", RegistrationOrder::Insertion, true )
    } // namespace

    bool AssetCompanionRule::add( const AssetCompanionRule* pRule )
    {
        return getAssetCompanionRules().addLogged( pRule, "Asset companion rule", pRule->_pName ) == RegistrationResult::Added;
    }

    void AssetCompanionRule::remove( const AssetCompanionRule* pRule )
    {
        (void)getAssetCompanionRules().remove( pRule ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    void AssetCompanionRule::appendCompanionPaths( string_view assetID, vector<string>& outListPath )
    {
        for ( const AssetCompanionRule* pRule : getAssetCompanionRules().getItems() )
        {
            string path = pRule->_pMakePath( assetID );
            if ( path.empty() == false && string_view( path ) != assetID )
                outListPath.push_back( std::move( path ) );
        }
    }

    bool AssetCompanionRule::hasRule( string_view name )
    {
        return getAssetCompanionRules().findByName( name ) != nullptr;
    }
} // namespace sw
