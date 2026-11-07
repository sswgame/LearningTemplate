#include "pch.h"

#include "Engine/Asset/Cook/AssetCompanion.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

namespace sw
{
    vector<AssetCompanionRule>& AssetCompanionRegistry::getRules()
    {
        // 함수 안 정적 값 — 다른 번역 단위의 정적 등록자가 먼저 불러도 표가 이미 있다.
        static vector<AssetCompanionRule> s_listRule;
        return s_listRule;
    }

    void AssetCompanionRegistry::appendCompanionPaths( string_view assetID, vector<string>& outListPath )
    {
        for ( const AssetCompanionRule& rule : getRules() )
        {
            string path = rule._pMakePath( assetID );
            if ( path.empty() == false && string_view( path ) != assetID )
                outListPath.push_back( std::move( path ) );
        }
    }

    bool AssetCompanionRegistry::hasRule( string_view name )
    {
        for ( const AssetCompanionRule& rule : getRules() )
        {
            if ( StringUtil::equals( rule._pName, name ) )
                return true;
        }
        return false;
    }

    AssetCompanionRegistrar::AssetCompanionRegistrar( const AssetCompanionRule& rule )
        : _rule{ rule }
    {
        AssetCompanionRegistry::getRules().push_back( _rule );
    }

    AssetCompanionRegistrar::~AssetCompanionRegistrar()
    {
        VectorUtil::removeIf( AssetCompanionRegistry::getRules(), [this]( const AssetCompanionRule& rule )
        { return rule._pMakePath == _rule._pMakePath && rule._pName == _rule._pName; } );
    }
} // namespace sw
