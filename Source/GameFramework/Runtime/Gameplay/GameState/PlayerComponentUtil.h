/**
 * @file PlayerComponentUtil.h
 * @brief 플레이어 상태 컴포넌트(`WalletComponent` · `InventoryComponent` · `QuestLogComponent` · `ReputationComponent`)의 찾기 · 열기입니다. 늦은 복원은 `DeferredStateRestore`.
 * @details 상태 범위 셋(언리얼 `AGameState` · `APlayerState` · 매치 상태) 가운데 플레이어 몫은 기능마다 컴포넌트 하나로 조종자 오브젝트에 함께 붙는다(Lyra 의
 *          `ULyraInventoryManagerComponent` · `ULyraQuickBarComponent` 처럼 기능마다). 조종자가 없는 디렉터 전용 씬(옛 게임 맵)은 디렉터 오브젝트에 둔다.
 *          기능이 없으면 컴포넌트가 없다 — "슬롯 0 이면 가방 없음" 같은 특수 값을 두지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/DeferredStateRestore.h"

namespace sw
{
    /**
     * @struct PlayerComponentUtil
     * @brief 플레이어 상태 컴포넌트 찾기 · 열기입니다. 컴포넌트마다 같은 찾기를 되풀이하지 않게 형식 인자로 받는다.
     * @details - **디렉터**: 같은 오브젝트에서 `findOnOwner` · `openOnOwner`(디렉터의 `startGame`).
     *          - **폰**: `PawnComponent::findControllerObject`(폰 → 쥔 조종자 → 그 오브젝트)의 결과에서 `getComponent<T>` — 형제 `PlayerStateComponent` 와 같은 길.
     *          - **씬 하나를 읽는 도구**(감시 값 · 치트 · 디버그 HUD · 퀘스트 목표 표시): `findOpen` — 열린 것이 둘 이상이면 오류로 멈춘다(어느 플레이어인지 등록 순서로 고르지 않는다).
     *            컴포넌트는 `onRegister` 에서 레지스트리에 올라 있어야 한다.
     */
    struct SW_GF_API PlayerComponentUtil
    {
        /** @brief @p component 와 같은 오브젝트의 @p T 입니다. 없으면 nullptr 입니다. */
        template <typename T>
        [[nodiscard]] static T* findOnOwner( const Component& component )
        {
            GameObject* const pOwner = component.getOwner();
            return pOwner != nullptr ? pOwner->getComponent<T>() : nullptr;
        }

        /** @brief @p component 와 같은 오브젝트의 @p T 입니다. 판을 열 디렉터가 반드시 쓰는 것이라 없으면 오류를 남기고 nullptr 입니다. 열기가 없는 지갑은 이것으로 찾는다. */
        template <typename T>
        [[nodiscard]] static T* requireOnOwner( const Component& component )
        {
            T* const pFound = findOnOwner<T>( component );
            if ( pFound == nullptr )
                SW_LOG_ERROR_ID( "PlayerComponent.MissingOnOwner", "no %# next to the game mode on this object - the game cannot start", T::StaticType()->_name.c_str() );
            return pFound;
        }

        /** @brief @p component 와 같은 오브젝트의 @p T 를 @p args 로 엽니다(`T::initialize`). 없거나 거절되면 오류를 남기고 nullptr 입니다. */
        template <typename T, typename... TArgs>
        [[nodiscard]] static T* openOnOwner( const Component& component, const TArgs&... args )
        {
            T* const pFound = requireOnOwner<T>( component );
            if ( pFound == nullptr )
                return nullptr;
            return pFound->initialize( args... ) != GameStateInitResult::Refused ? pFound : nullptr; // 거절한 까닭은 `initialize` 가 남겼다
        }

        /**
         * @brief 씬 @p pManager 의 열린(`isOpen`) @p T 입니다. 없으면 nullptr, 둘 이상이면 오류를 남기고 nullptr 입니다.
         * @details 매니저 형식은 형식 인자로 받아 이 헤더가 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다. 레지스트리를 읽어 씬을 훑지 않는다.
         */
        template <typename T, typename TManager>
        [[nodiscard]] static T* findOpen( const TManager* pManager )
        {
            if ( pManager == nullptr )
                return nullptr;
            T* pFound = nullptr;
            for ( T* pComponent : pManager->getComponentRegistry().template getAll<T>() )
            {
                if ( pComponent->isOpen() == false )
                    continue;
                if ( pFound != nullptr )
                {
                    SW_LOG_ERROR_ID( "PlayerComponent.SeveralOpen", "the scene has more than one open %# ('%#', '%#') - a single-player reader cannot pick one", T::StaticType()->_name.c_str(),
                                     pFound->getOwner() != nullptr ? pFound->getOwner()->getName().c_str() : "?",
                                     pComponent->getOwner() != nullptr ? pComponent->getOwner()->getName().c_str() : "?" );
                    return nullptr;
                }
                pFound = pComponent;
            }
            return pFound;
        }
    };
} // namespace sw
