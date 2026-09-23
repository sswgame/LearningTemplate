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
        /** @brief 끝난 비동기 로드를 활성 씬으로 바꿔 넣습니다. 메인 스레드에서 프레임당 한 번 부르십시오. */
        void tickTransitions();
        /** @brief 활성 씬만 틱합니다. App 메인 루프가 부릅니다(게임 모듈에서 또 부르지 마십시오). */
        void tick( float32 deltaTime );

        /** @brief 비동기 로드로 만든 씬 초기화에 쓸 디바이스를 설정합니다. */
        void setRhiDevice( IRHIDevice* pRhiDevice ) { _pRHIDevice = pRhiDevice; }
        /** @brief 현재 활성 씬을 반환합니다. */
        Scene* getActiveScene() const { return _pActiveScene; }
        /** @brief 활성 씬이 바뀐 횟수입니다. 에디터가 씬 로드 · 새 씬 동기화에 씁니다. */
        uint64 getSceneGeneration() const { return _sceneGeneration; }
        /** @brief 비동기 로드가 진행 중이거나 교체 대기면 true 입니다. */
        bool isTransitioning() const;
        /** @brief 진행 중인 비동기 씬 로드를 취소하거나 끝날 때까지 기다리고 대기열을 비웁니다(핫 리로드 · 종료 펜스용). */
        void cancelPendingAsyncLoads();
        /** @brief 로드된 씬을 모두 반환합니다. */
        const vector<unique_ptr<Scene>>& getLoadedScenes() const { return _listLoadedScene; }

    private:
        /** @brief 씬을 언로드하고 목록에서 제거합니다. */
        void unloadScene( Scene* pScene );
        /** @brief 활성 씬을 바꿉니다. `_pActiveScene` 대입은 모두 여기로 옵니다(대입 자리가 다섯이라 한곳으로 모았습니다). */
        void activateScene( Scene* pScene );
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
        };

        vector<unique_ptr<Scene>> _listLoadedScene;
        Scene*                    _pActiveScene;
        uint64                    _sceneGeneration;
        IRHIDevice*               _pRHIDevice;

        shared_ptr<AsyncLoadSlot> _asyncLoad;
        string                    _queuedPath;
        /** @brief 대기열에 든 요청의 약속. 대기열은 한 자리이므로 이것도 하나다. */
        TaskPromise<Scene*> _queuedPromise;
        atomic<bool>        _bLoadInFlight;
        TaskHandle          _loadHandle;
        bool                _bInitialized;
    };
} // namespace sw
