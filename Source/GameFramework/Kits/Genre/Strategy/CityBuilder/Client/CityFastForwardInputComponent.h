/**
 * @file CityFastForwardInputComponent.h
 * @brief 도시 건설 키트의 클라이언트 쪽 빨리 감기 입력 — 액션을 누르고 있는지를 공유 시계의 도시 디렉터(`CityGameModeComponent`)에 넘깁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CityBuilder`, 전용 서버는 짓지 않는다). 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓴다.
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
    class CityGameModeComponent;
    class GameObjectManager;

    /**
     * @class CityFastForwardInputComponent
     * @brief 빨리 감기 액션 하나를 디렉터 명령(`submitFastForward`)으로 바꿉니다. 멈춘 프레임에는 넘기지 않습니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Fast Forward Input", Tooltip = "Sends the fast forward input to the City Game Mode on the same object" )
    class SW_KIT_CLIENT_API CityFastForwardInputComponent : public Component
    {
    public:
        REFLECT_BODY();

        CityFastForwardInputComponent();
        virtual ~CityFastForwardInputComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 입력 명령을 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 빨리 감기 입력을 넘깁니다. */
        void updateInput( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Fast Forward Action", Tooltip = "InputMap action held to fast-forward the city (required)" )
        hashed_string _fastForwardAction;

        TComponentHandle<CityGameModeComponent> _director;
        uint8                                   _bInputLive   : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다)
        uint8                                   _bActionValid : 1; ///< 액션 이름이 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                  _reserved     : 6;
    };
} // namespace sw
