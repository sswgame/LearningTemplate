/**
 * @file GameStateComponent.h
 * @brief 키트 디렉터 여럿이 나눠 쓰는 판 상태(지갑 · 플래그 · 시계 · 퀘스트 일지 · 평판 · 가방 · 날씨 · 땅)를 든 컴포넌트입니다 — 언리얼 Lyra 의 GameState 액터 + GameState 컴포넌트 자리.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Quest/QuestLog.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Base/World/LandRegistry.h"
#include "GameFramework/Base/World/WeatherSystem.h"
#include "GameFramework/Base/World/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ItemCatalog;
    class QuestCatalog;
    class ReputationCatalog;
    class WeatherCatalog;

    /** @brief 판 상태를 여는 데 드는 것입니다. 카탈로그는 빌려 씁니다(컴포넌트보다 오래 살아야 한다 — 보통 게임 인스턴스가 든다). */
    struct GameStateSettings
    {
        WorldClockSettings       _clock{};
        const QuestCatalog*      _pQuestCatalog{ nullptr };
        const ReputationCatalog* _pReputationCatalog{ nullptr };
        const ItemCatalog*       _pItemCatalog{ nullptr };
        int32                    _inventorySlotCount{ 0 };    ///< 플레이어 가방 칸 수(0 이면 가방 없음 — `makeRefs` 의 가방 칸이 nullptr)
        const WeatherCatalog*    _pWeatherCatalog{ nullptr }; ///< 없으면 날씨 없음 — `makeRefs` 의 날씨 칸이 nullptr
        uint32                   _weatherSeed{ 0 };
        int32                    _landWidth{ 0 }; ///< 공유 땅 칸 수(0 이면 땅 없음 — `makeRefs` 의 땅 칸이 nullptr, 격자 키트는 단독)
        int32                    _landHeight{ 0 };
        float32                  _landCellSize{ 1.0f };
        float3                   _landOrigin{};
    };
} // namespace sw

namespace sw
{
    /** @brief `GameStateComponent::initialize` 의 결과입니다. */
    enum class GameStateInitResult : uint8
    {
        Fresh = 0,         ///< 새 판 — 여는 디렉터가 시작값(시작 돈 · 첫 플래그)을 넣는다
        Restored,          ///< 들고 있던 세이브 · 핫 리로드 바이트로 되살렸다 — 시작값을 넣지 않는다
        AlreadyInitialized ///< 같은 오브젝트의 앞 디렉터가 이미 열었다 — 설정은 무시한다(새 판인지는 `isFreshGame`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameStateComponent
     * @brief 키트 디렉터 여럿이 나눠 쓰는 판 상태 하나입니다. 같은 오브젝트에 디렉터들과 함께 두고 **맨 앞**에 붙입니다.
     * @details - **순서** — 한 오브젝트의 틱은 한 워커가 붙은 순서대로 돕니다(`TickRegistry`). 이 컴포넌트(시계를 흘림) → 첫 디렉터 → 둘째 디렉터 순서가
     *            매 틱 같고, 디렉터는 앞 디렉터가 이번 틱에 쓴 지갑 · 플래그를 잠금 없이 봅니다. 주의: 공유 상태를 쓰는 디렉터를 **다른 오브젝트**에 두면
     *            같은 그룹에서 동시에 돌아 데이터 경쟁이다 — 그래서 찾는 길도 `findOnOwner`(같은 오브젝트) 하나다.
     *          - **알림** — 시계 알림은 이 컴포넌트가 틱마다 꺼내 `getClockEvents` 로 내놓는다(다음 틱 처음에 비운다). 지갑 · 일지 · 평판의 알림 버퍼는
     *            게임(화면)의 것이다 — 빌려 쓰는 키트는 꺼내지 않고 상태(`QuestLog::getStatus` …)를 본다.
     *          - **여는 쪽** — 같은 오브젝트의 첫 디렉터가 `startGame` 에서 `initialize` 를 부른다. 시작값(공유 상태를 건드리는 시작 배치 포함)은
     *            `isFreshGame()` 일 때만 넣는다 — 되살린 판 위에 시작값이 덧쌓이지 않게.
     *          - **세이브** — `writeState` 는 상태마다 구간 하나(`StateArchiveUtil::writeSection`)라 모르는 구간은 건너뛰고 빠진 구간은 새 판으로 둔다.
     *            게임 인스턴스가 생성자에서 `registerStatefulComponent<GameStateComponent>()` 로 스냅숏에 올린다.
     */
    REFLECT( Category = "Framework", DisplayName = "Game State", Tooltip = "Shared state (wallet, flags, clock, quests, reputation) used by the kit directors on the same object" )
    class SW_GF_API GameStateComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "GTST" );
        static constexpr uint32 kStateVersion = 1;

        GameStateComponent();
        virtual ~GameStateComponent() override;

        /**
         * @brief 판 상태를 엽니다. 첫 호출만 설정을 쓰고, 들고 있던 복원 바이트가 있으면 그것으로 되살립니다.
         * @return 새 판 · 되살림 · 이미 열림. 되살리려던 바이트를 읽지 못했으면 알리고 새 판(`Fresh`)입니다.
         */
        GameStateInitResult initialize( const GameStateSettings& settings );

        /** @brief 틱 그룹을 디렉터와 같은 `PrePhysics` 로 둡니다. */
        void onBeginPlay() override;
        /** @brief 열렸으면 시계를 흘리고(날씨는 시계 뒤에 게임 초로, 날 넘김마다 평판이 식는다) 이번 틱의 시계 알림을 모읍니다. */
        void onTick( float32 deltaTime ) override;

        /** @brief 구간 여덟(지갑 · 플래그 · 시계 · 일지 · 평판 · 가방 · 날씨 · 땅)을 씁니다 — `ComponentStateStore::capture` 가 부릅니다. 열리기 전이면 들고 있던 바이트를 그대로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 열리기 전이면 들고 있다가 `initialize` 에서, 열린 뒤면 바로 적용합니다 — `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool isInitialized() const { return _bInitialized == SW_TRUE; }
        /** @brief 이번 판이 새 판으로 열렸으면 true 입니다(되살린 판이면 false). 디렉터가 시작값을 넣을지 묻습니다. */
        bool                   isFreshGame() const { return _bFreshGame == SW_TRUE; }
        Wallet&                getWallet() { return _wallet; }
        const Wallet&          getWallet() const { return _wallet; }
        GameFlags&             getFlags() { return _flags; }
        const GameFlags&       getFlags() const { return _flags; }
        WorldClock&            getClock() { return _clock; }
        const WorldClock&      getClock() const { return _clock; }
        QuestLog&              getQuestLog() { return _questLog; }
        const QuestLog&        getQuestLog() const { return _questLog; }
        ReputationState&       getReputation() { return _reputation; }
        Inventory&             getInventory() { return _inventory; }
        WeatherSystem&         getWeather() { return _weather; }
        const WeatherSystem&   getWeather() const { return _weather; }
        const Inventory&       getInventory() const { return _inventory; }
        const ReputationState& getReputation() const { return _reputation; }
        LandRegistry&          getLand() { return _land; }
        const LandRegistry&    getLand() const { return _land; }
        /** @brief 이번 틱에 시계가 넘은 경계입니다(읽기만 — 다음 틱 처음에 비운다). */
        const vector<WorldClockEvent>& getClockEvents() const { return _listClockEvent; }

        /** @brief 이 판 상태를 빌려 줄 포인터 묶음입니다. 설정에서 열지 않은 것(가방 칸 0 · 날씨 카탈로그 없음 · 땅 크기 0)은 nullptr 입니다. */
        GameStateRefs makeRefs();

        /** @brief @p component 와 같은 오브젝트의 판 상태입니다. 없으면 nullptr 입니다. */
        static GameStateComponent* findOnOwner( const Component& component );

    private:
        /** @brief 들고 있던 복원 바이트를 적용합니다. 틀이 깨졌으면 알리고 false 입니다. */
        [[nodiscard]] bool applyPendingState();
        /** @brief 구간을 읽어 아는 것만 바꿉니다. 틀(머리 · 구간 머리)이 깨졌으면 false 이고 아무것도 바꾸지 않습니다. 구간 하나가 깨진 것은 그 구간만 새 판입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        Wallet                  _wallet;
        GameFlags               _flags;
        WorldClock              _clock;
        QuestLog                _questLog;
        ReputationState         _reputation;
        Inventory               _inventory; ///< 플레이어 가방
        WeatherSystem           _weather;
        LandRegistry            _land; ///< 공유 땅 — 격자 · 배치 키트가 칸을 얻는다
        vector<WorldClockEvent> _listClockEvent;
        vector<uint8>           _pendingStateBytes; ///< 열리기 전에 받은 복원 바이트
        uint8                   _bInitialized : 1;
        uint8                   _bFreshGame   : 1;
        uint8                   _bWeather     : 1; ///< 날씨 카탈로그로 열었다
        uint8                   _reserved     : 5;
    };
} // namespace sw
