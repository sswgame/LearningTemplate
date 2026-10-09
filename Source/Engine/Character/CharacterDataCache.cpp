#include "pch.h"

#include "Engine/Character/CharacterDataCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Character/AnimNotify/AnimNotifyTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Resource/Cache/SharedAssetTable.h"

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

            [[nodiscard]] static bool loadSocketSet( string_view path, SocketSet& outSocketSet ) { return outSocketSet.loadFromResource( path, loadDefaultKinds() ); }
            [[nodiscard]] static bool loadNotifyTable( string_view path, AnimNotifyTable& outTable ) { return outTable.loadFromResource( path, AnimNotifyHandlerRegistry::getDefault() ); }
            [[nodiscard]] static bool loadPhysicsAsset( string_view path, PhysicsAsset& outAsset ) { return outAsset.loadFromResource( path ); }

            static SharedAssetTable<SocketSet>& getSocketSetTable()
            {
                static SharedAssetTable<SocketSet> s_table;
                return s_table;
            }

            static SharedAssetTable<AnimNotifyTable>& getNotifyTable()
            {
                static SharedAssetTable<AnimNotifyTable> s_table;
                return s_table;
            }

            static SharedAssetTable<PhysicsAsset>& getPhysicsAssetTable()
            {
                static SharedAssetTable<PhysicsAsset> s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const SocketSet> SocketSetCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        return CharacterDataCacheInternal::getSocketSetTable().acquire( path, &CharacterDataCacheInternal::loadSocketSet );
    }

    uint32 SocketSetCache::getReloadCount()
    {
        return CharacterDataCacheInternal::getSocketSetTable().getReloadCount();
    }

    const SocketKindTable& SocketSetCache::getDefaultKinds()
    {
        return CharacterDataCacheInternal::loadDefaultKinds();
    }

    bool SocketSetCache::isCached( string_view relativePath ) const
    {
        return CharacterDataCacheInternal::getSocketSetTable().findLive( relativePath ) != nullptr;
    }

    void SocketSetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)CharacterDataCacheInternal::getSocketSetTable().reloadShared( relativePath, &CharacterDataCacheInternal::loadSocketSet );
    }

    size_t SocketSetCache::getCachedCount() const
    {
        return CharacterDataCacheInternal::getSocketSetTable().countLive();
    }

    void SocketSetCache::clear()
    {
        CharacterDataCacheInternal::getSocketSetTable().clear();
    }

    shared_ptr<const AnimNotifyTable> AnimNotifyTableCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        return CharacterDataCacheInternal::getNotifyTable().acquire( path, &CharacterDataCacheInternal::loadNotifyTable );
    }

    uint32 AnimNotifyTableCache::getReloadCount()
    {
        return CharacterDataCacheInternal::getNotifyTable().getReloadCount();
    }

    bool AnimNotifyTableCache::isCached( string_view relativePath ) const
    {
        return CharacterDataCacheInternal::getNotifyTable().findLive( relativePath ) != nullptr;
    }

    void AnimNotifyTableCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)CharacterDataCacheInternal::getNotifyTable().reloadShared( relativePath, &CharacterDataCacheInternal::loadNotifyTable );
    }

    size_t AnimNotifyTableCache::getCachedCount() const
    {
        return CharacterDataCacheInternal::getNotifyTable().countLive();
    }

    void AnimNotifyTableCache::clear()
    {
        CharacterDataCacheInternal::getNotifyTable().clear();
    }

    shared_ptr<const PhysicsAsset> PhysicsAssetCache::acquire( string_view path )
    {
        return CharacterDataCacheInternal::getPhysicsAssetTable().acquire( path, &CharacterDataCacheInternal::loadPhysicsAsset );
    }

    uint32 PhysicsAssetCache::getReloadCount()
    {
        return CharacterDataCacheInternal::getPhysicsAssetTable().getReloadCount();
    }

    bool PhysicsAssetCache::isCached( string_view relativePath ) const
    {
        return CharacterDataCacheInternal::getPhysicsAssetTable().findLive( relativePath ) != nullptr;
    }

    void PhysicsAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)CharacterDataCacheInternal::getPhysicsAssetTable().reloadShared( relativePath, &CharacterDataCacheInternal::loadPhysicsAsset );
    }

    size_t PhysicsAssetCache::getCachedCount() const
    {
        return CharacterDataCacheInternal::getPhysicsAssetTable().countLive();
    }

    void PhysicsAssetCache::clear()
    {
        CharacterDataCacheInternal::getPhysicsAssetTable().clear();
    }
} // namespace sw
