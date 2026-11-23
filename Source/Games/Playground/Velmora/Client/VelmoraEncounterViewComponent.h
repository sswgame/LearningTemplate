/**
 * @file VelmoraEncounterViewComponent.h
 * @brief 벨모라 지역 전투의 클라이언트 쪽 — 공격 · 대시 액션을 전투(`VelmoraEncounterGameModeComponent`)의 입력 명령으로 넘기고 룸(적 · 탄 · 무적)을 디버그 그리기로 그립니다.
 * @details 클라이언트 전용 소스다(`Client/` — 전용 서버는 짓지 않는다). 전투 컴포넌트는 같은 오브젝트에 먼저 붙인다. 걷기 · 쓰기 입력과 지역 · 플레이어 그리기는 키트의 `AdventureAvatarComponent` 다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class VelmoraEncounterGameModeComponent;

    /**
     * @class VelmoraEncounterViewComponent
     * @brief 공격 · 대시 누름을 프레임마다 전투 입력 명령으로 넘기고 열린 룸을 그립니다.
     */
    REFLECT( Category = "Velmora", DisplayName = "Velmora Encounter View",
             Tooltip = "Client side of the Velmora region fights: sends attack and dash to the Velmora Encounter Game Mode on the same object and draws its room" )
    class VelmoraEncounterViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        VelmoraEncounterViewComponent();
        virtual ~VelmoraEncounterViewComponent() override;

        /** @brief 같은 오브젝트의 전투 컴포넌트를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 룸을 그립니다. */
        void updateView( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that strikes (required)" )
        hashed_string _attackAction;
        PROPERTY( Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that dashes (required)" )
        hashed_string _dashAction;

        TComponentHandle<VelmoraEncounterGameModeComponent> _encounter;
        uint8                                               _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않고 그리기만 한다)
        uint8                                               _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                              _reserved      : 6;
    };
} // namespace sw
