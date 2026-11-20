/**
 * @file GameFlagsOwnerComponent.h
 * @brief 씬의 월드 플래그(`GameFlags`)를 가진 컴포넌트의 베이스입니다. 아래 층(볼륨 · 캐릭터)이 판 상태 형식을 모른 채 열린 플래그를 찾습니다.
 * @details 판 상태(`SessionStateComponent`)는 게임플레이 층(맨 위)이라 바닥 층의 활성자 볼륨이 include 할 수 없다. 플래그 어휘는 바닥(Foundation/Condition)에 있으므로
 *          "플래그를 가진 것" 도 여기에 두고 위 층이 잇는다 — 언리얼에서 게임 코드가 인터페이스(`UInterface`)로 저장 객체를 찾는 자리를 컴포넌트 레지스트리 칸으로 둔다.
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class GameObjectManager;

    /**
     * @class GameFlagsOwnerComponent
     * @brief 열린 월드 플래그를 내는 컴포넌트입니다. 등록 때 컴포넌트 레지스트리의 자기 칸에 올라 씬을 훑지 않고 찾힙니다.
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Game Flags Owner", Tooltip = "Base of the components that own the scene's world flags" )
    class SW_GF_API GameFlagsOwnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        GameFlagsOwnerComponent()           = default;
        ~GameFlagsOwnerComponent() override = default;

        /** @brief 레지스트리의 `GameFlagsOwnerComponent` 칸에 오릅니다. 파생이 덮으면 이 함수를 부릅니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 열려 있어 읽고 쓸 수 있는 플래그입니다. 아직 열리지 않았으면 nullptr 입니다. 게임 스레드입니다. */
        [[nodiscard]] virtual GameFlags* findOpenFlags() = 0;

        /** @brief 씬 @p pManager 의 첫 열린 플래그(등록 순서)입니다 — 감시 값 `Session.Flag` 와 같은 판 상태입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static GameFlags* findFirstOpen( const GameObjectManager* pManager );
    };
} // namespace sw
