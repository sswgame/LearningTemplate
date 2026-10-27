/**
 * @file SessionStateComponent.h
 * @brief 키트 디렉터 여럿이 함께 쓰는 판 상태를 보관하는 컴포넌트입니다. 지갑, 플래그, 시계, 퀘스트 일지, 평판, 가방, 날씨, 땅이 들어 있습니다. 세이브 슬롯 하나(한 세션)의 상태이며, 월드 단위 복제 상태인 언리얼 GameState 와는 다릅니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/DeterministicRandom.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Progression/Reputation.h"
#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Base/World/Environment/WeatherSystem.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class GameObjectManager;
    class ItemCatalog;
    class QuestCatalog;
    class ReputationCatalog;
    class WeatherCatalog;

    /**
     * @brief 판 상태를 여는 데 필요한 설정입니다. 카탈로그는 소유하지 않고 참조만 하므로 컴포넌트보다 오래 살아 있어야 합니다. 보통 게임 인스턴스가 카탈로그를 보관합니다.
     * @details 숫자 필드의 범위는 `PROPERTY( Min )` 이고(시계 설정 안까지), 벗어나면 `SessionStateComponent::initialize` 가 오류로 거절한다(`ConfigRangeUtil`).
     *          음수 슬롯 수와 땅 크기를 조용히 고치지 않는다(폴백 금지 원칙). 카탈로그 포인터는 리플렉션 필드이 아니다.
     */
    REFLECT()
    struct SW_GF_API SessionStateSettings
    {
        REFLECT_BODY();
        PROPERTY()
        WorldClockSettings       _clock{};
        const QuestCatalog*      _pQuestCatalog{ nullptr };
        const ReputationCatalog* _pReputationCatalog{ nullptr };
        const ItemCatalog*       _pItemCatalog{ nullptr };
        PROPERTY( Min = 0 )
        int32                 _inventorySlotCount{ 0 };    ///< 플레이어 가방의 슬롯 수. 0 이면 가방이 없고 `makeRefs` 의 가방 포인터가 nullptr 이다
        const WeatherCatalog* _pWeatherCatalog{ nullptr }; ///< 없으면 날씨가 없고 `makeRefs` 의 날씨 포인터가 nullptr 이다
        PROPERTY()
        uint32 _weatherSeed{ DeterministicRandom32::kDefaultSeed }; ///< 날씨 수열의 씨앗(0 은 거절된다 — 씨앗을 주지 않으면 기본 씨앗)
        PROPERTY( Min = 0 )
        int32 _landWidth{ 0 }; ///< 공유 땅의 셀 수. 0 이면 땅이 없고 `makeRefs` 의 땅 포인터가 nullptr 이다. 격자 키트는 단독으로 쓴다
        PROPERTY( Min = 0 )
        int32 _landHeight{ 0 };
        PROPERTY( Min = 0.001 )
        float32 _landCellSize{ 1.0f }; ///< 셀 한 변의 월드 길이. 0 보다 커야 한다(`LandRegistry::initialize` 의 전제 조건)
        PROPERTY()
        float3 _landOrigin{};
    };
} // namespace sw

namespace sw
{
    /** @brief `SessionStateComponent::initialize` 의 결과입니다. */
    enum class SessionStateInitResult : uint8
    {
        Fresh = 0,          ///< 새 판이다. 여는 디렉터가 시작값(시작 돈, 첫 플래그)을 넣는다
        Restored,           ///< 보관하고 있던 세이브나 핫 리로드 바이트로 복원했다. 시작값은 넣지 않는다
        AlreadyInitialized, ///< 같은 오브젝트의 앞 디렉터가 이미 열었다. 설정은 무시하며, 새 판인지는 `isFreshGame` 으로 확인한다
        Refused             ///< 설정이 선언된 범위 밖이라 열지 않았다(오류를 남겼다). 여는 디렉터는 시작을 멈춘다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SessionStateComponent
     * @brief 키트 디렉터 여럿이 함께 쓰는 판 상태 하나입니다. 같은 오브젝트에 디렉터들과 함께 두고 **맨 앞**에 붙입니다.
     * @details - **순서**: 한 오브젝트의 틱은 한 워커가 붙은 순서대로 처리한다(`TickRegistry`). 이 컴포넌트(시계를 진행), 첫 디렉터, 둘째 디렉터 순서가
     *            매 틱 같고, 디렉터는 앞 디렉터가 이번 틱에 쓴 지갑과 플래그를 잠금 없이 읽는다. 주의할 점이 있다. 공유 상태를 쓰는 디렉터를 **다른 오브젝트**에 두면
     *            같은 그룹에서 동시에 실행되어 데이터 경쟁이 생긴다. 그래서 찾는 함수도 `findOnOwner`(같은 오브젝트) 하나뿐이다.
     *          - **알림**: 시계 알림은 이 컴포넌트가 틱마다 꺼내서 `getClockEvents` 로 내준다. 다음 틱 처음에 비운다. 지갑, 일지, 평판의 알림 버퍼는
     *            게임(화면)이 소유한다. 참조해서 쓰는 키트는 알림을 꺼내지 않고 상태(`QuestLog::getStatus` 등)를 본다.
     *          - **여는 쪽**: 같은 오브젝트의 첫 디렉터가 `startGame` 에서 `initialize` 를 부른다. 시작값(공유 상태를 건드리는 시작 배치 포함)은
     *            `isFreshGame()` 일 때만 넣는다. 복원한 판 위에 시작값이 중복으로 쌓이지 않게 하기 위해서다.
     *          - **세이브**: `writeState` 는 상태마다 구간 하나(`StateArchiveUtil::writeSection`)를 쓴다. 알 수 없는 구간은 건너뛰고, 빠진 구간은 새 판으로 둔다.
     *            게임 인스턴스가 생성자에서 `registerStatefulComponent<SessionStateComponent>()` 로 스냅샷에 등록한다.
     */
    REFLECT( Category = "Framework", DisplayName = "Game State", Tooltip = "Shared state (wallet, flags, clock, quests, reputation) used by the kit directors on the same object" )
    class SW_GF_API SessionStateComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "GTST" );
        static constexpr uint32 kStateVersion = 1;

        SessionStateComponent();
        virtual ~SessionStateComponent() override;

        /**
         * @brief 판 상태를 엽니다. 첫 호출만 설정을 쓰고, 보관하고 있던 복원 바이트가 있으면 그것으로 복원합니다.
         * @return 새 판, 복원, 이미 열림, 거절 중 하나입니다. 복원하려던 바이트를 읽지 못했으면 경고를 남기고 새 판(`Fresh`)으로 엽니다.
         *         설정이 범위 밖이면 오류를 남기고 `Refused` 이며, 열리지 않은 채로 남습니다.
         */
        [[nodiscard]] SessionStateInitResult initialize( const SessionStateSettings& settings );

        /** @brief 틱 그룹을 디렉터와 같은 `PrePhysics` 로 정합니다. */
        void onBeginPlay() override;
        /** @brief 열렸으면 시계를 진행하고(날씨는 시계 뒤에 게임 초 단위로, 날이 넘어갈 때마다 평판이 식음) 이번 틱의 시계 알림을 모읍니다. */
        void onTick( float32 deltaTime ) override;

        /** @brief 구간 여덟 개(지갑, 플래그, 시계, 일지, 평판, 가방, 날씨, 땅)를 씁니다. `ComponentStateStore::capture` 가 부릅니다. 열리기 전이면 보관하고 있던 바이트를 그대로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 열리기 전이면 보관해 두었다가 `initialize` 에서 적용하고, 열린 뒤면 바로 적용합니다. `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool isInitialized() const { return _bInitialized == SW_TRUE; }
        /** @brief 이번 판이 새 판으로 열렸으면 true 이고, 복원한 판이면 false 입니다. 디렉터가 시작값을 넣을지 판단할 때 묻습니다. */
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
        /** @brief 이번 틱에 시계가 넘은 경계 목록입니다. 읽기만 하며, 다음 틱 처음에 비웁니다. */
        const vector<WorldClockEvent>& getClockEvents() const { return _listClockEvent; }

        /** @brief 이 판 상태를 참조로 넘겨 줄 포인터 모음입니다. 설정에서 열지 않은 것(가방 슬롯 0, 날씨 카탈로그 없음, 땅 크기 0)은 nullptr 입니다. */
        SessionStateRefs makeRefs();

        /** @brief @p component 와 같은 오브젝트에 있는 판 상태입니다. 없으면 nullptr 입니다. */
        static SessionStateComponent* findOnOwner( const Component& component );
        /** @brief 씬 @p pManager 에서 열린 판 상태를 오브젝트 순서로 @p outListState 에 모읍니다(판 상태는 컴포넌트 레지스트리에 없다). 자동화 단계 · 탐침이 읽는다. */
        static void collectOpen( const GameObjectManager* pManager, vector<SessionStateComponent*>& outListState );
        /** @brief 씬 @p pManager 의 첫 열린 판 상태입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static const SessionStateComponent* findFirstOpen( const GameObjectManager* pManager );

    private:
        /** @brief 보관하고 있던 복원 바이트를 적용합니다. 구조가 깨졌으면 경고를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool applyPendingState();
        /** @brief 구간을 읽어서 아는 것만 바꿉니다. 구조(헤더와 구간 헤더)가 깨졌으면 false 를 반환하고 아무것도 바꾸지 않습니다. 구간 하나만 깨졌으면 그 구간만 새 판으로 둡니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        Wallet                  _wallet;
        GameFlags               _flags;
        WorldClock              _clock;
        QuestLog                _questLog;
        ReputationState         _reputation;
        Inventory               _inventory; ///< 플레이어 가방
        WeatherSystem           _weather;
        LandRegistry            _land; ///< 공유 땅. 격자와 배치 키트가 셀을 얻어 쓴다
        vector<WorldClockEvent> _listClockEvent;
        vector<uint8>           _pendingStateBytes; ///< 열리기 전에 받은 복원 바이트
        uint8                   _bInitialized : 1;
        uint8                   _bFreshGame   : 1;
        uint8                   _bWeather     : 1; ///< 날씨 카탈로그로 열었는지 여부
        uint8                   _reserved     : 5;
    };
} // namespace sw
