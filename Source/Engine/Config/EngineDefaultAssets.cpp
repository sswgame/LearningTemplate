#include "pch.h"

#include "Engine/Config/EngineDefaultAssets.h"

#include "Engine/Config/DefaultAssetTable.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Foundation/EngineDefines.h"

namespace sw
{
    unique_ptr<EngineDefaultAssets> EngineDefaultAssets::load( const AssetID& assetID )
    {
        unique_ptr<EngineDefaultAssets> assets = make_unique<EngineDefaultAssets>( ConstructKey{} );
        if ( assets->loadFromAsset( assetID ) == false )
            return nullptr;
        return assets;
    }

    bool EngineDefaultAssets::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        const string      path              = assetRelativePath.empty() ? string( constant::path::kEngineDefaultAssets ) : string( assetRelativePath );

        return DefaultAssetTable::load( AssetID{ path }, this, *EngineDefaultAssets::StaticType() );
    }
} // namespace sw

// 엔진 기본 에셋 표 - 그 칸들이 나머지 엔진 에셋(머티리얼 · 셰이더 · 파이프라인 · UI · 설정 표)을 끌고 오는 엔진 쪽 뿌리다(`AssetCookRoot.h`).
SW_ASSET_COOK_ROOT( EngineDefaultAssets, ::sw::constant::path::kEngineDefaultAssets );
