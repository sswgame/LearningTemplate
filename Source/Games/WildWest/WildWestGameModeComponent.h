/**
 * @file WildWestGameModeComponent.h
 * @brief 서부 평원 디렉터 — 서부극 데이터(`WesternCatalog`)와 판 상태를 열고, 플레이어의 명예 · 법 · 생존 · 사냥(`WesternOutlaw`)을 돌리고, 카메라가 기수를 따라가게 하고, 휘파람으로 말을 부르고, HUD 를 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternOutlaw.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    class PawnComponent;

    /**
     * @class WildWestGameModeComponent
     * @brief 레드 데드 리뎀션 2 · 젤다 BotW 식 오픈월드 승마 한 판입니다. 걷는 기수(`_player`)가 가까운 말에 상호작용 버튼으로 올라타고(`MountInteractionComponent`),
     *        말을 몰고(`MountMovementComponent` · `WesternHorseMountComponent`), 같은 버튼으로 내립니다(`VehicleExitComponent`). 이 디렉터는 그 흐름을 돌리지 않고 둘레만 맡습니다.
     * @details 타기 · 몰기 · 내리기는 키트와 기반 컴포넌트가 하고, 말 한 마리의 상태(`WesternHorse`)는 말 오브젝트의 `WesternHorseComponent`(키트) 가 가진다.
     *          이 디렉터가 하는 일은 다섯이다. 판을 열 때 서부극 카탈로그 · 날씨를 읽고 같은 오브젝트의 판 상태(`SessionStateComponent`, 맨 앞)를 연다(없으면 판을 열지 않는다). 매 프레임 카메라 리그 초점을 기수에 둔다.
     *          기수가 걷는 동안 휘파람 버튼이 발동하면 그 기수를 주인으로 둔 말 AI(`HorseFollowAIController`)를 모두 부른다. 행동 버튼이 발동하면 곁의 자리(`WesternDeedComponent`)를 쓴다. 틱 뒤에 같은 오브젝트의 HUD 를 쓴다.
     *          판 상태는 플레이어의 삶(`WesternOutlaw`)이고 지갑 · 평판 · 시계 · 날씨는 판 상태가 싣는다. 말 상태는 말 컴포넌트가 스냅숏에 싣는다. 자동 플레이(`-gv_wildWestAutoPlay=1`)는 기수를 가장 가까운 말에 태운다.
     *          HUD 는 게임의 `WildWestHUDViewModel` 을 HUD 컨트롤러에 건다 — 탑승 상태 글(`_rideStatus`), 말 스태미나(`_stamina` · `_staminaRatio`), 무법자 글(`_outlawStatus`)이고 문서(`ui/hud.ui.xml`)가 그렇게 잇는다.
     */
    REFLECT( Category = "WildWest", DisplayName = "Wild West Director", Tooltip = "Opens the western catalog, follows the rider with the camera, whistles horses and writes the HUD line" )
    class WildWestGameModeComponent : public WesternGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "WWDR" );
        static constexpr uint32 kStateVersion = 2;

        WildWestGameModeComponent();
        virtual ~WildWestGameModeComponent() override;

        void serializeState( StateIO& io ) override;

        /** @brief 판이 열렸으면 읽은 서부극 카탈로그이고, 아니면 nullptr 입니다(말 컴포넌트가 품종을 찾는다). */
        const WesternCatalog* findWesternCatalog() const override { return isStarted() ? _pWesternCatalog.get() : nullptr; }
        const WesternOutlaw*  findOutlaw() const override { return isStarted() ? &_outlaw : nullptr; }
        /** @brief 무법자 글입니다. 아무 일도 없으면(시작 명예 · 수배 없음 · 가죽 없음 · 코어 멀쩡) 비어 있어 HUD 에 아무것도 그리지 않습니다. */
        string                  formatOutlawStatus() const;
        const GameObjectHandle& getPlayerObject() const { return _player; }

    protected:
        [[nodiscard]] bool startGame() override;
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
        /** @brief 곁의 행동 자리를 씁니다. 틱 뒤에 돕니다. */
        void useDeed();
        /** @brief 수배 중이면 보안관이 기수를 보는지 법에 알립니다. 틱 뒤에 돕니다. */
        void updateLawSight();
        /** @brief 같은 오브젝트의 HUD 에 탑승 상태 한 줄과 말 스태미나, 무법자 글을 씁니다. 틱 뒤에 돕니다. */
        void updateHUD();
        /** @brief 자동 플레이: 기수를 가장 가까운 빈 좌석에 태웁니다. 틱 뒤에 돕니다. */
        void mountNearestHorse();

    private:
        PROPERTY( Category = "Data", DisplayName = "Western Data", Tooltip = "WesternCatalog XML (horse breeds, bond levels, survival)" )
        AssetRef<WesternCatalog> _westernDataPath;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Rider object (pawn with Mount Interaction)" )
        GameObjectHandle _player;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent that follows the rider" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Control", DisplayName = "Whistle Button", Tooltip = "Intent button on the rider pawn that calls the horses" )
        hashed_string _whistleButton;
        PROPERTY( Category = "Control", DisplayName = "Use Button", Tooltip = "Intent button on the rider pawn that does the deed at the nearest spot" )
        hashed_string _useButton;
        PROPERTY( Category = "Data", DisplayName = "Weather Data", Tooltip = "Weather catalog; the weather's temperature value moves the air temperature" )
        AssetRef<WeatherCatalog> _weatherDataPath;
        PROPERTY( Category = "Outlaw", DisplayName = "Region", Tooltip = "<Region id> in the western catalog this map belongs to" )
        hashed_string _regionID;
        PROPERTY( Category = "Outlaw", DisplayName = "Base Temperature", Tooltip = "Air temperature of the region before the weather" )
        float32 _regionBaseTemperature;
        PROPERTY( Category = "Outlaw", DisplayName = "Seconds Per Day", Min = 1.0, Tooltip = "Real seconds of one game day", Units = s )
        float32 _secondsPerDay;
        PROPERTY( Category = "Outlaw", DisplayName = "Witness Sight Range", Min = 0.0, Tooltip = "How far witnesses see a crime or the player", Units = m )
        float32 _witnessSightRange;
        PROPERTY( Category = "Outlaw", DisplayName = "Low Core Warning", Min = 0.0, Max = 100.0, Tooltip = "The HUD warns when a core falls below this" )
        float32 _lowCoreWarning;
        PROPERTY( Category = "Outlaw", DisplayName = "Starting Money", Min = 0, Tooltip = "Money of a new game, in the catalog currency" )
        int32 _startingMoney;

        WesternOutlaw                    _outlaw;
        shared_ptr<const WesternCatalog> _pWesternCatalog;
        shared_ptr<const WeatherCatalog> _pWeatherCatalog;
        uint8                            _bAutoMountRequested : 1; ///< 자동 플레이가 기수를 태우라고 한 번 청했다
        [[maybe_unused]] uint8           _reserved            : 7;
    };
} // namespace sw
