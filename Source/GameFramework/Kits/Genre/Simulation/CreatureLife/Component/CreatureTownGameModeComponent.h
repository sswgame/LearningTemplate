/**
 * @file CreatureTownGameModeComponent.h
 * @brief 생물 생활 키트(`GF_CreatureLife`)의 디렉터 — 플레이어의 부탁 일지 · 생물 호감도를 열고, 마을(`CreatureTown`)을 공유 시계 · 지갑 위에 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트 — 마을 · 카탈로그)을 들고 씬과 잇는다. 마을 오브젝트 · 생물의 모습과 대화 입력은 클라이언트 모듈(`GF_Client_CreatureLife` 의
 *          `CreatureTownViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          월드 상태(시계 · 날씨 · 땅)는 같은 오브젝트의 앞 디렉터가 연다(혼자 쓰는 씬은 `GameStateComponent::_bOpenAtBeginPlay`) — 열리지 않았으면 여기서 다른 설정으로 열지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureTown.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    class GameStateComponent;
    class QuestLog;
    class QuestLogComponent;
    class ReputationComponent;
    class Wallet;

    /** @struct CreatureTownPlacement @brief 마을 셀 하나에 놓을 오브젝트입니다(디렉터의 씬 데이터 — 시작 배치 · 마을 업그레이드). */
    REFLECT()
    struct CreatureTownPlacement
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Town object (the object of a habitat key in the creature catalog)" )
        CatalogKey<HabitatCell> _object{};
        PROPERTY( Min = 0 )
        int32 _x{ 0 };
        PROPERTY( Min = 0 )
        int32 _y{ 0 };
    };

    /**
     * @class CreatureTownGameModeComponent
     * @brief 마을 한 판입니다. 같은 오브젝트에서 월드 상태를 여는 디렉터 **뒤에** 붙어 그 디렉터가 이번 틱에 번 돈을 봅니다.
     * @details 시가 넘어가면 방문을 굴리고, 날이 넘어가면 하루를 넘긴다. 마을에 사는 생물은 카탈로그에 적힌 부탁(`<Species requests>`)을 받을 수 있게 되는 대로 한다.
     *          플레이어 지갑이 `_upgradePrice` 를 낼 수 있고 `_upgradeHabitat` 서식지가 아직 없으면 마을 업그레이드(`_listUpgradeObject`)을 산다.
     *          상태는 마을 하나(구간 'CTWN')이고 부탁 일지 · 호감도(공유 평판의 세력 `creature.<종>`) · 지갑은 플레이어 상태에 있다. 시작 배치는 공유 일지에 서식지 수를 알리므로
     *          새 판일 때만 놓는다.
     */
    REFLECT( Category = "CreatureLife", DisplayName = "Creature Town Game Mode", Tooltip = "Runs the creature town kit on the shared game state behind the game mode that opens it" )
    class SW_KIT_API CreatureTownGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "MVTN" );
        static constexpr uint32 kStateVersion = 1;

        CreatureTownGameModeComponent();
        virtual ~CreatureTownGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(판이 열린 동안만 받는다, 게임 스레드) ----
        /** @brief 생물과 대화합니다(하루 한 번 — 마을이 막는다). 판이 열리기 전이거나 마을이 거절하면 false 입니다. */
        [[nodiscard]] bool submitTalk( const hashed_string& speciesID );

        // ---- 읽기(클라이언트 모듈) ----
        const CreatureTown& getTown() const { return _town; }
        /** @brief 지금 카탈로그입니다(다시 읽힌 판을 따라간다). 판이 열리기 전이면 nullptr 일 수 있습니다. */
        const CreatureLifeCatalog* getCatalog() const { return _pCatalog.get(); }
        /** @brief 마을 모습(오브젝트 · 사는 생물)의 판입니다 — 바꾸거나 복원할 때마다 오르고, 열린 판은 1 이상입니다. 뷰는 이 값이 바뀐 때만 맞춘다. */
        uint32 getTownRevision() const { return _townRevision; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 마을을 열고 공유 상태(일지 · 평판 · 시계)를 빌립니다(시작과 복원이 같은 설정으로 연다). 크기가 범위 밖이면 마을이 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initializeTown( CreatureTown& outTown, GameStateComponent& state, ReputationComponent& reputation, QuestLogComponent& questLog ) const;
        /** @brief 배치 목록을 셀에 놓습니다. 놓지 못한 줄마다 오류를 남기고 false 입니다(데이터 오류). */
        [[nodiscard]] bool placeObjects( const vector<CreatureTownPlacement>& listPlacement );
        /** @brief 마을에 사는 생물이 카탈로그의 부탁 가운데 아직 받지 않은 것을 합니다. */
        void startOfferedRequests( const QuestLog& questLog );
        /** @brief 지갑이 낼 수 있고 업그레이드 서식지가 없으면 마을 업그레이드을 삽니다. */
        void buyUpgrade( Wallet& wallet, int32 day );
        /** @brief 마을이 낸 알림(서식지 · 방문 · 호감도 · 부탁 · 능력 · 매력)을 소리와 로그로 알립니다. */
        void reportTownEvents( int32 day );

    private:
        PROPERTY( Category = "Data", DisplayName = "Creature Data", Tooltip = "Creature life catalog XML" )
        AssetRef<CreatureLifeCatalog> _creatureDataPath;
        PROPERTY( Category = "Data", DisplayName = "Quest Data", Tooltip = "Quest catalog XML of the creature requests" )
        AssetRef<QuestCatalog> _questDataPath;
        PROPERTY( Category = "Data", DisplayName = "Friendship Data", Tooltip = "Reputation catalog XML, one faction (creature.<species>) per creature" )
        AssetRef<ReputationCatalog> _friendshipDataPath;
        PROPERTY( Category = "World", DisplayName = "Town Size", Tooltip = "Cells on one side of the square town (1 cell = 1 m); required", Min = 1 )
        int32 _townSize;
        PROPERTY( Category = "World", DisplayName = "Weather Without Catalog", Tooltip = "Weather id the visits are rolled with when the shared state runs no weather catalog" )
        hashed_string _fixedWeatherID;
        PROPERTY( Category = "Layout", DisplayName = "Start Objects", Tooltip = "Objects a fresh town starts with" )
        vector<CreatureTownPlacement> _listStartObject;
        PROPERTY( Category = "Economy", DisplayName = "Upgrade Habitat", Tooltip = "Habitat the town buys once the player can pay for it (empty: no upgrade)" )
        CatalogKey<HabitatDef> _upgradeHabitat;
        PROPERTY( Category = "Economy", DisplayName = "Upgrade Price", Tooltip = "Price of the upgrade in the default currency of the player wallet", Min = 0 )
        int32 _upgradePrice;
        PROPERTY( Category = "Economy", DisplayName = "Upgrade Objects", Tooltip = "Objects that form the upgrade habitat" )
        vector<CreatureTownPlacement> _listUpgradeObject;

        SharedAssetRef<CreatureLifeCatalog> _pCatalog;           ///< 마을이 빌린다 — 다시 읽히면 베이스가 상태를 왕복시켜 새 판에 다시 묶는다(`acquireRequired`)
        shared_ptr<const QuestCatalog>      _pQuestCatalog;      ///< 처음 연 때의 판 — 부탁 일지가 빌린다(일지는 첫 카탈로그만 쓴다)
        shared_ptr<const ReputationCatalog> _pFriendshipCatalog; ///< 처음 연 때의 판 — 평판이 빌린다(평판은 첫 카탈로그만 쓴다)
        CreatureTown                        _town;
        vector<CreatureTownEvent>           _listTownEvent; ///< `reportTownEvents` 의 임시 목록(용량만 넘긴다)
        uint32                              _townRevision;  ///< `getTownRevision`
    };
} // namespace sw
