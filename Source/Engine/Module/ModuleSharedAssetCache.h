/**
 * @file ModuleSharedAssetCache.h
 * @brief 모듈 종류의 공유 캐시를 모듈 서브시스템으로 올리는 매크로입니다. 모듈 서브시스템 등록자(맨 위 층)를 쓰므로 낮은 층이 보는 `SharedAssetCacheDefinition.h` 와 나눈다.
 */
#pragma once
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

/**
 * @brief 모듈(GameFramework · 키트 · 게임)의 종류 @p AssetType 의 정의를 적고(@p ... 는 `AssetTypeDef` 값), 그 모듈에서 인스턴스화해(@p ExportMacro 로 내보낸다 — 게임처럼 내보내지 않으면 빈 칸)
 *        모듈 서브시스템으로 등록하며, 같은 정의로 종류를 등록합니다(`SW_ASSET_TYPE`). 인스턴스화하는 .cpp, `namespace sw` 안에 둔다.
 * @param id 정적 이름 · 서브시스템 이름을 만드는 식별자입니다.
 * @code
 *     SW_MODULE_SHARED_ASSET_CACHE( Crops, CropCatalog, SW_KIT_API, AssetTypeDef::makeDataFile( CropCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/crops.xml" }, AssetTypeLoader::of<CropCatalog>() ) );
 * @endcode
 */
#define SW_MODULE_SHARED_ASSET_CACHE( id, AssetType, ExportMacro, ... )                                                                                          \
    static_assert( SharedAssetCacheDecl<AssetType>::kValue, "declare the cache in the asset header: SW_DECLARE_SHARED_ASSET_CACHE( <extern macro>, <asset> )" ); \
    template <>                                                                                                                                                  \
    struct SharedAssetCacheTraits<AssetType>                                                                                                                     \
    {                                                                                                                                                            \
        static constexpr AssetTypeDef kType = __VA_ARGS__;                                                                                                       \
    };                                                                                                                                                           \
    template class ExportMacro           SharedAssetCache<AssetType>;                                                                                            \
    class SharedAssetCacheSubsystem_##id final : public ::sw::SharedAssetCacheSubsystem<AssetType>                                                               \
    {                                                                                                                                                            \
    public:                                                                                                                                                      \
        static constexpr const utf8* kSubsystemName = "SharedAssetCache." #id;                                                                                   \
        const utf8*                  getSubsystemName() const override                                                                                           \
        {                                                                                                                                                        \
            return kSubsystemName;                                                                                                                               \
        }                                                                                                                                                        \
    };                                                                                                                                                           \
    SW_MODULE_SUBSYSTEM( Engine, SharedAssetCacheSubsystem_##id, SharedAssetCacheSubsystem_##id );                                                               \
    SW_ASSET_TYPE( id, SharedAssetCacheTraits<AssetType>::kType )
