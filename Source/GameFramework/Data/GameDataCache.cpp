#include "pch.h"

#include "GameFramework/Data/GameDataCache.h"

#include "Engine/Resource/AssetManager.h"

namespace sw
{
    namespace
    {
        struct GameDataCacheInternal
        {
            /** @brief 캐시 목록과 지금 묶인 매니저입니다. 목록을 바꾸는 쪽(캐시 생성 · 서비스 묶기)이 여럿이라 잠근다. */
            struct RegistryState
            {
                mutex                _mutex;
                vector<IAssetCache*> _listCache;
                AssetManager*        _pAssetManager{ nullptr };
            };

            /** @brief 모듈에 하나인 상태입니다. 캐시(함수 정적)보다 먼저 생겨 나중에 사라진다 — 첫 캐시의 생성자가 처음 부른다. */
            static RegistryState& getState()
            {
                static RegistryState s_state;
                return s_state;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GameDataCacheRegistry::addCache( IAssetCache* pCache )
    {
        if ( pCache == nullptr )
            return;
        GameDataCacheInternal::RegistryState& state = GameDataCacheInternal::getState();
        std::scoped_lock<mutex>               lock{ state._mutex };
        state._listCache.push_back( pCache );
        if ( state._pAssetManager != nullptr )
            state._pAssetManager->registerAssetCache( pCache );
    }

    void GameDataCacheRegistry::removeCache( const IAssetCache* pCache )
    {
        GameDataCacheInternal::RegistryState& state = GameDataCacheInternal::getState();
        std::scoped_lock<mutex>               lock{ state._mutex };
        for ( size_t index = 0; index < state._listCache.size(); ++index )
        {
            if ( state._listCache[index] == pCache )
            {
                state._listCache.erase( state._listCache.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void GameDataCacheRegistry::attach( AssetManager* pAssetManager )
    {
        GameDataCacheInternal::RegistryState& state = GameDataCacheInternal::getState();
        std::scoped_lock<mutex>               lock{ state._mutex };
        if ( state._pAssetManager == pAssetManager )
            return;
        if ( state._pAssetManager != nullptr )
        {
            for ( const IAssetCache* pCache : state._listCache )
                state._pAssetManager->unregisterAssetCache( pCache );
        }
        state._pAssetManager = pAssetManager;
        if ( pAssetManager != nullptr )
        {
            for ( IAssetCache* pCache : state._listCache )
                pAssetManager->registerAssetCache( pCache );
        }
    }
} // namespace sw
