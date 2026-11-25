/**
 * @file WesternRiderViewComponent.h
 * @brief 서부극 키트의 클라이언트 쪽 — 카메라 리그 초점을 기수에 두고, 디렉터(`WesternGameModeComponent`)와 탄 말을 읽어 HUD 뷰모델에 씁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_OpenWorldWestern`, 전용 서버는 짓지 않는다). 디렉터의 읽기 API(`findPlayerPawn` · `findOutlaw` · `findWesternCatalog`)만 쓰고 규칙을 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 하는 일은 모두 틱 뒤 게임 스레드에서 한다.
 *          디렉터와 같은 `PrePhysics` 그룹이라 카메라 디렉터(`CameraDirectorComponent`, `PostPhysics` 틱 뒤)가 이번 프레임의 초점을 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class PawnComponent;
    class WesternGameModeComponent;
    class WesternHUDViewModel;

    /**
     * @class WesternRiderViewComponent
     * @brief 기수의 화면입니다. HUD 는 `WesternHUDViewModel` 을 같은 오브젝트의 HUD 컨트롤러에 건다 — 탑승 상태 글(`_rideStatus`), 말 스태미나(`_stamina` · `_staminaRatio`), 무법자 글(`_outlawStatus`).
     */
    REFLECT( Category = "Western", DisplayName = "Western Rider View",
             Tooltip = "Client side of a western game: keeps the camera rig on the rider and writes the ride and outlaw lines of the HUD from the Western Game Mode on the same object" )
    class SW_KIT_CLIENT_API WesternRiderViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;

        WesternRiderViewComponent();
        virtual ~WesternRiderViewComponent() override;

        /** @brief @p director 의 무법자 글입니다. 아무 일도 없으면(시작 명예 · 수배 없음 · 가죽 없음 · 코어 멀쩡) 비어 있어 HUD 에 아무것도 그리지 않습니다. */
        string formatOutlawStatus( const WesternGameModeComponent& director ) const;

    private:
        /** @brief 틱 뒤 게임 스레드: 판이 열렸으면 카메라 초점과 HUD 를 씁니다. */
        void updateView( GameObjectManager& manager, const WesternGameModeComponent& director );
        /** @brief 카메라 리그 초점을 기수(탔으면 기수가 붙은 말 위)의 발 아래에 둡니다. */
        void updateCameraFocus( GameObjectManager& manager, const PawnComponent& playerPawn );
        /** @brief 같은 오브젝트의 HUD 에 탑승 상태 한 줄과 말 스태미나, 무법자 글을 씁니다. */
        void updateHUD( const WesternGameModeComponent& director, const PawnComponent& playerPawn );

    private:
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent that follows the rider" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "HUD", DisplayName = "Low Core Warning", Min = 0.0, Max = 100.0, Tooltip = "The HUD warns when a core falls below this" )
        float32 _lowCoreWarning;

        unique_ptr<WesternHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
    };
} // namespace sw
