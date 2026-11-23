/**
 * @file MinigameFlow.h
 * @brief 게임 인스턴스의 미니게임 흐름입니다 — 미니게임 정의를 읽고 검사해 켜고(게임 기능 · 카탈로그 · 입력 문맥 · UI) 그 맵으로 들어가며, 나올 때 끄고 들어오기 전 씬으로 돌아가 결과를 넘깁니다.
 * @details 형제 `GameSceneFlow` · `RespawnFlow` 와 같은 줄의 게임 인스턴스 서브시스템이다. 용과 같이 · GTA · RDR2 의 가게 · 오락실 미니게임, 마리오 파티의 연속 미니게임,
 *          언리얼 Lyra 의 `ULyraExperienceManagerComponent`(Experience 를 올리고 내린다)와 같은 자리다. 형제 서브시스템(`OnlineSession`)처럼 늘 서고, 미니게임을 쓰지 않는 게임에서는 아무 일도 안 한다.
 *          게임 범위 기능(gamesettings `gameFeatures`)은 게임 인스턴스가 이 흐름보다 먼저 켜 둔다 — 미니게임 범위의 해제 검사는 그것이 켜진 채로 찍어 그 몫을 남은 것으로 세지 않는다.
 *          칸 스택(`MinigameStack`): `enter` 는 쌓고(미니게임 안의 미니게임 — 덮인 미니게임의 범위는 끄고 돌아올 때 씬 복원 **전에** 다시 켠다, 컴포넌트가 `beginPlay` 에서 서비스를 찾는다),
 *          `switchTo` 는 맨 위를 바꾼다(돌아올 자리를 그대로 두고 다음 씬 전환 미니게임으로 — 마리오 파티식). 깊이 한도는 엔진 설정 `_minigame._stackDepthLimit`(없으면 진입이 진다).
 *          켜기는 맵을 요청하기 전이다 — 맵의 컴포넌트는 씬이 적용될 때(`beginPlay`, 맵 로드 완료 알림보다 앞) 기능 서비스 · 카탈로그를 찾는다. 진입은 원자적이다 — 지면 덮었던 미니게임을 다시 켠다.
 *          들어가기: `SceneSwitch` 는 들어갈 때의 활성 씬을 돌아올 자리(씬 경로 + `GameStateSnapshot` 목적 `Snapshot`)로 찍고 맵을 바꾼다. 찍기 직전에 자동 저장을 한 번 맡긴다.
 *          저장: 정의가 허락하면(`MinigameSavePolicy::Allowed`) 세이브가 칸 스택(미니게임 이름 · 덮인 씬의 세이브 봉투)을 머리에 담고, 불러오면 그 미니게임을 켠 뒤 그 맵을 열어 되살린다
 *          (`travelToSavedMinigame` — 언리얼이 세이브의 맵으로 `OpenLevel` 할 때 URL 로 경험 · 게임 모드를 함께 싣는 자리). 막는 미니게임의 칸은 있는 동안 저장을 막는다(`SaveLock` 까닭 `minigame`).
 *          나올 때 그 씬을 연 뒤 복원하고 결과(`MinigameResult`)를 넘긴다. 돌아올 씬이 없으면(첫 진입이 미니게임 — `gv_firstMinigame`) gamesettings 입구 씬으로 간다.
 *          넷 세션이 열린 인스턴스에서 씬 전환 진입은 오류로 거절한다(세션의 모든 피어가 같은 씬을 돌아야 한다 — 세션 이동 갈래는 돌아올 자리 형식에 더할 자리만 둔다).
 *          `SubLevel` 은 들어갈 때의 활성 씬에 맵을 하위 레벨(이름 = 미니게임 이름)로 더하고 나올 때 뺀다. 자리는 진입을 건 사물(트리거)의 변환이다 — 언리얼 레벨 인스턴스 액터 자리,
 *          얹을 자리 없이는 들어가지 않는다(원점에 엉뚱하게 얹히지 않게). 바탕 틱은 멈추지 않는다 — 미니게임 입력은 `inputContexts` 레이어가 위에서 가린다.
 *          장치 밖 씬 이동(세이브 불러오기 · 타이틀로 · 시나리오의 씬 열기)이 오면 칸 스택과 미니게임 범위를 비운다 — 씬 매니저의 받아들임 알림으로 이 흐름이 요청하지 않은 게임 이동을 듣는다.
 *          진입: 개발 명령 `minigame.enter <이름> [얹을 사물]` · `minigame.switch <이름>` · `minigame.leave [결과]`, 사물의 `MinigameTriggerComponent`(틱 뒤에 맡기고 `update` 가
 *          틱 밖에서 처리 — 스냅숏은 씬을 도는 동안 찍지 않는다), 실행 전역 변수 `-gv_firstMinigame=<이름>`(첫 씬 `gv_firstScene` 보다 위 — `enterFirstMinigame`).
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

#include "Engine/Scene/SceneSubLevelTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
    class GameFeatureScope;
    class GameInstance;
    class GameObject;
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

        /** @brief 미니게임이 끝나 결과를 넘길 때 부르는 함수 목록입니다(돌아올 자리를 복원한 뒤). */
        using FinishedDelegate = MulticastDelegate<void( const MinigameResult& result )>;

        /** @brief @p gameScope 는 기능이 서비스를 거는 게임 범위, @p packConfig 는 미니게임 · 카탈로그 경로의 팩 설정, @p owner 는 돌아올 씬을 찍고 복원하는 인스턴스입니다. 셋 다 이 서브시스템보다 오래 산다. */
        MinigameFlow( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner );
        ~MinigameFlow() override;

        MinigameFlow( const MinigameFlow& )            = delete;
        MinigameFlow& operator=( const MinigameFlow& ) = delete;

        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 씬 로드 받아들임 알림을 겁니다 — 장치 밖 씬 이동을 듣는다. 씬 매니저가 없는 범위(테스트)에서는 걸지 않는다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 미니게임을 끄고 기다리던 진입 · 칸 스택을 잊습니다 — 게임 인스턴스 종료(`onShutdown` 뒤)가 부른다. */
        void shutdownSubsystem() override;

        /**
         * @brief 미니게임 @p name 을 맨 위에 쌓아 들어갑니다(켜진 미니게임이 있으면 그 범위를 끄고 덮는다).
         * @param placement `SubLevel` 을 얹을 자리(진입을 건 사물의 변환). `SubLevel` 은 반드시 있어야 하고, `SceneSwitch` 는 보지 않는다.
         * @return 못 읽거나 검사에서 지거나, 넷 세션이 열렸는데 `SceneSwitch` 이거나, `SubLevel` 인데 활성 씬 · 자리가 없거나, 맨 위와 같은 미니게임이거나, 깊이 한도에 닿았거나,
         *         다른 진입 · 나가기가 맵을 기다리는 중이면 알리고 false 입니다. 찍기 · 켜기 · 맵 · 하위 레벨 요청이 져도 false 이고, 그때는 덮었던 미니게임을 다시 켠다(원자적).
         */
        [[nodiscard]] bool enter( string_view name, const optional<SubLevelPlacement>& placement = nullopt );
        /**
         * @brief 맨 위 미니게임을 @p name 으로 바꿉니다 — 돌아올 자리는 그대로다(마리오 파티식 연속). 칸이 없으면 `enter` 와 같다.
         * @return 맨 위나 새 것이 `SubLevel` 이면(얹은 씬이 바뀌는 교체는 나간 뒤 들어간다) 알리고 false 입니다. 나머지 거절은 `enter` 와 같고, 지면 앞 미니게임을 다시 켠다(원자적).
         */
        [[nodiscard]] bool switchTo( string_view name );
        /**
         * @brief 맨 위 미니게임을 끄고 그 돌아올 자리로 갑니다 — 하위 레벨이면 빼고, 씬 전환이면 찍은 씬을 열어 복원하며, 돌아올 씬이 없으면 입구 씬으로 간다.
         *        덮였던 미니게임은 씬 복원 전에 다시 켜고, 결과(@p resultName)는 복원 뒤에 `onFinished` 로 넘긴다.
         * @return 칸이 없거나, 다른 진입 · 나가기가 맵을 기다리는 중이거나, 씬 요청이 거절되면 알리고 false 입니다(거절이면 칸은 그대로다).
         */
        [[nodiscard]] bool leave( const hashed_string& resultName = hashed_string{}, vector<MinigameResultValue> listValue = {} );
        /** @brief 세이브 봉투의 머리에 실을 칸 스택을 씁니다(`MinigameStack::writeSaveState`). 미니게임 밖이면 빈 바이트입니다. */
        void writeSaveState( vector<uint8>& outBytes ) const { _stack.writeSaveState( outBytes ); }
        /** @brief 지금 켜진 칸이 세이브의 @p listFrame 과 같고 기다리는 맵이 없으면 true 입니다 — 씬을 다시 열지 않고 되살린다. */
        bool isInSavedMinigame( const vector<MinigameSavedFrame>& listFrame ) const;
        /** @brief 같은 미니게임 안에서 세이브를 되살린 뒤 칸의 돌아올 자리를 세이브의 것으로 바꿉니다(`isInSavedMinigame` 인 목록만). */
        void applySavedMinigame( vector<MinigameSavedFrame> listFrame );
        /**
         * @brief 세이브의 칸 스택(@p listFrame, 비지 않는다)으로 바꾸고 맨 위 미니게임을 켠 뒤 세이브의 씬 @p scenePath 를 엽니다. 씬이 선 뒤(졌으면 nullptr) @p onLoaded 를 부릅니다.
         * @return 정의를 읽지 못하거나 검사 · 깊이 한도 · 넷 세션에 걸리거나, 다른 진입 · 나가기가 맵을 기다리거나, 켜기 · 씬 요청이 지면 알리고 false 입니다 — 지금 칸은 그대로다.
         */
        [[nodiscard]] bool travelToSavedMinigame( vector<MinigameSavedFrame> listFrame, const string& scenePath, const Delegate<void( Scene* pScene )>& onLoaded );
        /** @brief @p scenePath 를 이 흐름의 요청으로 맡깁니다 — 칸 스택을 둔 채 씬을 다시 연다(받아들임 알림이 장치 밖 이동으로 보지 않게). */
        [[nodiscard]] bool requestScene( const string& scenePath, const Delegate<void( Scene* pScene )>& onLoaded );
        /**
         * @brief 틱 뒤 작업(디렉터의 `finishMinigame`)에서 나가기를 맡깁니다 — 다음 `update`(틱 밖)가 `leave` 합니다. 맡긴 나가기가 이미 있으면 경고하고 이것을 버립니다.
         * @details 하위 레벨 나가기는 그 자리에서 하위 레벨을 빼므로 그 안 디렉터의 규칙 중에 부르면 자기를 지운다 — 진입(`requestEnter`)과 같은 줄로 미룬다.
         */
        void requestLeave( const hashed_string& resultName, vector<MinigameResultValue> listValue );
        /**
         * @brief 틱 뒤 작업(트리거)에서 진입을 맡깁니다 — 다음 `update`(틱 밖)가 `enter` 합니다. 이미 맡긴 것이 있으면 경고하고 이것을 버립니다.
         * @details 씬 전환의 스냅숏은 씬을 도는 동안 찍지 않는다(자동 저장과 같은 자리 — `GameInstance::update`).
         */
        void requestEnter( string_view name, const optional<SubLevelPlacement>& placement );
        /**
         * @brief 미니게임 @p name 의 맵이 쓸 파일을 들어가기 전에 미리 읽습니다(`SceneManager::preloadScene` — 하나만 두고, 들어가면 로드가 이어받는다). 게임 상태는 바꾸지 않는다.
         * @return 정의를 읽었으면 true 입니다(쿠킹 전 Dev 실행은 프리로드 목록이 없어 읽을 것이 없다). 정의를 읽지 못했거나 씬 매니저가 없으면 false 입니다.
         */
        [[nodiscard]] bool preload( string_view name );
        /** @brief 켜진 범위의 다시 읽힌 카탈로그를 새 판으로 걸고(`GameFeatureScope::rebindReloadedCatalogs`) 맡긴 나가기 · 진입을 처리합니다 — `GameInstance::update` 앞머리(틱 밖)가 부른다. */
        void update();
        /**
         * @brief 실행 전역 변수 `gv_firstMinigame` 가 있으면 그 미니게임으로 들어갑니다 — 게임 인스턴스의 첫 씬 자리(`GameInstance::startFirstScene`)가 부른다.
         * @return 변수가 있으면(들어가기가 졌어도 — 다른 씬으로 대신하지 않는다) true, 비었으면 false 입니다.
         */
        bool enterFirstMinigame();

        /** @brief 미니게임이 끝날 때(돌아올 자리를 복원한 뒤) 결과를 받는 함수 목록입니다. 구독은 돌려받은 `Subscription` 이 풉니다. */
        FinishedDelegate& onFinished() { return _onFinished; }

        /** @brief 켜진(맨 위) 미니게임입니다. 없거나 덮인 것을 다시 켜지 못했으면 nullptr 입니다. */
        const MinigameDefinition* findActiveDefinition() const;
        /** @brief 칸 스택입니다(읽기). */
        const MinigameStack& getStack() const { return _stack; }
        /** @brief 켰고 미니게임 맵이 서기를 기다리는 중이면 true 입니다. */
        bool isEntering() const { return _bEntering == SW_TRUE; }
        /** @brief 나가며 돌아갈 씬이 서기를 기다리는 중이면 true 입니다. */
        bool isLeaving() const { return _pReturning != nullptr; }
        /** @brief 이 인스턴스에서 끈 범위들이 남긴 것(해제 검사)의 합입니다. 감시 값 `Minigame.LeakCount` 가 읽는다. */
        uint32 getLeakCount() const { return _leakCount; }
        /** @brief 씬 전환 미니게임에서 나와 돌아올 자리를 복원한 수입니다. 감시 값 `Minigame.RestoreCount` 가 읽는다. */
        uint32 getRestoreCount() const { return _restoreCount; }
        /** @brief 마지막으로 넘긴 결과입니다. 없으면 빈 값입니다. 감시 값 `Minigame.LastResult` 이 읽는다. */
        const MinigameResult& getLastResult() const { return _lastResult; }

        /** @brief 이름 @p name 의 미니게임 파일 리소스 id 입니다(`<팩>/minigames/<이름>.minigame.xml`). */
        static string makeAssetPath( const GamePackConfig& packConfig, string_view name );
        /**
         * @brief 팩의 미니게임 정의 가운데 허브 입구 자리(`portalSlot`)를 적은 것을 자리 순으로 @p outListPortal 에 모읍니다(먼저 비운다) — `MinigamePortalSpawnerComponent` 가 부른다.
         * @details 파일마다 들어갈 때와 같은 읽기 · 검사를 한다. 못 읽은 정의와 자리가 겹친 뒤의 정의는 오류를 남기고 빼며, 나머지 입구는 그대로 선다.
         */
        void collectPortals( vector<MinigameDefinition>& outListPortal ) const;
        /** @brief 하위 레벨을 사물 @p anchor 의 변환(위치는 월드)에 얹는 자리입니다 — 트리거 · 개발 명령이 쓴다. 장면 컴포넌트가 없으면 nullopt 입니다. */
        static optional<SubLevelPlacement> makeAnchorPlacement( const GameObject& anchor );
        /**
         * @brief @p definition 이 지금 인스턴스에서 들어갈 수 있는 방식인지 봅니다 — 넷 세션이 열렸으면(@p bNetSessionActive) 씬 전환은 거절한다(폴백 없음).
         * @return 거절이면 알리고 false 입니다.
         */
        [[nodiscard]] static bool validateEntry( const MinigameDefinition& definition, bool bNetSessionActive );

    private:
        /** @brief 나가며 돌아갈 씬을 기다리는 동안 쥐는 것입니다 — 나간 칸(찍은 씬 · 저장 막기를 복원까지 쥔다)과 넘길 결과. */
        struct PendingReturn
        {
            MinigameFrame  _frame;  ///< 나간 칸
            MinigameResult _result; ///< 복원 뒤에 넘길 결과
        };
        /** @brief 세이브의 미니게임 맵이 서기를 기다리는 동안 쥐는 것입니다(`travelToSavedMinigame`). */
        struct PendingSavedTravel
        {
            MinigameStack                   _previous;     ///< 세이브로 바꾸기 전의 칸 스택 — 맵이 서지 않으면 되돌린다
            Delegate<void( Scene* pScene )> _onLoaded;     ///< 씬이 선 뒤 부를 함수
            uint8                           _bDropped : 1; ///< 장치 밖 이동이 이 여행을 버렸다 — 끝을 알리기만 한다

            PendingSavedTravel( MinigameStack previous, const Delegate<void( Scene* pScene )>& onLoaded )
                : _previous{ std::move( previous ) }
                , _onLoaded{ onLoaded }
                , _bDropped{ SW_FALSE }
            {
            }
        };

        /** @brief 진입한 미니게임 맵이 섰습니다(`GameSceneFlow::requestSceneThen`). @p pScene 이 nullptr 이면 실패로 알리고 그 칸을 꺼낸다. */
        void onMapLoaded( Scene* pScene );
        /** @brief 세이브의 미니게임 맵이 섰습니다(`travelToSavedMinigame`). 졌으면 바꾸기 전 칸 스택을 되돌립니다. */
        void onSavedMapLoaded( Scene* pScene );
        /** @brief 나가며 돌아갈 씬(찍은 씬 또는 입구)이 섰습니다. 찍어 둔 씬이 있으면 복원하고 결과를 넘깁니다. */
        void onReturnLoaded( Scene* pScene );
        /** @brief 씬 로드 받아들임 알림입니다. 이 흐름이 요청하지 않은 게임 이동이면 칸 스택과 미니게임을 비웁니다(`clearForOutsideTravel`). */
        void onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future );
        /** @brief 장치 밖 씬 이동 — 기다리던 진입 · 나가기, 칸 스택, 켜진 미니게임을 모두 버린다(맵은 바꾸지 않는다 — 이동이 이미 들어왔다). */
        void clearForOutsideTravel( const string& scenePath );
        /** @brief @p name 의 정의를 읽고 이름 · 참조를 봅니다. 지면 알리고 false 입니다. */
        [[nodiscard]] bool loadDefinition( string_view name, MinigameDefinition& outDefinition ) const;
        /** @brief 엔진 설정의 쌓기 깊이 한도를 보고 하나 더 쌓을 수 있으면 true 입니다. 한도가 없거나 닿았으면 알리고 false 입니다. */
        [[nodiscard]] bool validateDepth( string_view name ) const;
        /** @brief @p definition 을 켭니다 — 게임 기능(적은 순서) → 카탈로그 맞춤 → 입력 문맥 → UI. 하나라도 지면 그때까지 건 것을 거두고(해제 검사 포함) nullptr 입니다. */
        [[nodiscard]] unique_ptr<GameFeatureScope> activate( const MinigameDefinition& definition );
        /** @brief 씬 전환 칸의 돌아올 자리를 만듭니다 — 활성 씬이 있으면 자동 저장을 한 번 맡기고 찍는다(저장을 허락하는 미니게임이면 세이브 봉투도), 없으면 입구 씬. 찍지 못하면 알리고 false 입니다. */
        [[nodiscard]] bool makeSceneReturn( const MinigameDefinition& definition, MinigameReturnPoint& outReturnPoint );
        /** @brief 맨 위 칸의 미니게임을 켭니다(덮였다 돌아올 때 · 진입이 져 되돌릴 때). 칸이 없으면 할 일이 없다. 켜지 못하면 알리고 켜진 미니게임 없이 둔다. */
        void activateTop();
        /** @brief 켜진 미니게임의 범위를 끕니다(칸은 그대로 둔다). 없으면 할 일이 없다. */
        void deactivateScope();
        /** @brief 하위 레벨 @p returnPoint 를 바탕 씬이 아직 활성이면 뺍니다. */
        static void removeSubLevel( const MinigameSubLevelReturn& returnPoint );
        /** @brief 결과를 넘깁니다(`onFinished`). */
        void deliverResult( const MinigameResult& result );
        /** @brief 다른 진입 · 나가기가 맵을 기다리는 중이면 @p name 의 요청을 거절한다고 알리고 true 입니다. */
        bool rejectIfLoadPending( string_view name ) const;
        /** @brief 입력 맵 레이어를 스택에 넣어 켜고(`InputMap::pushLayer`) 끌 때 빼서 넣기 전의 켬으로 되돌립니다(`popLayer` — 한 쌍). 없는 레이어 · 입력 서비스가 없는 호스트에 적은 레이어는 오류입니다. */
        [[nodiscard]] static bool pushInputLayers( GameFeatureScope& scope, const MinigameDefinition& definition );
        /** @brief 로컬 멀티 미니게임이면 입력 사용자를 열고 끌 때 닫습니다(레이어를 넣은 뒤 — 사용자마다의 맵이 그 레이어를 베낀다). 입력 서비스가 없는 호스트(전용 서버)는 할 일이 없다. */
        [[nodiscard]] static bool openLocalUsers( GameFeatureScope& scope, const MinigameDefinition& definition );

    private:
        SubsystemScope&                _gameScope;             ///< 기능이 서비스를 거는 게임 범위
        const GamePackConfig&          _packConfig;            ///< 미니게임 · 카탈로그 경로의 팩 설정
        GameInstance&                  _owner;                 ///< 돌아올 씬을 찍고 복원하는 인스턴스
        MinigameStack                  _stack;                 ///< 칸 스택 — 맨 위만 켜져 있다
        unique_ptr<GameFeatureScope>   _pActive;               ///< 맨 위 칸의 범위(없으면 nullptr)
        unique_ptr<PendingReturn>      _pReturning;            ///< 나가며 돌아갈 씬을 기다리는 중이면 그 자리와 결과(없으면 nullptr)
        unique_ptr<PendingSavedTravel> _pSavedTravel;          ///< 세이브의 미니게임 맵을 기다리는 중이면 바꾸기 전 칸 스택(없으면 nullptr)
        string                         _pendingEnter;          ///< 틱 뒤 작업에서 맡긴 진입(`requestEnter`) — 비었으면 없음
        optional<SubLevelPlacement>    _pendingPlacement;      ///< 맡긴 진입의 얹을 자리
        optional<MinigameResult>       _pendingLeave;          ///< 틱 뒤 작업에서 맡긴 나가기(`requestLeave` — 이름 칸은 비워 두고 `leave` 가 채운다). 없으면 nullopt
        FinishedDelegate               _onFinished;            ///< 결과를 받는 함수 목록
        MinigameResult                 _lastResult;            ///< 마지막으로 넘긴 결과
        Subscription                   _sceneLoadRequested;    ///< 씬 로드 받아들임 알림의 구독(초기화가 걸고 종료가 푼다)
        uint32                         _leakCount;             ///< 끈 범위들이 남긴 것의 합
        optional<MinigameResult>       _pendingRestoredResult; ///< 찍은 씬을 되살린 프레임의 결과 — 되살린 오브젝트의 상태 적용 · 시작(onBeginPlay) 뒤인 다음 `update` 에 알린다
        uint32                         _restoreCount;          ///< 돌아올 자리 복원 수
        uint8                          _bEntering        : 1;  ///< 맨 위 미니게임의 맵을 기다리는 중이면 1(하나만 — 겹친 진입은 거절한다)
        uint8                          _bRequestingScene : 1;  ///< 이 흐름이 씬 로드를 요청하는 동안 1(받아들임 알림이 동기로 온다)
    };
} // namespace sw
