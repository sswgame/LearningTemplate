/**
 * @file IGameFlagsOwner.h
 * @brief 씬의 월드 플래그(`GameFlags`)를 가진 컴포넌트가 내보내는 리플렉션 인터페이스입니다. 아래 층(볼륨 · 캐릭터)이 판 상태 형식을 모른 채 열린 플래그를 찾습니다.
 * @details 판 상태(`GameStateComponent`)는 게임플레이 층(맨 위)이라 바닥 층의 트리거가 include 할 수 없다. 플래그 어휘는 바닥(Foundation/Condition)에 있으므로
 *          "플래그를 가진 것" 도 여기에 두고 위 층이 포트 멤버로 내보낸다 — 언리얼에서 게임 코드가 인터페이스(`UInterface`)로 저장 객체를 찾는 자리다.
 *          예전에는 리플렉션 베이스(`Component` 파생)가 등록 때 레지스트리 칸에 손으로 올랐다 — 지금은 형식의 인터페이스 표가 레지스트리 인터페이스 칸에 저절로 올린다.
 */
#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class GameObjectManager;

    /**
     * @class IGameFlagsOwner
     * @brief 열린 월드 플래그를 내는 계약입니다. 내보내는 컴포넌트(`REFLECT_INTERFACE( IGameFlagsOwner, _gameFlagsOwnerPort )`)는 씬을 훑지 않고 레지스트리에서 찾힌다
     *        (`ComponentRegistry::getAllInterfaces<IGameFlagsOwner>()`).
     */
    REFLECT( Interface, Category = "Framework", DisplayName = "Game Flags Owner", Tooltip = "Contract of the components that own the scene's world flags" )
    struct SW_GF_API IGameFlagsOwner
    {
        REFLECT_BODY();

        virtual ~IGameFlagsOwner() = default;

        /** @brief 열려 있어 읽고 쓸 수 있는 플래그입니다. 아직 열리지 않았으면 nullptr 입니다. 게임 스레드입니다. */
        [[nodiscard]] virtual GameFlags* findOpenFlags() = 0;

        /**
         * @brief 씬 @p pManager 의 열린 월드 플래그(씬에 하나 — 감시 값 `Session.Flag` 와 같은 월드 상태)입니다. 없으면 nullptr 입니다.
         * @details 열린 소유자가 둘 이상이면 오류를 남기고 nullptr 이다 — 어느 플래그에 적을지를 등록 순서로 고르지 않는다(하위 레벨이 둘째 소유자를 연 경우).
         */
        [[nodiscard]] static GameFlags* findOpen( const GameObjectManager* pManager );
    };
} // namespace sw
