#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameDataCache.h"

#include "Core/Memory/LifetimeScope.h"

#include "Engine/Resource/AssetManager.h"

namespace sw
{
    namespace
    {
        struct GameDataCacheInternal
        {
            /** @brief 캐시 목록과 지금 묶인 매니저입니다. 목록을 바꾸는 쪽(캐시 생성 · 서비스 묶기)이 여럿이라 잠근다. */
            /** @brief 캐시 하나와, 묶인 매니저에 올린 등록 구독입니다(묶인 매니저가 없으면 빈 구독). */
            struct CacheEntry
            {
                IAssetCache* _pCache{ nullptr };
                Subscription _registration{};
            };

            struct RegistryState
            {
                mutex              _mutex;
                vector<CacheEntry> _listCache;
                AssetManager*      _pAssetManager{ nullptr };
            };

            /**
             * @brief 레지스트리 상태의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(첫 캐시의 생성자 또는 서비스 묶기가 처음 만든다).
             * @details 값이 이 모듈(GameFramework.dll)에 있으면 모듈 언로드가 범위보다 먼저 이 객체를 소멸시키고 그때 값도 함께 소멸한다. 캐시가 먼저 사라지는 순서
             *          (`unregisterCache`)와 상태가 먼저 사라지는 순서 모두 `find()` 가 nullptr 이면 할 일이 없는 것으로 끝난다.
             */
            inline static ScopeOwned<RegistryState> s_state{};
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GameDataCacheRegistry::registerCache( IAssetCache* pCache )
    {
        if ( pCache == nullptr )
            return;
        GameDataCacheInternal::RegistryState* const pState = GameDataCacheInternal::s_state.getOrCreate();
        // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다(Debug 는 `getOrCreate` 가 단언한다) — 캐시를 올릴 레지스트리가 없다.
        if ( pState == nullptr )
            return;
        ScopedLock<mutex>                  lock{ pState->_mutex };
        GameDataCacheInternal::CacheEntry& entry = pState->_listCache.emplace_back();
        entry._pCache                            = pCache;
        if ( pState->_pAssetManager != nullptr )
            entry._registration = pState->_pAssetManager->registerAssetCache( pCache );
    }

    void GameDataCacheRegistry::unregisterCache( const IAssetCache* pCache )
    {
        GameDataCacheInternal::RegistryState* const pState = GameDataCacheInternal::s_state.find();
        if ( pState == nullptr )
            return; // 레지스트리가 먼저 사라졌다(프로세스 끝 · 엔진 종료) — 뺄 목록이 없다
        ScopedLock<mutex> lock{ pState->_mutex };
        for ( size_t index = 0; index < pState->_listCache.size(); ++index )
        {
            if ( pState->_listCache[index]._pCache == pCache ) // 지우면 등록 구독도 함께 풀린다
            {
                pState->_listCache.erase( pState->_listCache.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void GameDataCacheRegistry::attach( AssetManager* pAssetManager )
    {
        // 풀 때(nullptr)는 만들지 않고 찾는다 — 레지스트리가 없으면 풀 매니저도 없다.
        GameDataCacheInternal::RegistryState* const pState = pAssetManager != nullptr ? GameDataCacheInternal::s_state.getOrCreate() : GameDataCacheInternal::s_state.find();
        if ( pState == nullptr )
            return;
        ScopedLock<mutex> lock{ pState->_mutex };
        if ( pState->_pAssetManager == pAssetManager )
            return;
        pState->_pAssetManager = pAssetManager;
        // 대입이 이전 매니저의 등록을 먼저 푼다. 풀 때(nullptr)는 빈 구독을 넣는다.
        for ( GameDataCacheInternal::CacheEntry& entry : pState->_listCache )
        {
            entry._registration = pAssetManager != nullptr ? pAssetManager->registerAssetCache( entry._pCache ) : Subscription{};
        }
    }
} // namespace sw
