/**
 * @file PrefabPoolComponentSpawner.h
 * @brief 풀(`PrefabPool`) 단위 테스트의 인스턴스 — 프리팹 대신 같은 컴포넌트를 코드로 단 오브젝트를 만드는 풀입니다(nogpu, 에셋 없음).
 * @details 풀 되돌리기 테스트(키트 · 게임)마다 "풀을 만들고 스포너 람다에서 오브젝트를 만들어 컴포넌트를 단다" 를 따로 적었다. 한 곳에 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"

namespace sw
{
    /**
     * @brief @p TComponent 들을 적은 순서로 단 오브젝트를 스폰하는 풀을 만듭니다. 컴포넌트가 없으면 빈 오브젝트입니다.
     * @code
     *     PrefabPool pool = PrefabPoolComponentSpawner<MeshComponent, RTSUnitViewComponent>::makePool( 4 );
     * @endcode
     */
    template <typename... TComponent>
    struct PrefabPoolComponentSpawner
    {
        /** @brief 컴포넌트를 단 뒤 스폰 상태를 손보는 함수입니다 — 프리팹이라면 파일에 적었을 값(폰의 자동 빙의 등)을 정한다. */
        using ConfigureFunction = void ( * )( GameObject& object );

        static PrefabPool makePool( uint32 maxIdleCount, ConfigureFunction pConfigure = nullptr )
        {
            PrefabPool pool{ string{}, maxIdleCount, SpawnLifetime::Persistent };
            pool.setSpawner( SW_DELEGATE_LAMBDA( PrefabPool::SpawnDelegate, [pConfigure]( GameObjectManager& manager, const utf8* pName )
            {
                GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
                ( (void)pObject->addComponent<TComponent>(), ... );
                if ( pConfigure != nullptr )
                    pConfigure( *pObject );
                return pObject;
            } ) );
            return pool;
        }
    };
} // namespace sw
