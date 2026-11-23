/**
 * @file AdventureAvatarComponent.h
 * @brief 액션 어드벤처 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`AdventureGameModeComponent`)의 입력 명령으로 넘기고, 지역 경계 · 봉화 · 야영지 · 플레이어를 디버그 그리기로 그립니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ActionAdventure`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 그리기는 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아,
 *          넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다(`ActionCombatAvatarComponent` 와 같은 한 프레임).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AdventureGameModeComponent;
    class GameObjectManager;

    /**
     * @class AdventureAvatarComponent
     * @brief `Move` 축 · 쓰기 액션을 프레임마다 디렉터 입력 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ActionAdventure", DisplayName = "Adventure Avatar",
             Tooltip = "Client side of an action adventure: sends input commands to the Adventure Game Mode on the same object and draws its regions, beacons, camps and player" )
    class SW_KIT_CLIENT_API AdventureAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        AdventureAvatarComponent();
        virtual ~AdventureAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 그립니다. */
        void updateAvatar( GameObjectManager& manager );
        void draw( const AdventureGameModeComponent& director ) const;

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action that lights a beacon or rests at a camp (required)" )
        hashed_string _useAction;

        TComponentHandle<AdventureGameModeComponent> _director;
        uint8                                        _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않고 그리기만 한다)
        uint8                                        _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                       _reserved      : 6;
    };
} // namespace sw
