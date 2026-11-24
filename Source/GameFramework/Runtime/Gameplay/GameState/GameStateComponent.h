/**
 * @file GameStateComponent.h
 * @brief 키트 디렉터 여럿이 함께 쓰는 월드 상태(씬에 하나)를 보관하는 컴포넌트입니다. 월드 플래그, 시계, 날씨, 땅이 들어 있습니다. 언리얼 `AGameState` 자리입니다.
 * @details 상태 범위는 셋이다(언리얼 `AGameState` · `APlayerState` · 매치 상태): 월드 상태는 이 컴포넌트, 플레이어별 상태(지갑 · 가방 · 퀘스트 일지 · 평판)는
 *          조종자 오브젝트의 플레이어 상태 컴포넌트(`WalletComponent` · `InventoryComponent` · `QuestLogComponent` · `ReputationComponent`), 미니게임 한 판의 임시 상태는 디렉터가 결과(`GameModeComponent::collectMinigameResult`)로만 내보낸다.
 *          클래스 이름은 `SessionState` 로 둔다 — 컴포넌트를 두루 고치는 다른 갈래와 병합 충돌이 커서 `WorldState` 로 바꾸기는 그 갈래가 합쳐진 뒤에 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Condition/IGameFlagsOwner.h"
#include "GameFramework/Runtime/Gameplay/GameState/DeferredStateRestore.h"
#include "GameFramework/Runtime/World/Environment/WeatherMaterialWriter.h"
#include "GameFramework/Runtime/World/Environment/WeatherSceneWriter.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"
#include "GameFramework/Runtime/World/Land/LandRegistry.h"

namespace sw
{
    class Archive;
    class GameObjectManager;
    class WeatherCatalog;

    /**
     * @brief 월드 상태를 여는 데 필요한 설정입니다. 플레이어별 설정(퀘스트 · 평판 · 아이템 카탈로그, 가방 슬롯 수)은 플레이어 상태 컴포넌트마다 연다(`PlayerComponentUtil::openOnOwner`).
     * @details 숫자 필드의 범위는 `PROPERTY( Min )` 이고(시계 설정 안까지), 벗어나면 `GameStateComponent::initialize` 가 오류로 거절한다(`ConfigRangeUtil`).
     *          땅 크기를 조용히 고치지 않는다(폴백 금지 원칙). 날씨 카탈로그는 리플렉션 필드가 아니다.
     */
    REFLECT()
    struct SW_GF_API GameStateSettings
    {
        REFLECT_BODY();
        PROPERTY()
        WorldClockSettings               _clock{};
        shared_ptr<const WeatherCatalog> _pWeatherCatalog; ///< 없으면 날씨가 없고 `findWeather` 가 nullptr 이다. 받은 때의 판(스냅숏) — 날씨 상태가 그 판의 항목을 가리키므로 판 상태가 쥔다
        PROPERTY()
        uint32 _weatherSeed{ DeterministicRandom32::kDefaultSeed }; ///< 날씨 수열의 씨앗(0 은 거절된다 — 씨앗을 주지 않으면 기본 씨앗)
        PROPERTY( Min = 0 )
        int32 _landWidth{ 0 }; ///< 공유 땅의 셀 수. 0 이면 땅이 없다(`LandRegistry::isInitialized` 가 false). 격자 키트는 단독으로 쓴다
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
    /**
     * @class GameStateComponent
     * @brief 키트 디렉터 여럿이 함께 쓰는 월드 상태(씬에 하나 — 둘째는 `onRegister` 가 거절한다)입니다. 같은 오브젝트에 디렉터들과 함께 두고 **맨 앞**에 붙입니다.
     * @details - **순서**: 한 오브젝트의 틱은 한 워커가 붙은 순서대로 처리한다(`TickRegistry`). 이 컴포넌트(시계를 진행), 첫 디렉터, 둘째 디렉터 순서가
     *            매 틱 같고, 디렉터는 앞 디렉터가 이번 틱에 쓴 플래그를 잠금 없이 읽는다. 주의할 점이 있다. 공유 상태를 쓰는 디렉터를 **다른 오브젝트**에 두면
     *            같은 그룹에서 동시에 실행되어 데이터 경쟁이 생긴다. 그래서 찾는 함수도 `findOnOwner`(같은 오브젝트) 하나뿐이다.
     *          - **알림**: 시계 알림은 이 컴포넌트가 틱마다 꺼내서 `getClockEvents` 로 내준다. 다음 틱 처음에 비운다(`ReputationComponent` 가 날이 넘어갈 때 평판을 식히며 읽는다).
     *          - **여는 쪽**: 같은 오브젝트의 첫 디렉터가 `startGame` 에서 `initialize` 를 부른다. 디렉터가 없는 씬(시연 · 테스트)은 `_bOpenAtBeginPlay` 로 기본 설정으로 연다. 시작값(공유 상태를 건드리는 시작 배치 포함)은
     *            `isFreshGame()` 일 때만 넣는다. 복원한 판 위에 시작값이 중복으로 쌓이지 않게 하기 위해서다.
     *          - **세이브**: `writeState` 는 상태마다 구간 하나(`StateIO::writeSection`)를 쓴다. 알 수 없는 구간은 건너뛰고, 빠진 구간은 새 판으로 둔다. 읽지 못한 구간이 있으면 로드 전체가 실패하고 상태는 그대로다(조용히 새 판으로 두지 않는다).
     *            정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다(게임마다 적던 줄을 빠뜨린 게임은 시계 · 날씨 · 플래그를 조용히 잃었다) — 게임의 디렉터보다 먼저 복원 바이트를 받는다.
     */
    REFLECT( Category = "Framework", DisplayName = "Game State", Tooltip = "World state, one per scene (flags, clock, weather, land) used by the kit directors on the same object" )
    class SW_GF_API GameStateComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( GameStateComponent );

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics; ///< 디렉터와 같은 그룹이다. 같은 오브젝트 안에서는 붙은 순서대로 처리하며, 이 컴포넌트가 맨 앞이다.
        REFLECT_INTERFACE( IGameFlagsOwner, _gameFlagsOwnerPort );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "GTST" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 플레이어 구간(지갑 · 일지 · 평판 · 가방)이 플레이어 상태 컴포넌트로 갔다 — 판 1 은 그 구간을 버리고 올린다

        GameStateComponent();
        virtual ~GameStateComponent() override;

        /**
         * @brief 판 상태를 엽니다. 첫 호출만 설정을 쓰고, 보관하고 있던 복원 바이트가 있으면 그것으로 복원합니다.
         * @return 새 판, 복원, 이미 열림, 거절 중 하나입니다. 복원하려던 바이트를 읽지 못했으면 경고를 남기고 새 판(`Fresh`)으로 엽니다.
         *         설정이 범위 밖이면 오류를 남기고, 등록에서 거절된 둘째 월드 상태(`onRegister`)면 `Refused` 이며, 열리지 않은 채로 남습니다.
         * @details 월드 상태(시계 · 날씨 · 플래그 · 땅)는 씬에 하나다 — 하위 레벨 미니게임이 바탕 씬에 둘째 판 상태를 열면 어느 시계 · 날씨를 읽을지가 등록 순서에 달린다(`findOpen`).
         */
        [[nodiscard]] GameStateInitResult initialize( const GameStateSettings& settings );

        /**
         * @brief 씬의 컴포넌트 레지스트리에 오릅니다 — 판 상태를 찾는 틱(날씨 파도 · 표면 노출 · 지역)이 씬을 훑지 않는다(`findOpen`).
         * @details 씬에 이미 월드 상태가 있으면 오류(`GameState.SecondInScene`)를 남기고 오르지 않으며, 이 컴포넌트는 열리지 않는다(`initialize` 가 `Refused`).
         */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 틱 그룹을 디렉터와 같은 `PrePhysics` 로 정합니다. */
        void onBeginPlay() override;
        /**
         * @brief 열렸으면 시계를 진행하고(날씨는 시계 뒤에 게임 초 단위로) 이번 틱의 시계 알림을 모읍니다.
         * @details 날씨가 머티리얼 칸을 적은 카탈로그면 틱 뒤 게임 스레드에서 그 값을 묶음에 쓴다(`WeatherMaterialWriter`) — 장면마다 따로 놓지 않는다.
         */
        void onTick( TickContext& context ) override;
        /** @brief 날씨가 쓴 머티리얼 칸과 씬 배율을 되돌리고 날씨 소리를 멈춥니다. */
        void onEndPlay() override;

        /** @brief 구간 넷(플래그, 시계, 날씨, 땅)을 씁니다. `ComponentStateStore::capture` 가 부릅니다. 열리기 전이면 보관하고 있던 바이트를 그대로 씁니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 보관해 두었다가 `initialize` 에서 적용하고, 열린 뒤면 바로 적용합니다. `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool isInitialized() const { return _restore.isOpen(); }
        /** @brief 이번 판이 새 판으로 열렸으면 true 이고, 복원한 판이면 false 입니다. 디렉터가 시작값을 넣을지 판단할 때 묻습니다. */
        bool isFreshGame() const { return _restore.isFreshGame(); }
        /** @brief 열렸으면 판 상태의 플래그입니다(`IGameFlagsOwner` — 아래 층의 활성자 볼륨이 진행 플래그를 적는다). */
        [[nodiscard]] GameFlags* findOpenFlags() { return isInitialized() ? &_flags : nullptr; }
        GameFlags&               getFlags() { return _flags; }
        const GameFlags&         getFlags() const { return _flags; }
        WorldClock&              getClock() { return _clock; }
        const WorldClock&        getClock() const { return _clock; }
        WeatherSystem&           getWeather() { return _weather; }
        const WeatherSystem&     getWeather() const { return _weather; }
        /** @brief 날씨가 씬의 빛 · 바람 · 소리에 쓰는 쪽입니다(읽기 — 감시 값). */
        const WeatherSceneWriter& getWeatherScene() const { return _weatherScene; }
        LandRegistry&             getLand() { return _land; }
        const LandRegistry&       getLand() const { return _land; }

        /** @brief 이번 틱에 시계가 넘은 경계 목록입니다. 읽기만 하며, 다음 틱 처음에 비웁니다. */
        const vector<WorldClockEvent>& getClockEvents() const { return _listClockEvent; }

        /** @brief 날씨 카탈로그로 열었으면 날씨이고, 아니면 nullptr 입니다 — 날씨를 선택으로 빌리는 키트(`T*`)에 넘긴다. */
        [[nodiscard]] WeatherSystem* findWeather() { return _bWeather == SW_TRUE ? &_weather : nullptr; }

        /** @brief @p component 와 같은 오브젝트에 있는 판 상태입니다. 없으면 nullptr 입니다. */
        static GameStateComponent* findOnOwner( const Component& component );
        /**
         * @brief @p component 와 같은 오브젝트의 판 상태를 찾아 엽니다. 디렉터의 `startGame` 이 부릅니다. 판 상태가 없거나 설정이 거절되면 오류를 남기고 nullptr 입니다.
         * @param outResult 새 판의 시작값을 넣을 첫 디렉터인지 볼 때 쓰며(`Fresh`), 실패하면 `Refused` 입니다.
         */
        [[nodiscard]] static GameStateComponent* openOnOwner( const Component& component, const GameStateSettings& settings, GameStateInitResult& outResult );
        /** @brief `openOnOwner` 의 결과를 보지 않는 버전입니다. 시작값은 `isFreshGame` 으로 봅니다. */
        [[nodiscard]] static GameStateComponent* openOnOwner( const Component& component, const GameStateSettings& settings );
        /**
         * @brief 씬 @p pManager 의 열린 판 상태(씬에 하나 — `onRegister` 가 둘째를 거절한다)입니다. 없으면 nullptr 입니다. 자동화 단계 · 감시 값 · 지역 · 날씨가 읽는다.
         * @details 씬을 훑지 않고 컴포넌트 레지스트리를 읽는다 — 잠금 없는 읽기라 병렬 틱에서도 부를 수 있고, 틱마다 불려도 할당하지 않는다.
         */
        [[nodiscard]] static GameStateComponent* findOpen( const GameObjectManager* pManager );
        /**
         * @brief 씬 @p manager 의 지금 날씨 값 @p valueName(카탈로그 `<Values>`)입니다. 날씨를 연 판 상태가 없는 씬은 0 입니다. 병렬 틱에서도 부를 수 있다.
         * @details 날씨를 자기 값으로 읽는 쪽(강수 · 젖음 · 파도 · 데칼 씻김)의 한 길이다. 빛의 어둠은 읽지 않는다 — 날씨가 해 · 하늘에 배율로 쓰므로(`<SceneBinding>`) 빛의 세기를 읽으면 이미 들어 있다
         *          (저마다 날씨 값을 곱하면 어둠이 두 번 걸린다).
         */
        [[nodiscard]] static float32 readWeather( const GameObjectManager& manager, const hashed_string& valueName );

    private:
        GameFlags                        _flags;
        WorldClock                       _clock;
        shared_ptr<const WeatherCatalog> _pWeatherCatalog; ///< 날씨가 가리키는 카탈로그의 판(열 때의 스냅숏 — 다시 읽기 정리가 놓아도 날씨의 항목 포인터가 끊기지 않는다)
        WeatherSystem                    _weather;
        WeatherMaterialWriter            _weatherMaterial; ///< 날씨 → 지면 머티리얼 묶음(카탈로그의 바인딩). `_pWeatherCatalog` 뒤에 둔다 — 먼저 사라진다
        WeatherSceneWriter               _weatherScene;    ///< 날씨 → 빛 · 하늘 · 안개 · 바람 · 소리(카탈로그의 바인딩). 카탈로그보다 먼저 사라진다
        LandRegistry                     _land;            ///< 공유 땅. 격자와 배치 키트가 셀을 얻어 쓴다
        vector<WorldClockEvent>          _listClockEvent;
        DeferredStateRestore             _restore;      ///< 열리기 전에 받은 복원 바이트 · 열림 · 새 판
        uint8                            _bWeather : 1; ///< 날씨 카탈로그로 열었는지 여부
        PROPERTY( Tooltip = "Open with default settings at begin play - only for scenes without a kit director (showcases, tests); a director opens it with its own settings" )
        uint8                  _bOpenAtBeginPlay : 1;
        uint8                  _bRefusedInScene  : 1; ///< 같은 씬에 이미 월드 상태가 있어 등록에서 거절됐다(`onRegister`) — 열리지 않는다
        [[maybe_unused]] uint8 _reserved         : 5;

    private:
        /** @brief 플래그 주인 계약을 주인의 `findOpenFlags` 로 넘기는 포트입니다(`REFLECT_INTERFACE`). */
        struct GameFlagsOwnerPort final : IGameFlagsOwner
        {
            [[nodiscard]] GameFlags* findOpenFlags() override;
        };
        GameFlagsOwnerPort _gameFlagsOwnerPort;
    };
} // namespace sw
