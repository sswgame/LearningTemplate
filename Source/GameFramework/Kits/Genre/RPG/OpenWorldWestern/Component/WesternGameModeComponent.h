/**
 * @file WesternGameModeComponent.h
 * @brief 서부극 키트의 디렉터 — 서부극 데이터(`WesternCatalog`)와 판 상태를 열고, 플레이어의 명예 · 법 · 생존 · 사냥(`WesternOutlaw`)을 돌리고, 휘파람으로 말을 부르고, 행동 자리를 씁니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트의 `WesternOutlaw` · `WesternHorse` …)을 들고 씬과 잇는다. 카메라 초점 · HUD 는 클라이언트 모듈(`GF_Client_OpenWorldWestern` 의
 *          `WesternRiderViewComponent`)이 이 디렉터의 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 휘파람 · 행동은 기수 폰의 의도 버튼으로 읽는다(서버가 받은 조종 명령과 같은 길).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternOutlaw.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    class PawnComponent;

    /**
     * @class WesternGameModeComponent
     * @brief 레드 데드 리뎀션 2 · 젤다 BotW 식 오픈월드 승마 한 판입니다. 걷는 기수(`_player`)가 가까운 말에 상호작용 버튼으로 올라타고(`MountInteractionComponent`),
     *        말을 몰고(`MountMovementComponent` · `WesternHorseMountComponent`), 같은 버튼으로 내립니다(`VehicleExitComponent`). 이 디렉터는 그 흐름을 돌리지 않고 둘레만 맡습니다.
     * @details 타기 · 몰기 · 내리기는 키트와 기반 컴포넌트가 하고, 말 한 마리의 상태(`WesternHorse`)는 말 오브젝트의 `WesternHorseComponent` 가 가진다(품종은 이 디렉터의 카탈로그에서 찾는다).
     *          이 디렉터가 하는 일은 셋이다. 판을 열 때 서부극 카탈로그 · 날씨를 읽고 같은 오브젝트의 월드 상태(`GameStateComponent`, 맨 앞)와 플레이어 상태 오브젝트(`_playerState` — 조종자)의 평판(`ReputationComponent`)을 열고 지갑(`WalletComponent`)을 찾는다(없으면 판을 열지 않는다).
     *          기수가 걷는 동안 휘파람 버튼이 발동하면 그 기수를 주인으로 둔 말 AI(`FollowOwnerAIControllerComponent`)를 모두 부른다. 행동 버튼이 발동하면 곁의 자리(`WesternDeedComponent`)를 쓴다.
     *          판 상태는 플레이어의 삶(`WesternOutlaw`)이고 시계 · 날씨는 월드 상태, 지갑 · 평판은 플레이어 상태 오브젝트의 컴포넌트가 싣는다(규칙은 틱 뒤 게임 스레드에서 돌아 다른 오브젝트를 읽고 쓴다). 말 상태는 말 컴포넌트가 스냅숏에 싣는다.
     *          자동 플레이(`_bAutoPlay` · 게임의 자동 플레이 스위치)는 기수를 가장 가까운 말에 태운다.
     */
    REFLECT( Category = "Western", DisplayName = "Western Game Mode", Tooltip = "Opens the western catalog, runs the player's honor, law, survival and hunting, whistles horses and does deeds" )
    class SW_KIT_API WesternGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "WWDR" };

        WesternGameModeComponent();
        virtual ~WesternGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        /** @brief 판이 열렸으면 읽은 서부극 카탈로그이고, 아니면 nullptr 입니다(말 컴포넌트가 품종을 찾는다). */
        const WesternCatalog* findWesternCatalog() const { return isStarted() ? _pWesternCatalog.get() : nullptr; }
        /** @brief 판이 열렸으면 플레이어의 명예 · 법 · 생존 · 사냥이고, 아니면 nullptr 입니다(클라이언트 모듈 · 감시 값이 읽는다). */
        const WesternOutlaw* findOutlaw() const { return isStarted() ? &_outlaw : nullptr; }
        /** @brief 기수 폰입니다. 없으면 nullptr 입니다. */
        const PawnComponent* findPlayerPawn() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( GameSeconds frameTime ) override;

    private:
        /** @brief 이 기수를 주인으로 둔 말 AI 를 모두 부릅니다. 틱 뒤에 돕니다. */
        void whistleHorses();
        /** @brief 곁의 행동 자리를 씁니다. 틱 뒤에 돕니다. */
        void useDeed();
        /** @brief 수배 중이면 보안관이 기수를 보는지 법에 알립니다. 틱 뒤에 돕니다. */
        void updateLawSight();
        /** @brief 자동 플레이: 기수를 가장 가까운 빈 좌석에 태웁니다. 틱 뒤에 돕니다. */
        void mountNearestHorse();
        /** @brief 법 · 명예의 알림을 소리와 로그로 알립니다. */
        void reportEvents();

    private:
        PROPERTY( Category = "Data", DisplayName = "Western Data", Tooltip = "WesternCatalog XML (horse breeds, bond levels, survival)" )
        AssetRef<WesternCatalog> _westernDataPath;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Rider object (pawn with Mount Interaction)" )
        GameObjectHandle _player;
        PROPERTY( Category = "Control", DisplayName = "Whistle Button", Tooltip = "Intent button on the rider pawn that calls the horses" )
        hashed_string _whistleButton;
        PROPERTY( Category = "Control", DisplayName = "Use Button", Tooltip = "Intent button on the rider pawn that does the deed at the nearest spot" )
        hashed_string _useButton;
        PROPERTY( Category = "Data", DisplayName = "Weather Data", Tooltip = "Weather catalog; the weather's temperature value moves the air temperature" )
        AssetRef<WeatherCatalog> _weatherDataPath;
        PROPERTY( Category = "Outlaw", DisplayName = "Region", Tooltip = "<Region id> in the western catalog this map belongs to" )
        CatalogKey<WesternRegionDef> _regionID;
        PROPERTY( Category = "Outlaw", DisplayName = "Base Temperature", Tooltip = "Air temperature of the region before the weather" )
        float32 _regionBaseTemperature;
        PROPERTY( Category = "Outlaw", DisplayName = "Seconds Per Day", Min = 1.0, Tooltip = "Real seconds of one game day", Units = s )
        float32 _secondsPerDay;
        PROPERTY( Category = "Outlaw", DisplayName = "Witness Sight Range", Min = 0.0, Tooltip = "How far witnesses see a crime or the player", Units = m )
        float32 _witnessSightRange;
        PROPERTY( Category = "Outlaw", DisplayName = "Starting Money", Min = 0, Tooltip = "Money of a new game, in the catalog currency" )
        int32 _startingMoney;

        SW_CODEC_STATE WesternOutlaw  _outlaw;
        BoundAssetRef<WesternCatalog> _pWesternCatalog;
        SW_TRANSIENT vector<WesternLawEvent> _listLawEvent;           ///< `reportEvents` 의 임시 목록(용량만 넘긴다)
        SW_TRANSIENT vector<ReputationEvent> _listHonorEvent;         ///< `reportEvents` 의 임시 목록(용량만 넘긴다)
        SW_TRANSIENT vector<WesternWitness> _listWitness;             ///< `updateLawSight` 가 프레임마다 다시 채우는 목격자 목록(용량을 다시 쓴다 — 상태가 아니다)
        SW_TRANSIENT uint8                  _bAutoMountRequested : 1; ///< 자동 플레이가 기수를 태우라고 한 번 청했다
        [[maybe_unused]] uint8              _reserved            : 7;
    };
} // namespace sw
