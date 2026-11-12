/**
 * @file GameSceneFlow.h
 * @brief 게임 인스턴스의 씬 흐름입니다 — gamesettings 의 씬 칸(첫 씬 · 입구 · 타이틀) 요청, 맡긴 씬 로드의 끝 알림, 로딩 화면 · 페이드 진행, "타이틀로" 명령.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Task/TaskFuture.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"

namespace sw
{
    struct GameSettings;

    class ILoadingScreen;
    class SaveGate;
    class Scene;
    class ScreenTransitionManager;
    class Widget;
    enum class SceneLoadKind : uint8; // Engine/Scene/SceneManager.h

    /**
     * @class GameSceneFlow
     * @brief 게임 인스턴스 서브시스템입니다(`game::findSubsystem<GameSceneFlow>()` · 인스턴스의 `getSubsystem<GameSceneFlow>()`). 언리얼 `UGameInstance` 의 맵 이동 · 로딩 화면 연결 자리입니다.
     * @details `GameInstance` 가 생성자에서 자기 gamesettings · 화면 전환 · 로딩 화면을 넘겨 만들고, 초기화(`onInitialize` 앞)에서 씬 로드 받아들임 알림을 건다 —
     *          누가 씬 로드를 요청했든(게임 · 시나리오 · 콘솔) 게임 이동이면 로딩 화면을 띄우고 `SceneLoadRequestedEvent` 를 내며, 끝나면 `SceneLoadCompletedEvent` 를 낸다.
     *          프레임마다 인스턴스가 `update` 를 부른다(넷 세션 · 파생 프레임 앞). 게임 스레드에서만 쓴다.
     */
    class SW_GF_API GameSceneFlow final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameSceneFlow";

        /** @brief 맡긴 씬 로드의 끝을 받는 함수입니다 — 활성이 된 씬, 실패 · 뒤 요청에 밀렸으면 nullptr. */
        using SceneLoadedFunction = Delegate<void( Scene* pScene )>;

        /**
         * @brief @p settings 는 게임 인스턴스의 gamesettings 이고 이 서브시스템보다 오래 산다. 화면 전환 · 로딩 화면은 같은 게임 범위의 형제다(먼저 선다). 로딩 화면은 없을 수 있다(전용 서버 — `IGameInstanceClientHook` 이 없다).
         * @param saveGate 게임 인스턴스의 세이브 막기 문(빌림 — 이 범위보다 먼저 서고 늦게 진다). 맡긴 씬 로드마다 막기를 든다.
         */
        GameSceneFlow( const GameSettings& settings, ScreenTransitionManager& screenTransition, ILoadingScreen* pLoadingScreen, SaveGate& saveGate );
        ~GameSceneFlow() override;

        GameSceneFlow( const GameSceneFlow& )            = delete;
        GameSceneFlow& operator=( const GameSceneFlow& ) = delete;

        /**
         * @brief 다시 선 인스턴스인지(활성 씬이 이미 있다) 정하고, 씬 로드 받아들임 알림(`SceneManager::onSceneLoadRequested`)을 겁니다 — 첫 씬 요청(`onInitialize` 안) 앞이다.
         * @details 언리얼 로딩 화면 모듈이 PreLoadMap 을 구독하는 것과 같다. 씬 매니저가 없는 게임 범위(테스트)에서는 알림을 걸지 않는다.
         */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 받아들임 알림을 풀고 맡긴 로드를 잊습니다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 화면 전환 · 로딩 화면이 먼저 선다(받아들임 알림이 로딩 화면을 띄운다). */
        span<const utf8* const> getSubsystemDependencies() const override;

        /**
         * @brief 한 프레임을 넘깁니다 — 화면 전환(페이드)을 진행하고, 로딩 화면을 갱신하고, 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 냅니다(요청 순서).
         * @details 맡긴 씬 로드가 남았거나 씬 매니저가 전환 중이면 로딩 중으로 본다. 페이드와 로딩 화면은 실제 시간(`GameTimeScale::getUnscaledDeltaTime`)으로 돈다 —
         *          게임 시간으로 돌리면 정지 메뉴(게임 시간 0)가 뜬 동안 페이드가 반쯤 검은 채로 멈춘다.
         */
        void update();

        /** @brief 게임이 처음 여는 씬입니다 — `-gv_firstScene` > 타이틀 씬 > 시작 맵(gamesettings). 모두 비었으면 빈 문자열입니다. */
        const string& getFirstScene() const;
        /** @brief 타이틀 다음에 여는 씬입니다 — 입구 씬 > 시작 맵. */
        const string& getEntranceScene() const;
        /**
         * @brief `getFirstScene()` 의 로드를 요청합니다. 열 씬이 없거나 요청이 실패하면 false 입니다(실패는 알립니다).
         * @details 요청이 들어가면 `SceneLoadRequestedEvent` 를 내고, 그 로드가 끝난(활성 씬이 됐거나 실패한) 뒤의 첫 `update` 가 `SceneLoadCompletedEvent` 를 냅니다.
         *          인스턴스가 살아 있는 월드 위에 다시 섰으면(게임 모듈 핫 리로드 · 백엔드 교체 — 초기화 때 이미 활성 씬이 있었다) 요청하지 않고 false 다.
         *          그 씬은 곧 스냅샷으로 되살아나거나 에디터가 연 씬이다. 언리얼의 핫 리로드도 `GameInstance::Init` 을 다시 부르지 않는다.
         */
        [[nodiscard]] bool requestFirstScene();
        /** @brief `getEntranceScene()` 의 로드를 요청합니다. 타이틀 화면이 "시작" 에서 부릅니다. 레벨 이벤트는 `requestFirstScene` 과 같습니다. */
        [[nodiscard]] bool requestEntranceScene();
        /**
         * @brief gamesettings 의 타이틀 씬(`titleScene`) 로드를 요청합니다. 타이틀 씬이 없거나 요청이 실패하면 false 입니다. 레벨 이벤트는 `requestFirstScene` 과 같습니다.
         * @details 타이틀 씬이 있는 게임이면 `registerSessionCommands` 가 UI 전역 명령 `ReturnToTitle` 로 이 함수를 등록해 일시정지 메뉴에 "타이틀로" 가 보인다
         *          (언리얼 Lyra 의 "Quit to Main Menu", 유니티 템플릿의 메뉴 씬 로드). 없는 게임은 등록하지 않아 단추가 접힌 채다.
         */
        [[nodiscard]] bool requestTitleScene();

        /**
         * @brief @p scenePath 의 로드를 게임 이동으로 요청하고, 끝나면 그 로드의 `SceneLoadCompletedEvent` 바로 뒤에 @p onLoaded 를 부릅니다. 요청이 실패하면 false 이고 @p onLoaded 는 불리지 않습니다.
         * @details 세이브 불러오기(찍은 씬으로 옮긴 뒤 적용)와 깨진 복원 뒤 씬 다시 열기가 쓴다. 이벤트 버스가 없는 범위(도구 · 테스트)에서도 끝을 잃지 않게 이벤트 구독이 아니라
         *          그 로드에 묶인 함수로 받는다(유니티 `LoadSceneAsync(...).completed`, 언리얼은 `OpenLevel` 뒤 `PostLoadMapWithWorld` 구독).
         */
        [[nodiscard]] bool requestSceneThen( const string& scenePath, const SceneLoadedFunction& onLoaded );

        /** @brief 타이틀 씬이 있는 게임이면 true 입니다 — 세션 명령(타이틀로)을 걸지 정한다. */
        bool hasTitleScene() const;
        /**
         * @brief 세션 명령(UI 전역 명령 `ReturnToTitle`)의 등록 구독을 맡깁니다. `unregisterSessionCommands` 가 거둡니다.
         * @details 거는 일은 클라이언트 전용 부분(`IGameInstanceClientHook::bindSessionCommands`)이 한다 — 씬 흐름은 UI 를 모른다. 게임 인스턴스가 종료의 맨 앞에서 거둔다.
         */
        void holdSessionCommand( Subscription registration );
        void unregisterSessionCommands();
        /** @brief UI 전역 명령 `ReturnToTitle` 의 처리 함수입니다(일시정지 메뉴의 "타이틀로"). 클라이언트 전용 부분이 이 함수를 명령에 건다. */
        void onReturnToTitleCommand( const hashed_string& command, Widget& source );

    private:
        /** @brief 맡긴 씬 로드 하나입니다. 끝나면 `update` 가 `SceneLoadCompletedEvent` 를 내고 목록에서 뺍니다. */
        struct PendingSceneLoad
        {
            string              _scenePath;
            TaskFuture<Scene*>  _future;
            Subscription        _saveBlock; ///< 로드가 끝날 때까지 세이브를 막는다 — 반쯤 선 씬 · 옛 씬을 새 씬 이름으로 쓰지 않게 한다(목록에서 빠지면 풀린다)
            SceneLoadedFunction _onLoaded;  ///< 끝을 받을 함수(`requestSceneThen`) — 묶이지 않았으면 이벤트만 낸다
        };

        /**
         * @brief @p scenePath 의 로드를 `SceneManager` 에 게임 이동으로 맡깁니다. 로딩 화면 · `SceneLoadRequestedEvent` 는 `onSceneLoadRequested` 가 붙인다.
         * @return 경로가 비었으면 조용히 false, 맡기지 못했으면 알리고 false 입니다(그때는 이벤트도 없습니다).
         */
        [[nodiscard]] bool requestLoadScene( const string& scenePath, const utf8* pWhich );
        /** @brief 씬 로드 받아들임 알림입니다. 게임 이동(`SceneLoadKind::Travel`)이면 대기 목록에 넣고 로딩 화면을 띄우고 `SceneLoadRequestedEvent` 를 냅니다. */
        void onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future );
        /** @brief 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 내고 묶인 함수를 부릅니다(요청 순서). 받는 쪽이 새 로드를 요청해도 된다 — 끝난 것을 먼저 목록에서 뺀 뒤 알린다. */
        void publishFinishedSceneLoads();

    private:
        const GameSettings&      _settings;                       ///< 게임 인스턴스의 gamesettings(빌림 — 인스턴스가 이 범위보다 오래 든다)
        ScreenTransitionManager& _screenTransition;               ///< 같은 게임 범위의 화면 전환(빌림)
        ILoadingScreen*          _pLoadingScreen;                 ///< 같은 게임 범위의 로딩 화면(빌림, 없으면 nullptr — 전용 서버)
        SaveGate&                _saveGate;                       ///< 게임 인스턴스의 세이브 막기 문(빌림)
        vector<PendingSceneLoad> _listPendingSceneLoad;           ///< 맡겼지만 아직 끝을 알리지 않은 씬 로드
        Subscription             _sceneLoadRequestedSubscription; ///< 씬 로드 받아들임 알림의 구독(초기화가 걸고 종료가 푼다)
        Subscription             _returnToTitleRegistration;      ///< UI 전역 명령 ReturnToTitle 의 등록 구독(`holdSessionCommand` 가 받는다)
        uint8                    _bResumingWorld;                 ///< 초기화 때 이미 활성 씬이 있었다(다시 선 인스턴스) — 첫 씬을 요청하지 않는다
    };
} // namespace sw
