#include "pch.h"

#include "Engine/Content/Pack/AssetCookRoot.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    vector<string_view>& AssetCookRootRegistry::getRoots()
    {
        // 함수 안 정적 값 — 다른 번역 단위의 정적 등록자가 먼저 불러도 표가 이미 있다.
        static vector<string_view> s_listRoot;
        return s_listRoot;
    }

    void AssetCookRootRegistry::appendRoots( vector<string>& outListAssetID )
    {
        for ( const string_view assetID : getRoots() )
        {
            outListAssetID.emplace_back( assetID );
        }
    }

    bool AssetCookRootRegistry::hasRoot( string_view assetID )
    {
        for ( const string_view root : getRoots() )
        {
            if ( root == assetID )
                return true;
        }
        return false;
    }

    AssetCookRootRegistrar::AssetCookRootRegistrar( string_view assetID )
        : _assetID{ assetID }
    {
        AssetCookRootRegistry::getRoots().push_back( _assetID );
    }

    AssetCookRootRegistrar::~AssetCookRootRegistrar()
    {
        VectorUtil::removeIf( AssetCookRootRegistry::getRoots(), [this]( const string_view root )
        { return root.data() == _assetID.data() && root.size() == _assetID.size(); } );
    }
} // namespace sw
