/**
 * @file MinigameFlow.h
 * @brief 게임 인스턴스의 미니게임 흐름입니다 — 미니게임 정의를 읽고 검사해 켜고(게임 기능 · 카탈로그 · 입력 문맥 · UI) 그 맵으로 들어가며, 나올 때 끄고 들어오기 전 씬으로 돌아갑니다.
 * @details 형제 `GameSceneFlow` · `RespawnFlow` 와 같은 줄의 게임 인스턴스 서브시스템이다. 용과 같이 · GTA · RDR2 의 가게 · 오락실 미니게임, 언리얼 Lyra 의
 *          `ULyraExperienceManagerComponent`(Experience 를 올리고 내린다)와 같은 자리다. 형제 서브시스템(`OnlineSession`)처럼 늘 서고, 미니게임을 쓰지 않는 게임에서는 아무 일도 안 한다.
 *          게임 범위 기능(gamesettings `gameFeatures`)은 게임 인스턴스가 이 흐름보다 먼저 켜 둔다 — 미니게임 범위의 해제 검사는 그것이 켜진 채로 찍어 그 몫을 남은 것으로 세지 않는다.
 *          미니게임은 한 번에 하나다. 새 미니게임으로 들어가면 앞의 것을 먼저 끈다(교체) — 앞 미니게임의 등록이 다음으로 새지 않고, 끈 뒤 남은 것은 해제 검사가 알린다(`GameFeatureScope`).
 *          켜기는 맵을 요청하기 전이다 — 맵의 컴포넌트는 씬이 적용될 때(`beginPlay`, 맵 로드 완료 알림보다 앞) 기능 서비스 · 카탈로그를 찾는다
 *          (Lyra 도 Experience 를 다 올린 뒤에 `OnExperienceLoaded` 로 폰을 세운다).
 *          들어가기: `SceneSwitch` 는 들어갈 때의 활성 씬을 돌아올 자리(`SuspendedScene` — 씬 경로 + `GameStateSnapshot` 목적 `Snapshot`)로 찍고 맵을 바꾼다. 찍기 직전에 자동 저장을
 *          한 번 맡기고, 돌아올 자리가 있는 동안 저장을 막는다(`SaveLock` 까닭 `minigame` — 미니게임 맵이 세이브의 씬으로 적히지 않게, 컷씬 막기와 같은 길).
 *          나올 때 그 씬을 연 뒤 복원한다. 돌아올 자리가 없으면(첫 진입이 미니게임 — `gv_firstMinigame`) gamesettings 입구 씬(`GameSceneFlow::getEntranceScene`)으로 간다.
 *          `SubLevel` 은 들어갈 때의 활성 씬에 맵을 하위 레벨(이름 = 미니게임 이름, 자리 = 원점)로 더하고 나올 때 뺀다. 바탕 틱은 멈추지 않는다 —
 *          언리얼 레벨 인스턴스 · 유니티 Additive 씬도 바탕 레벨을 멈추지 않고, 미니게임 입력은 `inputContexts` 레이어가 위에서 가린다.
 *          장치 밖 씬 이동(세이브 불러오기 · 타이틀로 · 시나리오의 씬 열기)이 오면 돌아올 자리와 미니게임 범위를 비운다 — 씬 매니저의 받아들임 알림으로 이 흐름이 요청하지 않은 게임 이동을 듣는다.
 *          진입: 개발 명령 `minigame.enter <이름>` · `minigame.leave`, 사물의 `MinigameTriggerComponent`(틱 뒤에 맡기고 `update` 가 틱 밖에서 처리 — 스냅숏은
 *          씬을 도는 동안 찍지 않는다), 실행 전역 변수 `-gv_firstMinigame=<이름>`(첫 씬 `gv_firstScene` 보다 위 — `enterFirstMinigame`).
 *          전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(`AutomationSession::startStage`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskFuture.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"

namespace sw
{
    class GameFeatureScope;
    class GameInstance;
    class Scene;
    class SubsystemScope;
    struct GamePackConfig;
    enum class SceneLoadKind : uint8; // Engine/Scene/SceneManager.h

    /**
     * @class MinigameFlow
     * @brief 게임 인스턴스 서브시스템입니다(`game::findSubsystem<MinigameFlow>()`). 게임 스레드에서만 부릅니다.
     */
    class SW_GF_API MinigameFlow final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "MinigameFlow";
        /** @brief 미니게임 파일의 팩 안 폴더입니다(`<팩>/minigames/<이름>.minigame.xml`). */
        static constexpr const utf8* kMinigameFolder = "minigames/";
        /** @brief 미니게임 파일의 접미입니다. */
        static constexpr const utf8* kMinigameSuffix = ".minigame.xml";
        /** @brief 돌아올 자리가 있는 동안 쥐는 저장 막기의 까닭입니다. */
        static constexpr const utf8* kSaveBlockReason = "minigame";

        /** @brief @p gameScope 는 기능이 서비스를 거는 게임 범위, @p packConfig 는 미니게임 · 카탈로그 경로의 팩 설정, @p owner 는 돌아올 씬을 찍고 복원하는 인스턴스입니다. 셋 다 이 서브시스템보다 오래 산다. */
        MinigameFlow( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner );
        ~MinigameFlow() override;

        MinigameFlow( const MinigameFlow& )            = delete;
        MinigameFlow& operator=( const MinigameFlow& ) = delete;

        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 씬 로드 받아들임 알림을 겁니다 — 장치 밖 씬 이동을 듣는다. 씬 매니저가 없는 범위(테스트)에서는 걸지 않는다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 미니게임을 끄고 기다리던 진입 · 돌아올 자리를 잊습니다 — 게임 인스턴스 종료(`onShutdown` 뒤)가 부른다. */
        void shutdownSubsystem() override;

        /**
         * @brief 미니게임 @p name 으로 들어갑니다.
         * @details 읽고(`MinigameDefinition::loadFromAsset`) 검사한 뒤(`validate`) 앞 미니게임을 끄고, 씬 전환이고 돌아올 자리가 없으면 활성 씬을 찍고, 새 것을 켜고,
         *          맵을 열거나(`SceneSwitch`) 활성 씬에 하위 레벨로 더한다(`SubLevel` — 맵을 기다리지 않고 바로 켜진 것이다).
         * @return 못 읽거나 검사에서 지거나, 활성 씬이 없는데 `SubLevel` 이거나, 같은 미니게임이 켜져 있거나, 다른 진입 · 나가기가 맵을 기다리는 중이면 알리고 false 입니다.
         *         활성 씬을 찍지 못하거나 켜기가 지거나 맵 · 하위 레벨 요청이 거절되어도 false 이다. 진입은 원자적이다 — 지면 앞 미니게임을 다시 켜고 여기서 찍은 돌아올 자리를 버린다.
         */
        [[nodiscard]] bool enter( string_view name );
        /**
         * @brief 미니게임을 끄고 돌아올 자리로 갑니다. 하위 레벨이면 빼기만 하고(바탕은 그대로 섰다), 씬 전환이면 돌아올 자리의 씬을 열어 복원하며, 돌아올 자리가 없으면 입구 씬으로 간다.
         * @return 켜진 미니게임과 돌아올 자리가 모두 없거나, 다른 진입 · 나가기가 맵을 기다리는 중이거나, 씬 요청이 거절되면 알리고 false 입니다.
         */
        [[nodiscard]] bool leave();
        /**
         * @brief 틱 뒤 작업(트리거)에서 진입을 맡깁니다 — 다음 `update`(틱 밖)가 `enter` 합니다. 이미 맡긴 것이 있으면 경고하고 이것을 버립니다.
         * @details 씬 전환의 스냅숏은 씬을 도는 동안 찍지 않는다(자동 저장과 같은 자리 — `GameInstance::update`).
         */
        void requestEnter( string_view name );
        /** @brief 켜진 범위의 다시 읽힌 카탈로그를 새 판으로 걸고(`GameFeatureScope::rebindReloadedCatalogs`) 맡긴 진입을 처리합니다 — `GameInstance::update` 앞머리(틱 밖)가 부른다. */
        void update();
        /**
         * @brief 실행 전역 변수 `gv_firstMinigame` 가 있으면 그 미니게임으로 들어갑니다 — 게임 인스턴스의 첫 씬 자리(`GameInstance::startFirstScene`)가 부른다.
         * @return 변수가 있으면(들어가기가 졌어도 — 다른 씬으로 대신하지 않는다) true, 비었으면 false 입니다.
         */
        bool enterFirstMinigame();

        /** @brief 켜진 미니게임입니다. 없으면 nullptr 입니다. */
        const MinigameDefinition* findActiveDefinition() const;
        /** @brief 돌아올 자리(찍어 둔 씬)가 있으면 true 입니다. */
        bool hasSuspendedScene() const { return _pSuspendedScene != nullptr; }
        /** @brief 켰고 미니게임 맵이 서기를 기다리는 중이면 true 입니다. */
        bool isEntering() const { return _bEntering == SW_TRUE; }
        /** @brief 나가며 돌아갈 씬이 서기를 기다리는 중이면 true 입니다. */
        bool isLeaving() const { return _bLeaving == SW_TRUE; }
        /** @brief 이 인스턴스에서 끈 범위들이 남긴 것(해제 검사)의 합입니다. 감시 값 `Minigame.LeakCount` 가 읽는다. */
        uint32 getLeakCount() const { return _leakCount; }
        /** @brief 씬 전환 미니게임에서 나와 돌아올 자리를 복원한 수입니다. 감시 값 `Minigame.RestoreCount` 가 읽는다. */
        uint32 getRestoreCount() const { return _restoreCount; }

        /** @brief 이름 @p name 의 미니게임 파일 리소스 id 입니다(`<팩>/minigames/<이름>.minigame.xml`). */
        static string makeAssetPath( const GamePackConfig& packConfig, string_view name );

    private:
        /** @brief 씬 전환 미니게임에서 돌아올 자리입니다 — 들어갈 때의 활성 씬과 그 스냅숏, 그동안 쥐는 저장 막기. */
        struct SuspendedScene
        {
            string        _scenePath; ///< 다시 열 씬(리소스 id)
            vector<uint8> _snapshot;  ///< 찍은 씬(`SerializePurpose::Snapshot`)
            Subscription  _saveBlock; ///< 돌아올 때까지 저장을 막는다(`kSaveBlockReason`) — 자리가 사라지면 풀린다
        };

        /** @brief 교체로 끈 앞 미니게임입니다 — 새 진입이 지면 다시 켠다(원자적 진입). */
        struct PreviousMinigame
        {
            MinigameDefinition _definition;        ///< 앞 미니게임
            string             _subLevelScenePath; ///< 앞 하위 레벨을 더한 바탕 씬(하위 레벨이 아니면 빈 글)
        };

        /** @brief 진입한 미니게임 맵이 섰습니다(`GameSceneFlow::requestSceneThen`). @p pScene 이 nullptr 이면 실패로 알리고 켠 것을 끈다. */
        void onMapLoaded( Scene* pScene );
        /** @brief 나가며 돌아갈 씬(돌아올 자리 또는 입구)이 섰습니다. 찍어 둔 씬이 있으면 복원합니다. */
        void onReturnLoaded( Scene* pScene );
        /** @brief 씬 로드 받아들임 알림입니다. 이 흐름이 요청하지 않은 게임 이동이면 돌아올 자리와 미니게임을 비웁니다(`clearForOutsideTravel`). */
        void onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future );
        /** @brief 장치 밖 씬 이동 — 기다리던 진입 · 나가기, 돌아올 자리, 켜진 미니게임을 모두 버린다(맵은 바꾸지 않는다 — 이동이 이미 들어왔다). */
        void clearForOutsideTravel( const string& scenePath );
        /** @brief @p scenePath 를 이 흐름의 요청으로 맡깁니다(받아들임 알림이 장치 밖 이동으로 보지 않게). */
        [[nodiscard]] bool requestScene( const string& scenePath, const Delegate<void( Scene* pScene )>& onLoaded );
        /** @brief @p name 의 정의를 읽고 이름 · 참조를 봅니다. 지면 알리고 false 입니다. */
        [[nodiscard]] bool loadDefinition( string_view name, MinigameDefinition& outDefinition ) const;
        /** @brief @p definition 을 켭니다 — 게임 기능(적은 순서) → 카탈로그 맞춤 → 입력 문맥 → UI. 하나라도 지면 그때까지 건 것을 거두고(해제 검사 포함) nullptr 입니다. */
        [[nodiscard]] unique_ptr<GameFeatureScope> activate( const MinigameDefinition& definition );
        /** @brief 활성 씬을 돌아올 자리로 찍습니다 — 찍기 직전에 자동 저장을 한 번 맡기고, 찍은 뒤 저장을 막는다. 활성 씬이 없으면 할 일이 없다(true). 찍지 못하면 알리고 false 입니다. */
        [[nodiscard]] bool suspendActiveScene( string_view name );
        /** @brief 켜진 미니게임을 끕니다(하위 레벨이면 바탕 씬에서 뺀다). 없으면 할 일이 없다. 맵은 바꾸지 않는다. */
        void deactivateActive();
        /** @brief 켜진 미니게임의 범위만 끕니다(하위 레벨은 그대로 둔다 — 교체 · 되돌리기용). */
        void deactivateScope();
        /** @brief @p definition 의 하위 레벨을 바탕 씬 @p scenePath 가 아직 활성이면 뺍니다. 하위 레벨이 아니면 할 일이 없다. */
        static void removeSubLevel( const MinigameDefinition& definition, const string& scenePath );
        /** @brief 교체를 위해 켜진 미니게임의 범위를 끄고 그 정의를 돌려줍니다(하위 레벨은 남긴다). 없으면 nullopt 입니다. */
        [[nodiscard]] optional<PreviousMinigame> takeActiveForReplace();
        /** @brief 진입이 졌을 때 @p previous 를 다시 켭니다(하위 레벨은 남아 있다). 다시 켜지 못하면 하위 레벨까지 거둔다. 부른 뒤 @p previous 는 비었다. */
        void restorePrevious( optional<PreviousMinigame>& previous );
        /** @brief 다른 진입 · 나가기가 맵을 기다리는 중이면 @p name 의 요청을 거절한다고 알리고 true 입니다. */
        bool rejectIfLoadPending( string_view name ) const;
        /** @brief 입력 맵 레이어를 스택에 넣어 켜고(`InputMap::pushLayer`) 끌 때 빼서 넣기 전의 켬으로 되돌립니다(`popLayer` — 한 쌍). 없는 레이어 · 입력 서비스가 없는 호스트에 적은 레이어는 오류입니다. */
        [[nodiscard]] static bool pushInputLayers( GameFeatureScope& scope, const MinigameDefinition& definition );

    private:
        SubsystemScope&              _gameScope;            ///< 기능이 서비스를 거는 게임 범위
        const GamePackConfig&        _packConfig;           ///< 미니게임 · 카탈로그 경로의 팩 설정
        GameInstance&                _owner;                ///< 돌아올 씬을 찍고 복원하는 인스턴스
        unique_ptr<GameFeatureScope> _pActive;              ///< 켜진 미니게임의 범위(없으면 nullptr)
        MinigameDefinition           _activeDefinition;     ///< 켜진 미니게임(`_pActive` 가 있을 때만 뜻이 있다)
        string                       _subLevelScenePath;    ///< 켜진 하위 레벨 미니게임을 더한 바탕 씬(리소스 id — 비었으면 없음)
        unique_ptr<SuspendedScene>   _pSuspendedScene;      ///< 씬 전환 미니게임에서 돌아올 자리(없으면 nullptr)
        string                       _pendingEnter;         ///< 틱 뒤 작업에서 맡긴 진입(`requestEnter`) — 비었으면 없음
        Subscription                 _sceneLoadRequested;   ///< 씬 로드 받아들임 알림의 구독(초기화가 걸고 종료가 푼다)
        uint32                       _leakCount;            ///< 끈 범위들이 남긴 것의 합
        uint32                       _restoreCount;         ///< 돌아올 자리 복원 수
        uint8                        _bEntering        : 1; ///< 켠 미니게임의 맵을 기다리는 중이면 1(하나만 — 겹친 진입은 거절한다)
        uint8                        _bLeaving         : 1; ///< 돌아갈 씬을 기다리는 중이면 1
        uint8                        _bRequestingScene : 1; ///< 이 흐름이 씬 로드를 요청하는 동안 1(받아들임 알림이 동기로 온다)
    };
} // namespace sw
