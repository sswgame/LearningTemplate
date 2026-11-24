/**
 * @file FightingViewComponent.h
 * @brief 3D 격투 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`FightingGameModeComponent`)의 입력 명령으로 넘기고, 두 캐릭터의 몸 오브젝트와 HUD 를 디렉터의 대전대로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Fighting`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getMatch` · `getFighterPosition` 등)만 쓰고
 *          대전(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 컨트롤러도 같은 오브젝트에 붙인다.
 *          몸은 씬에 놓인 오브젝트(원시 도형)다 — 자세에 맞춰 높이만 바꾼다. 명령 · 모습은 틱 뒤 게임 스레드에서, `PostUpdate` 라 디렉터 걸음 뒤에 돈다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class FightingGameModeComponent;
    class FightingHUDViewModel;
    class GameObjectManager;

    /**
     * @class FightingViewComponent
     * @brief `Move` 축을 넘패드 방향으로, 버튼 액션 누름을 카탈로그 버튼 비트로 바꿔 디렉터에 넘기고 두 몸 · HUD 를 맞춥니다.
     */
    REFLECT( Category = "Fighting", DisplayName = "Fighting View",
             Tooltip = "Client side of a Fighting match: sends input commands to the Fighting Game Mode on the same object and shows both fighters and the HUD" )
    class SW_KIT_CLIENT_API FightingViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        FightingViewComponent();
        virtual ~FightingViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 입력 · 명령 · 모습을 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 몸 · HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager );
        void submitInput( FightingGameModeComponent& director ) const;
        /** @brief 캐릭터 @p player 의 몸 오브젝트를 대전의 자리 · 자세 높이에 둡니다. */
        void updateBody( GameObjectManager& manager, const FightingGameModeComponent& director, int32 player, GameObjectHandle body ) const;
        void updateHUD( const FightingGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action turned into a numpad direction (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Button Actions", Tooltip = "InputMap actions of the attack buttons, in the catalog's button order (required)" )
        vector<hashed_string> _listButtonAction;
        PROPERTY( Category = "Input", DisplayName = "Dead Zone", Tooltip = "An axis below this is neutral", Min = 0.0, Max = 1.0 )
        float32 _deadZone;
        PROPERTY( Category = "Scene", DisplayName = "Player Body", Tooltip = "Object shown as player 0" )
        GameObjectHandle _playerBody;
        PROPERTY( Category = "Scene", DisplayName = "Opponent Body", Tooltip = "Object shown as player 1" )
        GameObjectHandle _opponentBody;

        TComponentHandle<FightingGameModeComponent> _director;
        unique_ptr<FightingHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        uint8                                       _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다)
        uint8                                       _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                      _reserved      : 6;
    };
} // namespace sw
