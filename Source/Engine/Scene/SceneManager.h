#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskHandleTracker.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Object/GameObject/Service/SceneEngineServices.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class IRHIDevice;
    class PreloadSet;
    class Scene;

    /**
     * @brief 이전 씬의 오브젝트를 가리키는 기록이 무효가 됐다는 알림입니다(활성 씬을 바꿨을 때 · 매니저 종료).
     * @details 오브젝트 포인터 · id 를 붙잡는 기록(되돌리기 스택 등)을 가진 쪽이 구독해 비웁니다. 씬 매니저는 구독자가 누구인지 모릅니다.
     */
    using SceneObjectsInvalidatedDelegate = MulticastDelegate<void()>;

    /** @brief 미리보기 씬이 아닌 씬(또는 없는 키)을 가리키는 키입니다. */
    inline constexpr uint32 kInvalidSceneKey = 0;

    /** @brief 매니저가 받아들인 씬의 자리입니다(`SceneManager::adoptScene`). */
    enum class SceneRole : uint8
    {
        Loaded,  ///< 로드된 씬 목록에 더한다. 활성 씬이 없을 때만 활성이 된다
        Active,  ///< 로드된 씬 목록에 더하고 활성으로 바꾼 뒤 이전 활성 씬을 내린다
        Preview, ///< 미리보기 씬 목록에 더한다(활성 · 저장 · 전환에 끼지 않음)
    };

    /** @brief 미리보기 씬을 누가 틱하는지입니다(`ScenePreviewPolicy::_tick`). */
    enum class ScenePreviewTick : uint8
    {
        /**
         * 씬 매니저가 틱하되 호스트가 이번 프레임에 그 씬의 뷰를 요청한(그려질) 프레임에만 한다(`updatePreviewScenes` 의 프레임 입력).
         * 숨은 창(뒤 탭 · 접힌 창)의 씬은 시간이 멈춘다 — 다시 보이면 그 프레임의 델타로 이어 가므로 숨었던 시간만큼 튀지 않는다.
         */
        WhenDrawn = 0,
        External, ///< 씬 매니저는 틱하지 않는다 — 씬의 주인(하니스)이 직접 틱한다.
    };

    /**
     * @brief 미리보기 씬의 틱 정책입니다(`SceneManager::createPreviewScene`). 프리팹 창은 틱 · 물리 없음, 애니메이션 창은 틱만, 래그돌 창은 물리까지입니다.
     */
    struct ScenePreviewPolicy
    {
        ScenePreviewTick _tick{ ScenePreviewTick::WhenDrawn }; ///< 누가 언제 틱하는지
        float32          _timeScale{ 1.0f };                   ///< 이 씬의 시간 배율(틱 델타에 곱한다). 0 이면 틱은 돌되 시간은 멈춘다
        bool             _bPhysics{ false };                   ///< 물리 단계를 돌릴지
        bool             _bAudio{ false };                     ///< 오디오 엔진에 리스너 · 에미터를 넣을지(보통 끈다)
        bool             _bPlay{ false };                      ///< 만들 때 `beginPlay`, 내릴 때 `endPlay` 를 부를지(컴포넌트의 `onBeginPlay` 에 기대는 창)
        /**
         * @brief 이 씬 물리의 고정 스텝(초)입니다. 0 이면 엔진 설정의 고정 스텝(`EngineConfig::_fixedDeltaTime`)을 쓴다 — 정책의 기본값이지 빠진 값의 대체가 아니다.
         * @details 래그돌 · 물리 에셋 창이 관절 한계를 촘촘한 스텝으로 보려고 쓴다(`ScenePhysics::setFixedStepOverride`). 음수는 오류다.
         */
        float32 _fixedStepOverride{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 미리보기 씬 하나와 그 키 · 정책입니다. */
    struct PreviewSceneEntry
    {
        uint32             _key{ kInvalidSceneKey };
        unique_ptr<Scene>  _scene{};
        ScenePreviewPolicy _policy{};
        uint64             _tickCount{ 0 }; ///< 씬 매니저가 이 씬을 틱한 횟수(`updatePreviewScenes`, 탐침 `Editor.PreviewTickCount`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneManager
     * @brief 로드된 씬들을 관리하고 활성 씬을 추적합니다.
     */
    class SW_API SceneManager : public ISubsystem
    {
    public:
        /**
         * @brief 빈 매니저로 만듭니다. 엔진 서비스 묶음은 바인딩된 표에서 한 번 읽어 둡니다(`SceneEngineServices::makeFromBoundEngine`).
         * @details 서비스 저장소(`EngineServiceCollection`)는 표를 묶기 **전에** 이것을 만든다 — 그래서 `initializeSubsystem` 이 묶인 뒤의 표를 다시 읽습니다.
         */
        SceneManager();
        /** @brief 매니저를 해제합니다(`shutdownSubsystem` 을 부르지 않았으면 여기서 내린다). */
        ~SceneManager() override;

        /** @brief 복사를 금지합니다. */
        SceneManager( const SceneManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        SceneManager& operator=( const SceneManager& ) = delete;

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Scene";
        /**
         * @brief 매니저의 유일한 초기화 길입니다 — 묶인 표의 씬 쪽 묶음을 읽어 초기화하고, 씬이 바뀌면(오브젝트 무효) 되돌리기 기록(있으면)을 비우게 구독합니다.
         * @details 기동 순서와 손으로 만든 매니저(테스트)가 모두 이것을 부릅니다. 이 매니저와 이 매니저가 만드는 씬(동기 · 비동기 로드)은 이 묶음만 씁니다.
         *          빈 칸의 일(로드 띄우기 · 에셋 정리 · 되돌리기 비우기)은 건너뛰고, 타입 표가 없으면 씬 로드를 거절합니다.
         *          컴포넌트가 입력 · 오디오 · 모듈 코드 · 에셋 · 물리 씬을 들고, 스트리밍은 비동기 IO 다. 종료도 기록을 비운 뒤 구독을 뗀다.
         */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 로드된 씬을 모두 내리고 종료합니다(유일한 종료 길, 소멸자도 이 길을 탄다). */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Scene; }

        /** @brief 새 씬을 만들고, 활성 씬이 없으면 활성으로 정합니다. */
        Scene* createScene( string_view name );
        /** @brief 빈 씬을 만들어 활성으로 바꾸고 이전 활성 씬을 언로드합니다. */
        Scene* createEmptyActiveScene( string_view name );
        /**
         * @brief 씬 파일의 비동기 로드를 요청하고, 끝나면 활성 씬을 주는 TaskFuture 를 반환합니다.
         * @details **대기열에 들어가도 자기 답을 받습니다.** 이미 로드가 도는 중이면 이 요청은
         *          대기열로 가고, 반환하는 future 는 **이 요청의 것**입니다. 돌던 로드의 것이
         *          아닙니다. 대기열은 **한 자리**이므로 새 요청은 앞의 것을 밀어내고, 밀려난 쪽의
         *          future 는 nullptr 로 끝납니다(영원히 끝나지 않는 future 는 없습니다).
         */
        TaskFuture<Scene*> requestLoadFuture( string_view path );
        /** @brief 씬 파일의 비동기 로드를 요청합니다(TaskManager 워커). */
        bool requestLoadAsync( string_view path );
        /**
         * @brief 지금 로드하는 씬의 프리로드 진행(0~1)을 @p outProgress 에 채웁니다. 프리로드 세트가 없으면(쿠킹 전 Dev 실행 · 목록이 빈 씬 · 끈 경우) false 입니다.
         * @details 요청이 들어가면 쿠킹이 옆에 구운 프리로드 목록(`ScenePreloadList`, `<씬>.preload.json`)을 `PreloadSet` 으로 연다 — 씬을 읽고 짓는 동안 그 씬이 쓸 파일을
         *          스트리밍 큐가 미리 읽는다. 로드가 끝난 뒤에도 잠시(`gv_scenePreloadHoldFrames`) 붙들어 씬이 에셋을 잡을 때까지 캐시가 비지 않게 한다. 로딩 화면 진행률이 이것을 읽는다.
         */
        [[nodiscard]] bool tryGetLoadProgress( float32& outProgress ) const;
        /**
         * @brief 활성 씬의 루트를 씬 XML 로 저장합니다.
         * @param path 리소스 상대 또는 절대 경로. 비어 있으면 씬 소스 경로를 씁니다.
         */
        [[nodiscard]] bool saveActiveScene( string_view path = {} );
        /**
         * @brief 활성 씬의 사본을 @p path 에 저장합니다. 씬 소스 경로는 바꾸지 않습니다(다음 저장은 원래 자리로 간다 — BugIt 의 씬 사본).
         * @details 저장이 막혀 있으면(`setSaveBlockReason`) 쓰지 않고 false 입니다.
         */
        [[nodiscard]] bool saveActiveSceneCopy( string_view path );
        /**
         * @brief 씬 저장을 막거나(사유) 풉니다(빈 문자열).
         * @details 호스트가 모듈 컴포넌트를 씬에서 걷어 낸 채 아직 되돌리지 못한 동안 세웁니다(게임 모듈 리로드 · 그 실패). 그 사이에 저장하면
         *          그 컴포넌트가 빠진 씬이 저장됩니다. 막혀 있으면 `saveActiveScene` 이 사유를 알리고 false 를 반환합니다.
         */
        void setSaveBlockReason( string_view reason ) { _saveBlockReason = string{ reason }; }
        /** @brief 씬 저장이 막혀 있으면 true 입니다. */
        bool isSaveBlocked() const { return _saveBlockReason.empty() == false; }
        /** @brief 끝난 비동기 로드를 활성 씬으로 바꿔 넣습니다. 메인 스레드에서 프레임당 한 번 부르십시오. */
        void updateTransitions();
        /** @brief 활성 씬만 틱합니다. App 메인 루프가 부릅니다(게임 모듈에서 또 부르지 마십시오). */
        void update( float32 deltaTime );

        /**
         * @brief 미리보기 씬(툴 창의 숨은 월드)을 만듭니다. 활성 씬 · 로드된 씬 목록과 따로 삽니다 — 저장 · 전환 · 영속 오브젝트에 끼지 않습니다.
         * @details 언리얼 `FPreviewScene` · 유니티 `PreviewRenderUtility` 자리입니다(`docs/plans/EditorMultiWorld.md` 2.1). 씬마다 오브젝트 매니저와 씬 범위
         *          (`GameObjectManager::getSceneScope`)가 따로라 서비스(물리 · 오디오 · 내비)도 월드마다 하나씩입니다.
         *          틱은 `tickPreviewScenes` 가 정책대로 하고, 월드 플레이 상태(`setWorldPlaying`)는 따르지 않습니다(`ScenePreviewPolicy::_bPlay` 가 따로 정한다).
         *          디바이스가 걸려 있으면(`setRHIDevice`) 다른 씬과 같이 `adoptScene` 이 기본 머티리얼 · 카메라를 겁니다 — 미리보기 창은 곧바로 그리기 때문입니다.
         *          그림은 호스트가 이 씬의 키로 뷰를 요청할 때만 나옵니다(`HostSceneView` · `RenderPacketBuilder::populateScenePreviews`).
         * @return 만든 씬. 매니저가 소유하며 `destroyPreviewScene` 이나 `shutdownSubsystem` 이 내립니다.
         */
        Scene* createPreviewScene( string_view name, const ScenePreviewPolicy& policy = {} );
        /** @brief 미리보기 씬 @p pScene 을 내립니다(플레이 중이면 끝낸 뒤). 초점이던 씬이면 초점을 활성 씬으로 되돌립니다. 미리보기 씬이 아니면 false 입니다. */
        bool destroyPreviewScene( Scene* pScene );
        /** @brief @p pScene 이 이 매니저의 미리보기 씬이면 true 입니다. */
        bool isPreviewScene( const Scene* pScene ) const;
        /** @brief 미리보기 씬의 키(만든 순서대로 1 부터, 다시 쓰지 않음)입니다. 미리보기 씬이 아니면 `kInvalidSceneKey`. 렌더 뷰가 "어느 씬" 을 고르는 열쇠가 됩니다. */
        uint32 getPreviewSceneKey( const Scene* pScene ) const;
        /** @brief 오브젝트 매니저 @p pManager 를 가진 미리보기 씬의 키입니다. 미리보기 씬의 것이 아니면(활성 · 로드된 씬) `kInvalidSceneKey` 입니다. Undo 명령이 대상 씬을 기억할 때 씁니다. */
        uint32 getPreviewSceneKeyOf( const GameObjectManager* pManager ) const;
        /** @brief 키로 미리보기 씬을 찾습니다. 없으면 nullptr 입니다. */
        Scene* findPreviewScene( uint32 key ) const;
        /** @brief 미리보기 씬의 정책을 바꿉니다. 미리보기 씬이 아니면 false 입니다. 오디오 · 물리 · 플레이 설정은 바로 적용됩니다. */
        bool setPreviewPolicy( Scene* pScene, const ScenePreviewPolicy& policy );
        /** @brief 미리보기 씬의 정책입니다. 미리보기 씬이 아니면 nullptr 입니다. */
        const ScenePreviewPolicy* findPreviewPolicy( const Scene* pScene ) const;
        /** @brief 미리보기 씬 목록입니다(만든 순서). */
        const vector<PreviewSceneEntry>& getPreviewScenes() const { return _listPreviewScene; }
        /**
         * @brief 이번 프레임에 그려질 미리보기 씬(@p listDrawnSceneKey)을 `deltaTime * 시간 배율` 로 틱합니다. 전역 `GameTimeScale` 은 곱하지 않습니다(게임 월드 전용).
         * @details 활성 씬 틱과 별개로 프레임마다 한 번 부릅니다(`Engine::updateScenes` 가 호스트의 미리보기 뷰 요청에서 키를 모아 넘긴다).
         *          정책이 `ScenePreviewTick::External` 이거나 키가 목록에 없는 씬은 건너뜁니다. 틱 중에 미리보기 씬을 만들거나 내리지 마십시오.
         */
        void updatePreviewScenes( float32 deltaTime, vector_reference<const uint32> listDrawnSceneKey );

        /**
         * @brief 툴(인스펙터 · 선택 · 기즈모)이 겨누는 씬을 고릅니다. 로드된 씬이나 미리보기 씬이어야 하고, 아니면 false 입니다(바뀌지 않음).
         * @details 활성 씬(게임이 돌리는 월드)과 다릅니다 — 프리팹 창에 초점을 두어도 게임 월드는 그대로입니다. 미리보기 창(`editor::EditorPreviewPanel`)이 포커스를 얻을 때 겨누고 잃을 때 놓습니다.
         */
        bool focusScene( Scene* pScene );
        /** @brief 초점 씬을 활성 씬으로 되돌립니다. */
        void clearSceneFocus() { _pFocusedScene = nullptr; }
        /** @brief 툴이 겨누는 씬입니다. 고른 적이 없거나 그 씬이 내려졌으면 활성 씬입니다. */
        Scene* getFocusedScene() const { return _pFocusedScene != nullptr ? _pFocusedScene : _pActiveScene; }

        /** @brief 비동기 로드로 만든 씬 초기화에 쓸 디바이스를 설정합니다. */
        void setRHIDevice( IRHIDevice* pRHIDevice ) { _pRHIDevice = pRHIDevice; }
        /** @brief 현재 활성 씬을 반환합니다. */
        Scene* getActiveScene() const { return _pActiveScene; }
        /**
         * @brief 활성 씬의 오브젝트 매니저입니다. 활성 씬이 없으면 nullptr 입니다.
         * @details 활성 씬을 다루는 개발 명령 · 자동화 · 디버그 HUD · 게임 인스턴스 · 에디터가 모두 이 한 곳으로 찾는다 —
         *          "활성 씬 → 오브젝트 매니저" 를 파일마다 다시 쓰지 않는다.
         */
        GameObjectManager* findActiveObjectManager() const;
        /** @brief 활성 씬에서 이름이 @p name 인 오브젝트입니다. 활성 씬이 없거나 그 이름이 없으면 nullptr 입니다. */
        GameObject* findActiveObject( const hashed_string& name ) const;
        /**
         * @brief 월드가 플레이 중인지 정합니다. 바뀌면 활성 씬의 오브젝트 매니저에 `beginPlay` · `endPlay` 를 부릅니다.
         * @details 플레이 중에 활성 씬이 바뀌면 나가는 씬은 끝나고 들어오는 씬이 시작합니다. 에디터가 없으면 App 이 켜고(`ModuleHost`),
         *          에디터가 있으면 Play · Stop 이 켜고 끕니다. App · Shipping 에서도 켜야 onBeginPlay 가 불립니다(시퀀스 자동 재생 ·
         *          대화 그래프 로드 · 물리 동기화가 거기에 기댄다).
         */
        void setWorldPlaying( bool bPlaying );
        /** @brief 월드가 플레이 중이면 true 입니다. */
        bool isWorldPlaying() const { return _bWorldPlaying; }
        /** @brief 활성 씬이 바뀐 횟수입니다. 에디터가 씬 로드 · 새 씬 동기화에 씁니다. */
        uint64 getSceneGeneration() const { return _sceneGeneration; }
        /** @brief 비동기 로드가 진행 중이거나 교체 대기면 true 입니다. */
        bool isTransitioning() const;
        /** @brief 진행 중인 비동기 씬 로드를 취소하거나 끝날 때까지 기다리고 대기열을 비웁니다(핫 리로드 · 종료 펜스용). */
        void cancelPendingAsyncLoads();
        /**
         * @brief 워커에서 짓는 중인 씬 로드가 끝날 때까지 기다립니다. 결과는 버리지 않고 다음 `update` 가 받습니다(언리얼 `FlushAsyncLoading` 자리).
         * @details 로드 잡은 타입 표(`TypeInfo` 의 상속 프로퍼티 · 검증 캐시)를 읽고 고친다. 타입을 등록 · 해제하는 쪽은 모든 타입의 그 캐시를 비우므로
         *          먼저 이것을 불러 둘이 겹치지 않게 한다. 짓는 동안 표가 바뀌지 않으니 세대 비교(`_typeTableGeneration`)의 다시 짓기는 기다린 뒤의 등록에서만 일어난다.
         */
        void waitForPendingLoad() const;
        /** @brief 로드된 씬을 모두 반환합니다. */
        const vector<unique_ptr<Scene>>& getLoadedScenes() const { return _listLoadedScene; }
        /**
         * @brief 올라와 있는 모든 씬(로드된 씬 + 미리보기 씬)을 @p outListScene 에 모읍니다(비우고 채운다).
         * @details "어느 씬을 훑을 것인가" 를 한 곳에서 정한다 — 씬 사이 참조 검사(`SceneObjectReferenceUtil::findReferenceTo`)처럼 전 씬을 훑는 쪽이 이것을 쓴다.
         */
        void collectAllScenes( vector<const Scene*>& outListScene ) const;
        /** @brief 이 매니저와 그 씬이 쓰는 엔진 서비스 묶음입니다. */
        const SceneEngineServices& getEngineServices() const { return _services; }

        /**
         * @brief 루트 오브젝트를 플레이 중 씬 전환 너머로 가져갑니다 — 유니티 `Object.DontDestroyOnLoad` · 언리얼 심리스 트래블의 액터 목록 자리입니다.
         * @details 활성 씬이 바뀔 때 표시된 루트와 그 자손을 새 씬에 **같은 오브젝트 · 컴포넌트 id** 로 다시 만듭니다(반사 상태 · 이름 · 소켓 부착이
         *          따라가고 핸들이 이어집니다). 다시 만드는 것이라 포인터 · 반사되지 않은 런타임 상태는 새것이고 `onBeginPlay` 가 다시 불립니다
         *          (되돌리기 · 핫 리로드와 같은 계약). 루트가 아니거나 플레이 중이 아니면 경고하고 무시합니다(유니티도 루트만 · 플레이 모드에서만 받는다). 플레이를 멈추면 표시를 잊습니다.
         */
        void markPersistent( GameObject* pRoot );
        /** @brief 씬 전환 너머로 가져갈 오브젝트로 표시되어 있으면 true 입니다. */
        bool isPersistent( const GameObject* pObject ) const;

        /**
         * @brief 이전 씬의 오브젝트가 무효가 될 때(활성 씬 교체 · `shutdown`) 알리는 델리게이트입니다. 알림은 이전 씬을 언로드하기 전에 갑니다.
         * @details 구독자는 이 매니저보다 오래 살아야 합니다. 호스트는 매니저를 먼저 해제하므로 구독을 따로 해제하지 않습니다.
         */
        SceneObjectsInvalidatedDelegate& onSceneObjectsInvalidated() { return _onSceneObjectsInvalidated; }

    private:
        /** @brief 로드된 씬을 내리고 종료합니다(`shutdownSubsystem` 과 소멸자가 부른다). */
        void shutdown();
        /**
         * @brief 씬을 받아들이는 유일한 길입니다 — 디바이스가 걸려 있으면 `Scene::initialize` 를 부르고 @p role 의 목록에 넣습니다.
         * @details 만들기(`createScene` · `createEmptyActiveScene` · `createPreviewScene`)와 비동기 로드 결과가 모두 여기로 옵니다. 길마다 초기화 여부가 갈리지 않게 하려고 한 곳에 둡니다.
         */
        Scene* adoptScene( unique_ptr<Scene> scene, SceneRole role );
        /** @brief 씬을 언로드하고 목록에서 제거합니다. */
        void unloadScene( Scene* pScene );
        /** @brief 활성 씬을 바꿉니다. `_pActiveScene` 대입은 모두 여기로 옵니다. */
        void activateScene( Scene* pScene );
        /** @brief 표시된 영속 루트와 그 자손을 @p pFrom 에서 @p pTo 로 같은 id 로 옮겨 심습니다. 사라진 루트는 표시에서 뺍니다. */
        void carryPersistentObjects( Scene* pFrom, Scene* pTo );
        /** @brief `onSceneObjectsInvalidated` 구독자에게 알립니다(활성 씬이 바뀔 때 · 종료). */
        void broadcastSceneObjectsInvalidated();
        /** @brief 받은 에셋 매니저에 더 이상 참조되지 않는 에셋을 정리하게 합니다(활성 씬이 바뀐 뒤). 없으면 아무것도 하지 않습니다. */
        void garbageCollectUnusedAssets();
        /**
         * @brief 워커에 로드를 실제로 띄웁니다. 이 로드의 결과를 받을 약속을 함께 넘깁니다.
         * @details 요청 경로와 대기열 경로가 **같은 자리**로 모입니다 — 대기열에 넣은 요청자가 쥔 future 와 실제 로드의 약속이
         *          갈리지 않게.
         */
        bool dispatchLoad( string_view path, TaskPromise<Scene*> promise );
        /** @brief 워커에서 씬을 로드하는 잡 본문입니다. TaskArgs 는 AsyncLoadSlot shared_ptr 와 경로 문자열입니다. */
        static void loadSceneAsyncJob( const TaskArgs& args );

    private:
        struct AsyncLoadSlot
        {
            mutex               _mutex;
            unique_ptr<Scene>   _scene;
            atomic<bool>        _bReady{ false };
            atomic<bool>        _bAccepting{ true };
            TaskPromise<Scene*> _promise{};
            uint32              _typeTableGeneration{ 0 }; ///< 로드를 띄울 때의 `TypeRegistry::getGeneration` — 다르면 짓는 동안 타입(생성 함수)이 바뀌었다
            SceneEngineServices _services{};               ///< 워커가 짓는 씬에 넘길 묶음(`initializeSubsystem` 이 적는다 — 워커는 전역 표를 읽지 않는다)
        };

        vector<unique_ptr<Scene>> _listLoadedScene;
        vector<uint64>            _listPersistentObjectID; ///< `markPersistent` 로 표시한 루트의 오브젝트 id(옮겨 심어도 같다)
        Scene*                    _pActiveScene;
        vector<PreviewSceneEntry> _listPreviewScene; ///< `createPreviewScene` 로 만든 미리보기 씬(로드된 씬 목록과 따로)
        Scene*                    _pFocusedScene;    ///< `focusScene` 으로 고른 씬(nullptr 이면 활성 씬)
        uint32                    _nextPreviewKey;   ///< 다음 미리보기 씬 키(1 부터, 줄이지 않는다)
        bool                      _bWorldPlaying;    ///< `setWorldPlaying`
        uint64                    _sceneGeneration;
        IRHIDevice*               _pRHIDevice;
        SceneEngineServices       _services; ///< 생성자가 읽고 `initializeSubsystem` 이 다시 읽은 엔진 서비스 묶음(빌림)

        shared_ptr<AsyncLoadSlot> _asyncLoad;
        string                    _queuedPath;
        /** @brief 대기열에 든 요청의 약속. 대기열은 한 자리이므로 이것도 하나다. */
        TaskPromise<Scene*> _queuedPromise;
        atomic<bool>        _bLoadInFlight;
        TaskHandle          _loadHandle;
        /** @brief 이 매니저가 띄운 씬 로드 태스크 전부(핸들 하나는 다시 띄우면 덮인다). 종료 · 취소는 엔진 전체(`waitAll`)가 아니라 이것만 기다린다. */
        TaskHandleTracker               _loadTracker;
        string                          _saveBlockReason;               ///< 비어 있지 않으면 씬 저장을 막는다(`setSaveBlockReason`)
        SceneObjectsInvalidatedDelegate _onSceneObjectsInvalidated;     ///< `onSceneObjectsInvalidated`
        Subscription                    _commandStackClearSubscription; ///< 기동 순서가 건 되돌리기 기록 비우기 구독(`initializeSubsystem`)
        bool                            _bInitialized;

        /** @brief 요청한 씬의 프리로드 목록을 열어 파일을 미리 읽기 시작합니다. 목록이 없으면 아무것도 하지 않습니다(`tryGetLoadProgress` 참고). */
        void beginScenePreload( string_view path );
        /** @brief 프리로드 세트를 한 프레임 진행하고, 로드가 끝난 뒤 붙든 프레임이 차면 놓습니다(`updateTransitions` 가 부른다). */
        void updateScenePreload();

        unique_ptr<PreloadSet> _pScenePreload;     ///< 로드 중이거나 끝난 지 얼마 안 된 씬의 프리로드. 없으면 nullptr
        uint32                 _preloadHeldFrames; ///< 로드가 끝난 뒤 프리로드를 붙들고 지난 프레임 수
    };
} // namespace sw

namespace sw
{
    namespace engine
    {
        /**
         * @brief 엔진 범위 씬 매니저의 활성 씬 오브젝트 매니저입니다. 엔진 서비스 바인딩 전이거나 활성 씬이 없으면 nullptr 입니다.
         * @details 엔진 서비스 창구로 씬을 찾는 쪽(개발 명령 · 자동화 · 디버그 HUD)의 한 창구입니다. 게임 · 에디터 범위는 자기 창구로
         *          `SceneManager` 를 찾은 뒤 멤버 `findActiveObjectManager()` 를 부릅니다.
         */
        SW_API GameObjectManager* findActiveObjectManager();
        /** @brief 엔진 범위 활성 씬에서 이름이 @p name 인 오브젝트입니다. 바인딩 전 · 활성 씬 없음 · 그 이름 없음이면 nullptr 입니다. */
        SW_API GameObject* findActiveObject( const hashed_string& name );
    } // namespace engine
} // namespace sw
