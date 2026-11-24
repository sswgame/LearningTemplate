#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessSystem.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Service/FrameSystemUtil.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessComponent.h"

namespace sw
{
    CharacterWetnessSystem::CharacterWetnessSystem()
        : _listHandleScratch{}
    {
    }

    CharacterWetnessSystem* CharacterWetnessSystem::find( const GameObjectManager& manager )
    {
        return FrameSystemUtil::find<CharacterWetnessSystem>( manager );
    }

    CharacterWetnessSystem& CharacterWetnessSystem::getOrCreate( GameObjectManager& manager )
    {
        return FrameSystemUtil::getOrCreate<CharacterWetnessSystem>( manager );
    }

    void CharacterWetnessSystem::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 노출 컴포넌트가 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        if ( manager.getComponentRegistry().getAll<CharacterWetnessComponent>().empty() )
            FrameSystemUtil::remove<CharacterWetnessSystem>( manager );
    }

    void CharacterWetnessSystem::runMain( GameObjectManager& manager, float32 /*deltaTime*/ )
    {
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 마지막 노출 컴포넌트가 떠나 시스템이 떨어졌을 수 있다.
        // 시간은 컴포넌트 틱이 쌓은 것을 쓴다 — 틱이 돌지 않은(편집 모드 · 꺼진) 컴포넌트는 바뀌지 않는다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager]()
        {
            CharacterWetnessSystem* pSystem = find( *pManager );
            if ( pSystem != nullptr )
                pSystem->runFramePass( *pManager );
        } );
    }

    SceneFrameAccess CharacterWetnessSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void CharacterWetnessSystem::runFramePass( GameObjectManager& manager )
    {
        manager.forEachActiveRegistered<CharacterWetnessComponent>( _listHandleScratch, [&manager]( CharacterWetnessComponent& exposure )
        { exposure.applyExposure( manager ); } );
    }
} // namespace sw
