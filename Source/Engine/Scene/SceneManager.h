#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    class IRHIDevice;
    class Scene;

    /**
     * @class SceneManager
     * @brief 로드된 씬들을 관리하고 활성 씬을 추적합니다.
     */
    class SW_API SceneManager
    {
    public:
        /** @brief 빈 매니저로 만듭니다. */
        SceneManager();
        /** @brief 매니저를 해제합니다. */
        ~SceneManager();

        /** @brief 복사를 금지합니다. */
        SceneManager( const SceneManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        SceneManager& operator=( const SceneManager& ) = delete;

        /** @brief 매니저를 초기화합니다. */
        bool initialize();
        /** @brief 로드된 씬을 내리고 종료합니다. */
        void shutdown();

        /** @brief 새 씬을 만들고, 활성 씬이 없으면 활성으로 정합니다. */
        Scene* createScene( string_view name );
        /** @brief 빈 씬을 만들어 활성으로 바꾸고 이전 활성 씬을 언로드합니다. */
        Scene* createEmptyActiveScene( string_view name );
        /**
         * @brief 씬 파일의 비동기 로드를 요청하고, 끝나면 활성 씬을 주는 TaskFuture 를 반환합니다.
         * @details **대기열에 들어가도 자기 답을 받습니다.** 이미 로드가 도는 중이면 이 요청은
         *          대기열로 가고, 반환하는 future 는 **이 요청의 것**입니다. 돌던 로드의 것이
         *          아닙니다. 예전에는 후자였고, `tickTransitions` 가 대기열 때문에 그 로드를
         *          버리면서 같은 약속에 nullptr 을 넣었습니다. 그래서 대기열에 넣은 쪽은 자기 씬이
         *          멀쩡히 활성이 되는데도 실패를 받았습니다. 대기열은 **한 자리**이므로 새 요청은
         *          앞의 것을 밀어내고, 밀려난 쪽의 future 는 nullptr 로 끝납니다(예전에는 아무
         *          통지도 없이 사라져 그 future 가 영원히 끝나지 않았습니다).
         */
        TaskFuture<Scene*> requestLoadFuture( string_view path );
        /** @brief 씬 파일의 비동기 로드를 요청합니다(TaskManager 워커). */
        bool requestLoadAsync( string_view path );
        /**
         * @brief 활성 씬의 루트를 씬 XML 로 저장합니다.
         * @param path 리소스 상대 또는 절대 경로. 비어 있으면 씬 소스 경로를 씁니다.
         */
        bool saveActiveScene( string_view path = {} );
        /**
         * @brief 씬 저장을 막거나(사유) 풉니다(빈 문자열).
         * @details 호스트가 모듈 컴포넌트를 씬에서 걷어 낸 채 아직 되돌리지 못한 동안 세웁니다(게임 모듈 리로드 · 그 실패). 그 사이에 저장하면
         *          그 컴포넌트가 빠진 씬이 저장됩니다. 막혀 있으면 `saveActiveScene` 이 사유를 알리고 false 를 반환합니다.
         */
        void setSaveBlockReason( string_view reason ) { _saveBlockReason = string{ reason }; }
        /** @brief 씬 저장이 막혀 있으면 true 입니다. */
        bool isSaveBlocked() const { return _saveBlockReason.empty() == false; }
        /** @brief 끝난 비동기 로드를 활성 씬으로 바꿔 넣습니다. 메인 스레드에서 프레임당 한 번 부르십시오. */
        void tickTransitions();
        /** @brief 활성 씬만 틱합니다. App 메인 루프가 부릅니다(게임 모듈에서 또 부르지 마십시오). */
        void tick( float32 deltaTime );

        /** @brief 비동기 로드로 만든 씬 초기화에 쓸 디바이스를 설정합니다. */
        void setRhiDevice( IRHIDevice* pRhiDevice ) { _pRHIDevice = pRhiDevice; }
        /** @brief 현재 활성 씬을 반환합니다. */
        Scene* getActiveScene() const { return _pActiveScene; }
        /**
         * @brief 월드가 플레이 중인지 정합니다. 바뀌면 활성 씬의 오브젝트 매니저에 `beginPlay` · `endPlay` 를 부릅니다.
         * @details 플레이 중에 활성 씬이 바뀌면 나가는 씬은 끝나고 들어오는 씬이 시작합니다. 에디터가 없으면 App 이 켜고(`ModuleHost`),
         *          에디터가 있으면 Play · Stop 이 켜고 끕니다. 예전에는 에디터 Play 버튼만 `beginPlay` 를 불러, App · Shipping 에서는
         *          onBeginPlay 가 한 번도 불리지 않았습니다(시퀀스 자동 재생 · 대화 그래프 로드 · 물리 동기화가 에디터 밖에서 죽어 있었다).
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
        /** @brief 로드된 씬을 모두 반환합니다. */
        const vector<unique_ptr<Scene>>& getLoadedScenes() const { return _listLoadedScene; }

        /**
         * @brief 루트 오브젝트를 플레이 중 씬 전환 너머로 가져갑니다 — 유니티 `Object.DontDestroyOnLoad` · 언리얼 심리스 트래블의 액터 목록 자리입니다.
         * @details 활성 씬이 바뀔 때 표시된 루트와 그 자손을 새 씬에 **같은 오브젝트 · 컴포넌트 id** 로 다시 만듭니다(반사 상태 · 이름 · 소켓 부착이
         *          따라가고 핸들이 이어집니다). 다시 만드는 것이라 포인터 · 반사되지 않은 런타임 상태는 새것이고 `onBeginPlay` 가 다시 불립니다
         *          (되돌리기 · 핫 리로드와 같은 계약). 루트가 아니거나 플레이 중이 아니면 경고하고 무시합니다(유니티도 루트만 · 플레이 모드에서만 받는다). 플레이를 멈추면 표시를 잊습니다.
         */
        void markPersistent( GameObject* pRoot );
        /** @brief 씬 전환 너머로 가져갈 오브젝트로 표시되어 있으면 true 입니다. */
        bool isPersistent( const GameObject* pObject ) const;

    private:
        /** @brief 씬을 언로드하고 목록에서 제거합니다. */
        void unloadScene( Scene* pScene );
        /** @brief 활성 씬을 바꿉니다. `_pActiveScene` 대입은 모두 여기로 옵니다(대입 자리가 다섯이라 한곳으로 모았습니다). */
        void activateScene( Scene* pScene );
        /** @brief 표시된 영속 루트와 그 자손을 @p pFrom 에서 @p pTo 로 같은 id 로 옮겨 심습니다. 사라진 루트는 표시에서 뺍니다. */
        void carryPersistentObjects( Scene* pFrom, Scene* pTo );
        /**
         * @brief 워커에 로드를 실제로 띄웁니다. 이 로드의 결과를 받을 약속을 함께 넘깁니다.
         * @details 요청 경로와 대기열 경로가 **같은 자리**로 모이게 하려고 뽑았습니다. 예전에는
         *          대기열을 띄우는 쪽이 `requestLoadFuture` 를 다시 불러 약속을 새로 만들었고,
         *          그래서 대기열에 넣은 요청자가 쥔 future 와 실제 로드의 약속이 갈렸습니다.
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
            uint32              _factoryHeadSerial{ 0 }; ///< 로드를 띄울 때의 `GameObjectManager::getFactoryHeadSerial`
        };

        vector<unique_ptr<Scene>> _listLoadedScene;
        vector<uint64>            _listPersistentObjectId; ///< `markPersistent` 로 표시한 루트의 오브젝트 id(옮겨 심어도 같다)
        Scene*                    _pActiveScene;
        bool                      _bWorldPlaying; ///< `setWorldPlaying`
        uint64                    _sceneGeneration;
        IRHIDevice*               _pRHIDevice;

        shared_ptr<AsyncLoadSlot> _asyncLoad;
        string                    _queuedPath;
        /** @brief 대기열에 든 요청의 약속. 대기열은 한 자리이므로 이것도 하나다. */
        TaskPromise<Scene*> _queuedPromise;
        atomic<bool>        _bLoadInFlight;
        TaskHandle          _loadHandle;
        string              _saveBlockReason; ///< 비어 있지 않으면 씬 저장을 막는다(`setSaveBlockReason`)
        bool                _bInitialized;
    };
} // namespace sw
