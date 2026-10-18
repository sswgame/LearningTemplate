/**
 * @file WildWestDirectorComponent.h
 * @brief 서부 평원 승마 디렉터 — 서부극 데이터(`WesternCatalog`)를 열고, 카메라가 기수를 따라가게 하고, 휘파람으로 말을 부르고, HUD 한 줄(탑승 상태 · 걸음새 · 속도 · 스태미나)을 씁니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"

namespace sw
{
    class PawnComponent;

    /**
     * @class WildWestDirectorComponent
     * @brief 레드 데드 리뎀션 2 · 젤다 BotW 식 오픈월드 승마 한 판입니다. 걷는 기수(`_player`)가 가까운 말에 상호작용 버튼으로 올라타고(`MountInteractionComponent`),
     *        말을 몰고(`MountMovementComponent` · `WesternHorseMountComponent`), 같은 버튼으로 내립니다(`VehicleExitComponent`). 이 디렉터는 그 흐름을 돌리지 않고 둘레만 맡습니다.
     * @details 타기 · 몰기 · 내리기는 키트와 기반 컴포넌트가 하고, 말 한 마리의 상태(`WesternHorse`)는 말 오브젝트의 `WildWestHorseComponent` 가 가진다.
     *          이 디렉터가 하는 일은 넷이다. 판을 열 때 서부극 카탈로그를 읽는다(경로는 PROPERTY, 없으면 판을 열지 않는다). 매 프레임 카메라 리그 초점을 기수에 둔다.
     *          기수가 걷는 동안 휘파람 버튼이 발동하면 그 기수를 주인으로 둔 말 AI(`HorseFollowAIController`)를 모두 부른다. 틱 뒤에 같은 오브젝트의 HUD 에 한 줄을 쓴다.
     *          판 상태는 없다(말 상태는 말 컴포넌트가 스냅숏에 싣는다). 자동 플레이(`-gv_wildWestAutoPlay=1`)는 기수를 가장 가까운 말에 태운다.
     *          HUD 는 게임의 `WildWestHUDViewModel` 을 HUD 컨트롤러에 건다 — 탑승 상태 글(`_rideStatus`), 말 스태미나(`_stamina` · `_staminaRatio`)이고 문서(`ui/hud.ui.xml`)가 그렇게 잇는다.
     */
    REFLECT( Category = "WildWest", DisplayName = "Wild West Director", Tooltip = "Opens the western catalog, follows the rider with the camera, whistles horses and writes the HUD line" )
    class WildWestDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "WWDR" );
        static constexpr uint32 kStateVersion = 1;

        WildWestDirectorComponent();
        virtual ~WildWestDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        /** @brief 판이 열렸으면 읽은 서부극 카탈로그이고, 아니면 nullptr 입니다(말 컴포넌트가 품종을 찾는다). */
        const WesternCatalog*   findWesternCatalog() const { return isStarted() ? &_westernCatalog : nullptr; }
        const GameObjectHandle& getPlayerObject() const { return _player; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 기수 폰입니다. 없으면 nullptr 입니다. */
        const PawnComponent* findPlayerPawn() const;
        /** @brief 카메라 리그 초점을 기수(탔으면 기수가 붙은 말 위)의 발 아래에 둡니다. */
        void updateCameraFocus( const PawnComponent& playerPawn );
        /** @brief 이 기수를 주인으로 둔 말 AI 를 모두 부릅니다. 틱 뒤에 돕니다. */
        void whistleHorses();
        /** @brief 같은 오브젝트의 HUD 에 탑승 상태 한 줄과 말 스태미나를 씁니다. 틱 뒤에 돕니다. */
        void updateHUD();
        /** @brief 자동 플레이: 기수를 가장 가까운 빈 좌석에 태웁니다. 틱 뒤에 돕니다. */
        void mountNearestHorse();

    private:
        PROPERTY( Category = "Data", DisplayName = "Western Data", AssetPath, Tooltip = "WesternCatalog XML (horse breeds, bond levels, survival)" )
        string _westernDataPath;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Rider object (pawn with Mount Interaction)" )
        GameObjectHandle _player;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent that follows the rider" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Control", DisplayName = "Whistle Button", Tooltip = "Intent button on the rider pawn that calls the horses" )
        hashed_string _whistleButton;

        WesternCatalog         _westernCatalog;
        uint8                  _bAutoMountRequested : 1; ///< 자동 플레이가 기수를 태우라고 한 번 청했다
        [[maybe_unused]] uint8 _reserved            : 7;
    };
} // namespace sw
