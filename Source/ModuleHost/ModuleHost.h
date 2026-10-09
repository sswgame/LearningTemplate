/**
 * @file ModuleHost.h
 * @brief 게임 모듈의 수명 주기를 관리합니다(이미지 올리기 · 게임 API 바인딩 · 게임 업데이트 · 게임 쪽 핫 리로드). App 과 Server 가 같이 씁니다.
 *
 * @note 호스트 ↔ 모듈 계약(RuntimeAPI)의 수명 주기를 실행 파일 쪽에서 맡습니다. 에디터 인스턴스 · 에디터 리로드 · 에디터 UI · RHI 교체 뒤
 *       재초기화는 App 전용 `EditorModuleHost`(Source/App)가 이 클래스 위에 얹습니다 — 의존은 그쪽에서 여기로 한 방향입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Renderer/Frame/RenderView.h"

#include "RuntimeAPI/ABI/GameAPI.h"

namespace sw
{
    struct ModuleResolution;
    struct ModuleService;

    class IWindow;
    class LiveReloadManager;
    class ModuleCatalog;
    class ModuleCompiler;
    class RenderThread;
    class RHI;

    /** @brief 모듈 인스턴스를 내릴 범위입니다. `Editor` 는 호스트 모듈(App 의 에디터)이고, 그것이 없는 호스트(전용 서버)에서는 내릴 것이 없습니다. */
    enum class ModuleScope : uint8
    {
        Editor,
        Game,
        Both
    };

    /**
     * @brief 한 프레임 동안 고정되는 호스트 ↔ 모듈 상태입니다.
     * @details 에디터 상태는 DLL 경계를 넘는 함수 포인터 호출로만 알 수 있습니다. 고정 스텝마다 다시 묻던 것을 한 번 고정해,
     *          프레임 안의 모든 단계가 같은 답을 보게 합니다. 필드는 값이 만들어지는 곳에서 채웁니다. 게임플레이 활성 여부는
     *          beginFrame(게임 업데이트 전)에서, 게임 뷰 · 씬 뷰 RT 와 씬 틱 여부는 updateEditorUi(에디터가 이번 프레임 입력을 처리한 뒤)에서
     *          채웁니다. 순서를 바꾸면 에디터의 Step 한 칸이 틱 없이 소비됩니다.
     */
    struct ModuleFrameState
    {
        /** @brief 에디터가 이번 프레임에 그려 달라는 게임 뷰 · 씬 뷰 RT 입니다. 보이지 않는 패널의 뷰는 비어 있고, 둘 다 비면 백버퍼에 바로 그립니다. */
        HostViewTargets _views;
        /** @brief 이번 프레임에 게임 모듈의 update/fixedUpdate 를 돌려야 하면 1 입니다. */
        uint8 _bGameplayActive : 1;
        /** @brief 이번 프레임에 씬 GameObject 를 틱해야 하면 1 입니다. */
        uint8                  _bTickScene : 1;
        [[maybe_unused]] uint8 _reserved   : 6;

        /** @brief 에디터가 없을 때의 답(둘 다 돈다)으로 시작합니다. */
        ModuleFrameState()
            : _views{}
            , _bGameplayActive{ SW_TRUE }
            , _bTickScene{ SW_TRUE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ModuleHost
     * @brief 게임 모듈의 수명 주기와 게임 API 바인딩을 감쌉니다(App · Server 공통).
     *
     * @details
     *   실행 파일이 unique_ptr 로 소유합니다. 전용 서버는 이 클래스를 그대로(`initializeDedicatedServer`), App 은 파생 `EditorModuleHost` 를 씁니다.
     *   - 핫 리로드 때 onBefore/onAfter 콜백이 자동으로 불립니다.
     *   - 모듈을 내리는 경로는 `suspendModules` 하나입니다. 호스트 모듈(에디터)이 끼어드는 자리는 아래 보호 가상 함수 다섯입니다.
     *
     *   파생 클래스는 소멸자에서 `shutdown()` 을 먼저 부릅니다 — 이 클래스의 소멸자 안에서는 가상 함수가 파생 쪽으로 가지 않습니다.
     */
    class ModuleHost
    {
    public:
        // 1) 생성 · 소멸과 initialize/shutdown
        ModuleHost();
        virtual ~ModuleHost();

        ModuleHost( const ModuleHost& )            = delete;
        ModuleHost& operator=( const ModuleHost& ) = delete;

        /**
         * @brief 이미 로드된 모듈의 핸들입니다. 핫 리로드가 없으면(Shipping) 항상 nullptr 입니다.
         * @details 모듈 수명을 아는 것은 여기입니다. 부르는 쪽이 LiveReloadManager 를 직접 알 필요가 없습니다. RHI 백엔드 교체가
         *          모듈을 다시 세울 때 이 핸들로 같은 DLL 을 다시 바인딩합니다.
         */
        void* getLoadedModuleHandle( string_view moduleName ) const;

        /**
         * @brief 타입 공급자 모듈(GameFramework · 키트 · SWGame)의 이미지를 올려 그 타입을 등록합니다. 인스턴스는 만들지 않습니다.
         * @details 기동 단계 `ModuleTypes` 의 호스트 로더(`App` · `Server`)가 부릅니다 — 이것이 끝나야 엔진이 씬을 읽습니다. 게임 인스턴스는
         *          그 뒤 기동(`initializeDedicatedServer` · `EditorModuleHost::initialize`)이 만듭니다. Shipping 은 모듈이 정적 링크라 올릴 것이 없습니다.
         *          무엇을 어떤 순서로 올릴지는 모듈 매니페스트의 해석 결과(@p resolution)가 정합니다 — 켜진 GameFramework · 키트 · SWGame 을 적재 순서대로
         *          (의존이 먼저). 키트의 리로드 의존은 매니페스트의 `_listDependency` 입니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저(Shipping 에서는 nullptr)
         * @param catalog 매니페스트 묶음(`Bin/Modules/<모듈>.module.json`)
         * @param resolution `catalog.resolve` 결과
         */
        [[nodiscard]] bool loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution );
        /**
         * @brief 전용 서버로 게임 인스턴스를 만듭니다 — 창 · RHI · 렌더 스레드 · 에디터가 없고, 게임은 `initialize( nullptr, nullptr )` 를 받습니다.
         * @details 모듈 콜백 · 핫 리로드(Dev) · 상태 보존은 App 과 같습니다.
         */
        [[nodiscard]] bool initializeDedicatedServer( LiveReloadManager* pLiveReloadManager );
        /**
         * @brief 모듈 인스턴스를 내리고, 등록부에 걸어 둔 콜백을 **모두** 떼어 냅니다.
         * @details 콜백은 이 객체의 메서드를 가리킵니다. 실행 파일이 이 객체를 등록부보다 먼저 지우므로, 여기서 떼지 않으면 그 사이에
         *          리로드가 돌 때 이미 사라진 객체를 부르게 됩니다.
         */
        void shutdown();

        // 2) 프레임 단위 처리
        /**
         * @brief 이번 프레임의 게임플레이 활성 여부를 확정합니다. 게임 업데이트보다 먼저 부릅니다.
         * @details 고정 스텝이 여러 번 돌아도 호스트 모듈(에디터)에 다시 묻지 않도록 여기서 한 번 고정합니다.
         */
        void beginFrame();
        /** @brief 게임 업데이트를 부릅니다. */
        void updateGame( float32 deltaTime );
        /** @brief 물리 같은 고정 주기 게임 업데이트를 부릅니다. */
        void fixedUpdateGame( float32 fixedDeltaTime );

        /** @brief 이번 프레임의 모듈 상태입니다. beginFrame 뒤에만 유효합니다. */
        const ModuleFrameState& getFrameState() const { return _frameState; }
        /** @brief ModuleCompiler 인스턴스를 반환합니다. HostServiceList.xxx 가 모듈에 넘깁니다. */
        ModuleCompiler* getModuleCompiler() const { return _moduleCompiler.get(); }

        // 3) LiveReload 콜백. LiveReloadManager 의 델리게이트가 부른다
        /** @brief 게임 모듈을 내리기 직전에 불립니다. 상태를 보존하고 인스턴스를 놓습니다. */
        void onBeforeGameReload();
        /**
         * @brief 게임 모듈이 다시 올라온 직후에 불립니다. API 를 바인딩하고 인스턴스를 만든 뒤 상태를 되돌립니다.
         * @param pLibraryModule 새로 올라온 DLL 핸들. 배포 구성은 정적 링크라 쓰지 않습니다.
         */
        void onAfterGameReload( void* pLibraryModule );
        /** @brief GameFramework · 키트 DLL 을 내리기 직전에 불립니다. 게임과 같은 절차를 밟습니다. */
        void onBeforeGameplayDllReload();
        /**
         * @brief 키트 DLL 이 다시 올라온 직후에 불립니다. 씬이 캐시한 TypeInfo 포인터를 새 DLL 의 것으로 바꿉니다.
         * @details 이것을 빠뜨리면 살아 있는 오브젝트가 내려간 DLL 의 TypeInfo 를 가리킵니다.
         */
        void onAfterGameplayDllReload( void* pLibraryModule );
        /**
         * @brief 연쇄 교체 대상이 모두 준비된 뒤 첫 commit 직전에 불립니다. 에디터가 아닌 모듈이 섞여 있으면 게임을 먼저 내립니다.
         * @param listModuleName 이번 배치에서 교체될 모듈 이름들
         * @return 게임 상태를 찍지 못해 **아무것도 내리지 않았으면** false 입니다. 그러면 리로드를 거두고 옛 게임 모듈이 계속 돕니다.
         */
        [[nodiscard]] bool onBeforeCommitBatch( const vector<string>& listModuleName );
        /**
         * @brief 새 게임 이미지가 이 호스트와 같은 API 표로 빌드됐는지 봅니다(ABI 버전 · 지문 · `exportGameApi`). 옛 이미지를 내리기 전에 불립니다.
         * @details 여기서 거절하면 옛 게임이 그대로 돈다. 같은 검사를 옛 것을 내린 뒤(`onAfterGameReload`)에야 하면 거절이 곧 게임을 잃는 일이 된다.
         */
        bool isGameImageUsable( void* pLibraryModule ) const;
        /**
         * @brief 새 게임 모듈이 리로드 직후 결함을 냈을 때 불립니다. 인스턴스와 API 표를 **모듈을 부르지 않고** 버립니다.
         * @param faultCode 예외 코드(Windows) · 시그널 번호(리눅스)
         */
        void onGameReloadFault( uint32 faultCode );

        /**
         * @brief 모듈 인스턴스를 안전하게 내립니다(워커 비우기 → 상태 보존 → 파괴).
         * @param scope 내릴 대상
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         * @details 종료 · 핫 리로드 · RHI 핫스왑이 모두 이 순서를 지켜야 합니다. 사유마다 따로 조립하면 한 곳만 고치고 나머지를
         *          잊게 됩니다.
         */
        void suspendModules( ModuleScope scope, bool bReleaseApiTable );
        /**
         * @brief 렌더 워커 · GPU · 태스크가 하던 일을 모두 끝낼 때까지 기다립니다. 모듈을 내리기 전에 반드시 거치는 곳입니다.
         * @details 디바이스가 없는 RHI 도 여기에 들어옵니다(백엔드 교체 실패). 그래서 `hasDevice()` 를 먼저 확인합니다.
         */
        void drainRenderWorkers();
        /** @brief 리로드 그래프를 깨진 상태로 표시해 이후 리로드를 막습니다. 배포 구성에서는 아무 일도 하지 않습니다. */
        void markReloadGraphBroken( const utf8* pReason );

        // 4) 모듈 바인딩
        /** @brief 게임 API 테이블을 바인딩합니다. 배포 구성은 정적 `exportGameApi`, 개발 구성은 DLL 심볼을 씁니다. */
        bool bindGameApi( void* pLibraryModule );
#if !defined( SW_SHIPPING )
        /**
         * @brief 이미 만든 게임 인스턴스와 그 API 표를 호스트에 붙입니다.
         * @details 모듈 DLL · 디바이스 없이 가짜 API 표로 게임을 내리는 순서(상태 찍기 → shutdown → destroy)를 시험하려고 둡니다.
         *          시험 전용 창구라 배포본에는 없다 — 배포본에서 호스트의 API 표를 바꿔 끼울 길을 남기지 않는다.
         */
        void attachGameInstance( const GameAPI& gameApi, GameHandle game );
#endif

    protected:
        /**
         * @brief 콜백을 걸고 게임 인스턴스를 만듭니다(게임 모듈 이미지는 `loadModuleImages` 가 이미 올렸다). App · 서버 기동의 공통 몸체입니다.
         * @param pRHI 활성 RHI. 주면 게임은 디바이스가 있어야 만들어지고, nullptr 이면(전용 서버) 디바이스 없이 만듭니다.
         * @param pWindow 플랫폼 창(없으면 nullptr)
         * @param pRenderThread 렌더 스레드(워커 비우기용, 없으면 nullptr)
         */
        [[nodiscard]] bool initializeGame( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread );
        /** @brief RHI 교체 뒤 게임을 다시 세웁니다. 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        [[nodiscard]] bool recreateGameInstance( void* pGameModule );
        /** @brief 호스트가 모듈에 넘기는 서비스 표를 채웁니다. @p bGameModule 이면 게임에 허용된 것(GameVisible)만 채웁니다. */
        void fillModuleService( ModuleService& outService, bool bGameModule ) const;

        /** @brief 이번 프레임 상태를 호스트 모듈이 채웁니다(뷰 RT · 씬 틱 여부). */
        ModuleFrameState&  getMutableFrameState() { return _frameState; }
        LiveReloadManager* getLiveReloadManager() const { return _pLiveReloadManager; }
        RHI*               getRhi() const { return _pRHI; }
        IWindow*           getWindow() const { return _pWindow; }

        // 호스트 모듈(App 의 에디터)이 끼어드는 자리 — 이 클래스만으로는(전용 서버) 모두 아무 일도 하지 않습니다.
        /** @brief 이번 프레임에 게임 로직을 돌려야 하는지입니다. 호스트 모듈이 없으면 항상 true 입니다. */
        virtual bool queryGameplayActive() const { return true; }
        /** @brief 월드 플레이 상태를 호스트 모듈이 정하면 true 입니다(에디터의 Play · Stop). false 면 `beginFrame` 이 월드를 플레이로 켭니다. */
        virtual bool controlsWorldPlay() const { return false; }
        /** @brief 무엇을 내리든 그 전에 불립니다(에디터 시뮬레이션 멈춤). 게임 상태를 찍기 전입니다. */
        virtual void onBeforeSuspendModules() {}
        /** @brief `ModuleScope::Editor` · `Both` 를 내릴 때 게임보다 먼저 불립니다(에디터 인스턴스 파괴). */
        virtual void suspendHostModule( bool bReleaseApiTable ) { (void)bReleaseApiTable; }
        /** @brief 렌더 워커 · GPU 를 비운 직후, 태스크를 기다리기 전에 불립니다(에디터의 렌더 대기 표시 버리기). */
        virtual void onRenderWorkersDrained() {}

    private:
        /** @brief ModuleService 를 다시 만들어 게임 모듈에 넘깁니다(게임에 허용된 것만 채워집니다). */
        void rebindGameService();
        /** @brief 게임 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다. @p bReleaseApiTable 은 `suspendModules` 와 같습니다. */
        void destroyGameInstance( bool bReleaseApiTable );
        /**
         * @brief 이미 바인딩한 API 테이블로 게임 인스턴스를 만들고 초기화합니다. 실패하면 정리한 뒤 false 를 반환합니다.
         * @note RHI 를 받은 호스트는 **디바이스가 없으면 만들지 않습니다.** `RHI::getDevice()` 는 널 참조를 반환하므로, 디바이스가 없는 상태에서
         *       물으면 그 자리에서 죽습니다.
         */
        [[nodiscard]] bool createGameInstance();

        /**
         * @brief 게임이 자기 상태를 직렬화해 두게 합니다(리로드 사이에 보존할 것).
         * @return 게임이 직렬화를 실패로 알렸으면 false 입니다. 게임이 없거나 찍을 상태가 없으면 true 입니다.
         */
        [[nodiscard]] bool captureGameState();
        /**
         * @brief `suspendModules` 의 본체입니다.
         * @param bKeepGameOnCaptureFailure true 면 게임 상태를 찍지 못했을 때 아무것도 내리지 않고 false 를 돌려줍니다(리로드 직전).
         *        종료 · RHI 교체처럼 상태를 넘겨받을 새 이미지가 없으면 false 로 둡니다.
         */
        [[nodiscard]] bool suspendModulesInternal( ModuleScope scope, bool bReleaseApiTable, bool bKeepGameOnCaptureFailure );
        /** @brief 보존해 둔 상태를 새 인스턴스에 되돌립니다. 실패하면 **버리지 않고** 다음 리로드까지 들고 있습니다. */
        void restoreGameState();

    private:
        unique_ptr<ModuleCompiler> _moduleCompiler;

        GameAPI    _gameApi;
        GameHandle _game;

        LiveReloadManager* _pLiveReloadManager; // 소유하지 않는다
        RHI*               _pRHI;               // 소유하지 않는다
        IWindow*           _pWindow;            // 소유하지 않는다
        RenderThread*      _pRenderThread;      // 소유하지 않는다

        // 핫 리로드 때 게임 상태를 보존하는 재사용 버퍼
        vector<uint8> _listGameSavedState;

        /** @brief 이번 프레임에 고정한 모듈 상태입니다. */
        ModuleFrameState _frameState;
    };
} // namespace sw
