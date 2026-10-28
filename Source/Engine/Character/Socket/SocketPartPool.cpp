#include "pch.h"

#include "Engine/Character/Socket/SocketPartPool.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /**
     * @brief `-gv_socketPartPool=0` — 소켓 부품 풀을 끕니다(`takePart` 는 늘 못 내주고 `storePart` 는 늘 받지 않는다 → 부르는 쪽이 새로 스폰하고 지운다). 풀 전 · 후 스폰 비용 비교용입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_socketPartPool, 1, "Socket part pool for appearance parts (0 = off: every part is spawned and destroyed, for before/after cost comparison)" );

    namespace
    {
        struct SocketPartPoolInternal
        {
            /** @brief 첫 씬이 아니라 지금 활성 씬의 풀을 읽는다 — 시나리오 감시 값이 부릅니다. */
            [[nodiscard]] static bool readTakeHitCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getTakeHitCount() );
                return true;
            }
            [[nodiscard]] static bool readTakeMissCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getTakeMissCount() );
                return true;
            }
            [[nodiscard]] static bool readStoredCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getStoredCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( SocketPartPool, DestroyPost )

    SW_AUTOMATION_WATCH( socketPartPoolTakeHits, "SocketPartPool.TakeHits", "Appearance parts the socket part pool handed out from rest (cumulative)", &SocketPartPoolInternal::readTakeHitCount );
    SW_AUTOMATION_WATCH( socketPartPoolTakeMisses, "SocketPartPool.TakeMisses", "Appearance part requests the socket part pool could not serve (the part was spawned new, cumulative)",
                         &SocketPartPoolInternal::readTakeMissCount );
    SW_AUTOMATION_WATCH( socketPartPoolStored, "SocketPartPool.Stored", "Appearance parts the socket part pool took in to rest (cumulative)", &SocketPartPoolInternal::readStoredCount );

    SocketPartPool::SocketPartPool( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapResting{}
        , _takeHitCount{ 0 }
        , _takeMissCount{ 0 }
        , _storedCount{ 0 }
    {
    }

    void SocketPartPool::update( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void SocketPartPool::onSceneCleared()
    {
        _mapResting.clear();
    }

    GameObject* SocketPartPool::takePart( const hashed_string& assetPath )
    {
        const auto found = gv_socketPartPool != 0 ? _mapResting.find( assetPath ) : _mapResting.end();
        if ( found != _mapResting.end() )
        {
            vector<GameObjectHandle>& listResting = found->second;
            while ( listResting.empty() == false )
            {
                GameObject* pObject = _pManager->resolveGameObject( listResting.back() );
                listResting.pop_back();
                if ( pObject == nullptr || pObject->isPendingDestroy() )
                    continue; // 쉬는 동안 누가 지웠다
                pObject->setActive( true );
                ++_takeHitCount;
                return pObject;
            }
        }
        ++_takeMissCount;
        return nullptr;
    }

    bool SocketPartPool::storePart( GameObject& object, const hashed_string& assetPath )
    {
        SocketBindingComponent* pBinding = object.getComponent<SocketBindingComponent>();
        const bool              bRefused = gv_socketPartPool == 0 || object.isPendingDestroy();
        if ( bRefused || pBinding == nullptr )
            return false;
        if ( pBinding->getState() == SocketBindingState::Bound )
            (void)pBinding->release( SocketReleaseMode::Animated ); // 씬 컴포넌트가 있어 붙었으므로 떼기는 실패하지 않는다
        if ( pBinding->getState() != SocketBindingState::ReleasedAnimated )
            return false; // 물리로 떨어졌거나 되돌아가는 중이다 — 쉬게 할 수 없다
        object.setActive( false );
        _mapResting[assetPath].push_back( object.getHandle() );
        ++_storedCount;
        return true;
    }

    uint32 SocketPartPool::getRestingCount( const hashed_string& assetPath ) const
    {
        const auto found = _mapResting.find( assetPath );
        if ( found == _mapResting.end() )
            return 0;
        uint32 count = 0;
        for ( const GameObjectHandle handle : found->second )
        {
            const GameObject* pObject = _pManager->resolveGameObject( handle );
            if ( pObject != nullptr && pObject->isPendingDestroy() == false )
                ++count;
        }
        return count;
    }
} // namespace sw
