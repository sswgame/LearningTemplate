#include "pch.h"

#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw
{
    /** @brief 코스터 레이아웃(`CoasterLayoutCatalog`)의 종류입니다. */
    SW_MODULE_SHARED_ASSET_CACHE( Coasters, CoasterLayoutCatalog, SW_KIT_API,
                                  AssetTypeDef::makeDataFile( CoasterLayoutCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/coasters.xml" }, AssetTypeLoader::of<CoasterLayoutCatalog>( MemoryTag::Game ) ) );

    /** @brief 공원 배치(`ParkLayout`, 놀이기구 목록)의 종류와 그 공유 캐시입니다. 로더가 같은 팩의 코스터 레이아웃을 공유 캐시에서 받는다(`ParkLayout::loadFromAsset`). */
    SW_MODULE_SHARED_ASSET_CACHE( ParkLayout, ParkLayout, SW_KIT_API,
                                  AssetTypeDef::makeDataFile( ParkLayout::kDataKind, AssetPathRule{ "game/", {}, "/data/rides.xml" }, AssetTypeLoader::of<ParkLayout>( MemoryTag::Game ) ) );
} // namespace sw
