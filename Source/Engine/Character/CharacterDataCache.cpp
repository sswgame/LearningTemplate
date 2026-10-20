#include "pch.h"

#include "Engine/Character/CharacterDataCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetCacheDefinition.h"

namespace sw
{
    namespace
    {
        struct CharacterDataCacheInternal
        {
            static constexpr const utf8* kDefaultSocketKindPath = "engine/character/default.socketkinds.xml";

            static const SocketKindTable& loadDefaultKinds()
            {
                static SocketKindTable s_kinds;
                static const bool      s_bLoaded = s_kinds.loadFromResource( kDefaultSocketKindPath );
                (void)s_bLoaded;
                return s_kinds;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SocketSet>
    {
        static constexpr const utf8*           kKindName   = "SocketSet";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, SocketSet& outSocketSet ) { return outSocketSet.loadFromResource( path, CharacterDataCacheInternal::loadDefaultKinds() ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<AnimationNotifyTable>
    {
        static constexpr const utf8*           kKindName   = "AnimationNotifyTable";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, AnimationNotifyTable& outTable ) { return outTable.loadFromResource( path, AnimationNotifyHandlerRegistry::getDefault() ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<PairedAnimationDef>
    {
        static constexpr const utf8* kKindName  = "PairedAnimationDef";
        static constexpr MemoryTag   kMemoryTag = MemoryTag::Animation;
        /** @brief 돌고 있는 세션이 옛 에셋의 역할 인덱스를 읽으므로 제자리에서 바꾸지 않는다. */
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::Detach;

        [[nodiscard]] static bool load( string_view path, PairedAnimationDef& outDef ) { return outDef.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<PhysicsAsset>
    {
        static constexpr const utf8*           kKindName   = "PhysicsAsset";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Physics;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( string_view path, PhysicsAsset& outAsset ) { return outAsset.loadFromResource( path ); }
    };
} // namespace sw

namespace sw
{
    template class SW_API SharedAssetCache<SocketSet>;
    template class SW_API SharedAssetCache<AnimationNotifyTable>;
    template class SW_API SharedAssetCache<PairedAnimationDef>;
    template class SW_API SharedAssetCache<PhysicsAsset>;

    SW_BUILT_IN_ASSET_CACHE( SocketSetCache );
    SW_BUILT_IN_ASSET_CACHE( AnimationNotifyTableCache );
    SW_BUILT_IN_ASSET_CACHE( PairedAnimationDefCache );
    SW_BUILT_IN_ASSET_CACHE( PhysicsAssetCache );
} // namespace sw
