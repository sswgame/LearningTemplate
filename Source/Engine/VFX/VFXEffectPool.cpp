#include "pch.h"

#include "Engine/VFX/VFXEffectPool.h"

#include "Core/Foundation/BuildInfo.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/VFX/VFXComponent.h"

namespace sw
{
    namespace
    {
        struct VFXEffectPoolInternal
        {
            /** @brief 풀 오브젝트의 이름입니다(씬 목록에서 무엇인지 보이게). */
            static constexpr const utf8* kObjectName = "VFXPooledEffect";

            /** @brief 감시 값 `VFX.PoolPlayed` 입니다. 효과 `key`(경로)를 이 씬에서 튼 수(누적)입니다. */
            [[nodiscard]] static bool readPlayed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<VFXEffectPool>().getPlayCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 감시 값 `VFX.PoolSize` 입니다. 효과 `key`(경로)의 풀이 만든 오브젝트 수입니다. */
            [[nodiscard]] static bool readPoolSize( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<VFXEffectPool>().getPoolSize( hashed_string( query._key ) ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( vfxPoolPlayed, "VFX.PoolPlayed", "Times the pooled one-shot effect at path key played in this scene (cumulative)", &VFXEffectPoolInternal::readPlayed, false,
                               true );
    SW_AUTOMATION_QUERY_WATCH( vfxPoolSize, "VFX.PoolSize", "Objects the pool of the one-shot effect at path key has made", &VFXEffectPoolInternal::readPoolSize, false, true );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( VFXEffectPool, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    VFXEffectPool::VFXEffectPool( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapPool{}
    {
    }

    void VFXEffectPool::onSceneCleared()
    {
        _mapPool.clear();
    }

    bool VFXEffectPool::play( const string& effectPath, const float3& position, uint32 poolLimit ) const
    {
        if ( build::kWithClientCode == false || effectPath.empty() || poolLimit == 0 )
            return false;
        // 오브젝트를 만들 수 있는 때(틱 밖 · 틱 뒤 게임 스레드)에 돈다 — 서비스는 매니저에서 다시 찾는다(그사이 씬이 비었어도 매니저마다 하나다).
        _pManager->executeOrDeferPostTickOnScene( [effectPath, position, poolLimit]( GameObjectManager& manager )
        { manager.getSubsystem<VFXEffectPool>().playNow( effectPath, position, poolLimit ); } );
        return true;
    }

    void VFXEffectPool::playNow( const string& effectPath, const float3& position, uint32 poolLimit )
    {
        EffectPool&   pool    = _mapPool[hashed_string( effectPath )];
        VFXComponent* pEffect = acquireEffect( pool, effectPath, poolLimit );
        if ( pEffect == nullptr )
            return;
        pEffect->setWorldPosition( position );
        pEffect->play();
        ++pool._playCount;
    }

    uint32 VFXEffectPool::getPlayCount( const hashed_string& effectPath ) const
    {
        const auto found = _mapPool.find( effectPath );
        return found != _mapPool.end() ? found->second._playCount : 0;
    }

    uint32 VFXEffectPool::getPoolSize( const hashed_string& effectPath ) const
    {
        const auto found = _mapPool.find( effectPath );
        return found != _mapPool.end() ? static_cast<uint32>( found->second._listEffect.size() ) : 0;
    }

    VFXComponent* VFXEffectPool::acquireEffect( EffectPool& pool, const string& effectPath, uint32 poolLimit )
    {
        for ( const TComponentHandle<VFXComponent>& handle : pool._listEffect )
        {
            VFXComponent* pEffect = _pManager->resolveComponent( handle );
            if ( pEffect != nullptr && pEffect->isPlaying() == false )
                return pEffect;
        }
        if ( pool._listEffect.size() < poolLimit )
        {
            VFXComponent* pEffect = createEffect( effectPath );
            if ( pEffect != nullptr )
                pool._listEffect.push_back( TComponentHandle<VFXComponent>{ pEffect } );
            return pEffect;
        }
        // 모두 재생 중이고 상한이다 — 가장 오래 전에 튼 칸을 새 자리에서 다시 튼다.
        const uint32  slot    = pool._nextSlot % static_cast<uint32>( pool._listEffect.size() );
        VFXComponent* pEffect = _pManager->resolveComponent( pool._listEffect[slot] );
        pool._nextSlot        = slot + 1;
        if ( pEffect == nullptr )
        {
            pEffect                = createEffect( effectPath ); // 누가 지운 칸 — 그 자리에 다시 만든다
            pool._listEffect[slot] = TComponentHandle<VFXComponent>{ pEffect };
        }
        return pEffect;
    }

    VFXComponent* VFXEffectPool::createEffect( const string& effectPath )
    {
        GameObject* pObject = _pManager->createGameObject( hashed_string( VFXEffectPoolInternal::kObjectName ) );
        if ( pObject == nullptr )
            return nullptr;
        pObject->setTransient( true ); // 효과를 튼 쪽이 다시 세우는 파생 — 세이브 · 상태 해시에 넣지 않는다(`GameObject::setTransient`)
        VFXComponent* pEffect = pObject->addComponent<VFXComponent>();
        if ( pEffect != nullptr )
            pEffect->setEffectPath( effectPath ); // 효과 정의는 공유 캐시의 한 벌이라 풀이 커져도 파일은 한 번 읽는다
        return pEffect;
    }
} // namespace sw
