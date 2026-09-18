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
    class FrameRenderer;
    class IRHIDevice;
    class Scene;

    /**
     * @class SceneManager
     * @brief 로드된 씬들을 관리하고, Active Scene을 추적합니다.
     */
    class SW_API SceneManager
    {
    public:
        /** @brief 빈 매니저. */
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

        /** @brief 새로운 씬을 생성하고, 활성 씬이 없으면 활성으로 지정합니다. */
        Scene* createScene( string_view name );
        /** @brief 빈 씬을 만들어 활성으로 바꾸고 이전 활성 씬을 언로드합니다. */
        Scene* createEmptyActiveScene( string_view name );
        /**
         * @brief 씬 디스크립터 XML 비동기 로드를 요청하고 완료 시 활성 씬을 제공하는 TaskFuture를 반환합니다.
         * @details **대기열에 들어가도 자기 답을 받는다.** 이미 로드가 도는 중이면 이 요청은
         *          대기열로 가고, 돌려주는 future 는 **이 요청의 것**이다 — 돌던 로드의 것이
         *          아니다. 예전에는 후자였고, `tickTransitions` 가 대기열 때문에 그 로드를
         *          버리면서 같은 약속에 nullptr 을 넣었다. 그래서 대기열에 넣은 쪽은 자기 씬이
         *          멀쩡히 활성이 되는데도 실패를 받았다. 대기열은 **한 자리**이므로 새 요청은
         *          앞의 것을 밀어내고, 밀려난 쪽의 future 는 nullptr 로 끝난다(예전에는 아무
         *          통지도 없이 사라져 그 future 가 영원히 끝나지 않았다).
         */
        TaskFuture<Scene*> requestLoadFuture( string_view path );
        /** @brief 씬 디스크립터 XML 비동기 로드를 요청합니다 (TaskManager 워커). */
        bool requestLoadAsync( string_view path );
        /**
         * @brief 활성 씬 루트를 디스크립터 XML로 저장합니다.
         * @param path 리소스 상대 또는 절대. 비어 있으면 씬 소스 경로를 씁니다.
         */
        bool saveActiveScene( string_view path = {} );
        /** @brief 메인 스레드: 완료된 비동기 로드를 교체합니다. 프레임당 한 번 호출하세요. */
        void tickTransitions();
        /** @brief 활성 씬만 tick. App 메인 루프가 호출한다 (게임 모듈에서 중복 호출하지 말 것). */
        void tick( float32 deltaTime );

        /** @brief 비동기 로드로 만든 씬 초기화에 쓸 디바이스를 설정합니다. */
        void setRhiDevice( IRHIDevice* pRhiDevice ) { _pRHIDevice = pRhiDevice; }
        /**
         * @brief App 이 소유한 FrameRenderer 를 연결합니다 (비소유).
         * @details **씬에는 내려보내지 않는다** — 씬은 그리는 쪽을 모른다(Scene::tick 주석). 여기 두는 이유는
         *          에디터 뷰포트 툴바가 뷰 모드를 바꾸려고 `getFrameRenderer()` 로 찾아오기 때문이고,
         *          그 하나뿐이다.
         */
        void setFrameRenderer( FrameRenderer* pFrameRenderer ) { _pFrameRenderer = pFrameRenderer; }
        /**
         * @brief 현재 붙어 있는 FrameRenderer 입니다 (없으면 nullptr).
         * @details 에디터가 렌더러 상태(뷰 모드 등)를 정할 때 쓰는 유일한 경로다 — Engine 은 Editor 를
         *          include 할 수 없으므로 방향은 항상 Editor -> Engine 이다.
         */
        FrameRenderer* getFrameRenderer() const { return _pFrameRenderer; }

        /** @brief 현재 활성화된(주요) 씬 반환 */
        Scene* getActiveScene() const { return _pActiveScene; }
        /** @brief 활성 씬이 바뀐 횟수입니다. 에디터가 로드/뉴 씬 동기화에 씁니다. */
        uint64 getSceneGeneration() const { return _sceneGeneration; }
        /** @brief 비동기 로드가 진행 중이거나 교체 대기면 true입니다. */
        bool isTransitioning() const;
        /** @brief 진행 중인 비동기 씬 로드 작업을 즉시 취소/대기하고 큐를 비웁니다. (핫리로드/종료 펜스용) */
        void cancelPendingAsyncLoads();
        /** @brief 로드된 씬 모두 반환 */
        const vector<unique_ptr<Scene>>& getLoadedScenes() const { return _listLoadedScene; }

    private:
        /** @brief 씬을 언로드하고 목록에서 제거합니다. */
        void unloadScene( Scene* pScene );
        /**
         * @brief 워커에 로드를 실제로 띄웁니다. 이 로드의 결과를 받을 약속을 함께 넘깁니다.
         * @details 요청 경로와 대기열 경로가 **같은 자리**로 모이게 하려고 뽑았다 — 예전에는
         *          대기열을 띄우는 쪽이 `requestLoadFuture` 를 다시 불러 약속을 새로 만들었고,
         *          그래서 대기열에 넣은 요청자가 쥔 future 와 실제 로드의 약속이 갈렸다.
         */
        bool dispatchLoad( string_view path, TaskPromise<Scene*> promise );
        /** @brief TaskArgs: AsyncLoadSlot shared_ptr, path string. */
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
        FrameRenderer*            _pFrameRenderer;

        shared_ptr<AsyncLoadSlot> _asyncLoad;
        string                    _queuedPath;
        /** @brief 대기열에 든 요청의 약속. 대기열은 한 자리이므로 이것도 하나다. */
        TaskPromise<Scene*> _queuedPromise;
        atomic<bool>        _bLoadInFlight;
        TaskHandle          _loadHandle;
        bool                _bInitialized;
    };
} // namespace sw
