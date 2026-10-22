#include "pch.h"

#include "Engine/Character/Socket/SocketPartPool.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_SCENE_SERVICE( SocketPartPool, DestroyPost )

    SocketPartPool::SocketPartPool( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapResting{}
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
        const auto found = _mapResting.find( assetPath );
        if ( found == _mapResting.end() )
            return nullptr;
        vector<GameObjectHandle>& listResting = found->second;
        while ( listResting.empty() == false )
        {
            GameObject* pObject = _pManager->resolveGameObject( listResting.back() );
            listResting.pop_back();
            if ( pObject == nullptr || pObject->isPendingDestroy() )
                continue; // 쉬는 동안 누가 지웠다
            pObject->setActive( true );
            return pObject;
        }
        return nullptr;
    }

    bool SocketPartPool::storePart( GameObject& object, const hashed_string& assetPath )
    {
        SocketBindingComponent* pBinding = object.getComponent<SocketBindingComponent>();
        if ( object.isPendingDestroy() || pBinding == nullptr )
            return false;
        if ( pBinding->getState() == SocketBindingState::Bound )
            (void)pBinding->release( SocketReleaseMode::Animated ); // 씬 컴포넌트가 있어 붙었으므로 떼기는 실패하지 않는다
        if ( pBinding->getState() != SocketBindingState::ReleasedAnimated )
            return false; // 물리로 떨어졌거나 되돌아가는 중이다 — 쉬게 할 수 없다
        object.setActive( false );
        _mapResting[assetPath].push_back( object.getHandle() );
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
