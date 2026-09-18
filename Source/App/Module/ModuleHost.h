/**
 * @file ModuleHost.h
 * @brief 에디터/게임 모듈 라이프사이클 관리 — LiveReload 콜백·API 바인딩
 *
 * @note RuntimeAPI 레이어에 위치하여 호스트↔모듈 계약의 라이프사이클을 관리합니다.
 *       Engine은 Editor를 모르고, App은 EditorAPI/GameAPI 세부사항을 모릅니다.
 *       Dev 모드에서 LiveReloadManager와 협력해 핫리로드 전후 콜백을 처리합니다.
 */
#pragma once
#include "App/AppConfig.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/GameAPI.h"

namespace sw
{
    struct GameKitConfig;
    struct NativeWindowEvent;

    class CameraComponent;
    class IRHIDevice;
    class IWindow;
    class LiveReloadManager;
    class ModuleCompiler;
    class RenderThread;
    class RHI;

    /** @brief 모듈 인스턴스를 내릴 범위. */
    enum class ModuleScope : uint8
    {
        Editor,
        Game,
        Both
    };

    /**
     * @brief 한 프레임 동안 고정되는 호스트↔모듈 상태.
     * @details 에디터 상태는 DLL 경계를 넘는 함수 포인터 호출로만 알 수 있다. 고정 스텝마다
     *          다시 묻던 것을 한 번 래치해 프레임 안의 모든 단계가 같은 답을 본다.
     *          필드는 생산되는 자리에서 채운다 — 게임플레이 활성 여부는 beginFrame(게임
     *          업데이트 이전), 게임 뷰포트와 씬 틱 여부는 updateEditorUi(에디터가 이번 프레임
     *          입력을 처리한 이후)다. 순서를 바꾸면 에디터의 Step 한 칸이 틱 없이 소비된다.
     */
    struct ModuleFrameState
    {
        /** @brief 오프스크린 Game View RT 식별자. 0 이면 백버퍼에 직접 그린다. */
        uint64 _gameViewportTarget;
        /** @brief Game View RT 의 가로 픽셀. RT 가 0 이면 의미가 없습니다. */
        uint32 _gameViewportWidth;
        /** @brief Game View RT 의 세로 픽셀. RT 가 0 이면 의미가 없습니다. */
        uint32 _gameViewportHeight;
        /** @brief 이번 프레임 게임 모듈의 update/fixedUpdate 를 돌려야 하면 1. */
        uint8 _bGameplayActive : 1;
        /** @brief 이번 프레임 씬 GameObject 를 틱해야 하면 1. */
        uint8                  _bTickScene : 1;
        [[maybe_unused]] uint8 _reserved   : 6;

        /** @brief 에디터가 없을 때의 답(둘 다 돈다)으로 시작합니다. */
        ModuleFrameState()
            : _gameViewportTarget{ 0 }
            , _gameViewportWidth{ 0 }
            , _gameViewportHeight{ 0 }
            , _bGameplayActive{ SW_TRUE }
            , _bTickScene{ SW_TRUE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class ModuleHost
     * @brief 에디터·게임 모듈의 생명주기·API 바인딩을 캡슐화합니다.
     *
     * @details
     *   App은 ModuleHost를 unique_ptr으로 소유합니다.
     *   - initialize()에서 LiveReloadManager에 콜백을 등록합니다.
     *   - 핫리로드 시 onBefore/onAfter 콜백이 자동으로 호출됩니다.
     */
    class ModuleHost
    {
    public:
        // 1) ctor/dtor → initialize/shutdown
        ModuleHost();
        ~ModuleHost();

        ModuleHost( const ModuleHost& )            = delete;
        ModuleHost& operator=( const ModuleHost& ) = delete;

        /**
         * @brief 이미 로드된 모듈의 핸들입니다. 핫리로드가 없으면(Shipping) 늘 nullptr.
         * @details 모듈 수명을 아는 것은 여기다 — 부르는 쪽이 LiveReloadManager 를 직접 알 필요가 없다.
         *          RHI 백엔드 교체가 모듈을 다시 세울 때 이 핸들로 같은 DLL 을 다시 바인딩한다.
         */
        void* getLoadedModuleHandle( string_view moduleName ) const;

        /**
         * @brief LiveReloadManager에 콜백을 등록하고 에디터/게임 모듈을 로드합니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저 (nullable for Shipping)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 윈도우
         * @param pRenderThread 렌더 스레드 (drainWorkers 용)
         * @param bEnableEditor 에디터 모드 여부
         * @param listGameKitModule 함께 로드할 GameFramework 킷 모듈 목록
         */
        bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& listGameKitModule );
        /**
         * @brief 모듈 인스턴스를 내리고, 등록부에 걸어 둔 콜백을 **전부** 떼어 냅니다.
         * @details 콜백은 이 객체의 메서드를 가리킨다 — `App` 이 이 객체를 등록부보다 먼저 지우므로
         *          여기서 떼지 않으면 그 사이의 리로드가 죽은 객체로 뛰어든다.
         */
        void shutdown();

        // 3) 프레임 단위 처리
        /**
         * @brief 이번 프레임 게임플레이 활성 여부를 확정합니다. 게임 업데이트보다 먼저 부릅니다.
         * @details 고정 스텝이 여러 번 돌아도 에디터에 다시 묻지 않게 여기서 한 번 래치합니다.
         */
        void beginFrame();
        /** @brief 게임 업데이트를 호출합니다. */
        void updateGame( float32 deltaTime );
        /** @brief 물리 등 고정 주기 게임 업데이트를 호출합니다. */
        void fixedUpdateGame( float32 fixedDeltaTime );
        /**
         * @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신하고, 그 결과를 프레임 상태에
         *        확정합니다(게임 뷰포트 RT·씬 틱 여부).
         * @details 에디터가 없으면 즉시 반환하며, 그때 프레임 상태는 "백버퍼 + 씬 틱" 기본값입니다.
         */
        void updateEditorUi( float32 deltaTime );
        /** @brief 월드 틱 이후 에디터 Step을 소비합니다. */
        void endEditorFrame();
        /** @brief 네이티브 윈도우 이벤트를 에디터에 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );

        /** @brief 이번 프레임 모듈 상태입니다. beginFrame 이후에만 유효합니다. */
        const ModuleFrameState& getFrameState() const { return _frameState; }
        /** @brief 이번 프레임 Game View에 쓸 카메라를 에디터에서 조회합니다. */
        CameraComponent* getViewportCamera() const;
        /** @brief 부팅 시 결정된 에디터 모드 여부입니다. */
        bool isEditorEnabled() const { return _bEnableEditor == SW_TRUE; }

        /** @brief 에디터 인스턴스 핸들을 반환합니다. App 의 프레젠트 훅이 씁니다. */
        EditorHandle getEditor() const { return _editor; }
        /** @brief EditorAPI 테이블을 반환합니다. App 의 프레젠트 훅이 씁니다. */
        const EditorAPI& getEditorApi() const { return _editorApi; }
        /** @brief ModuleCompiler 인스턴스를 반환합니다. HostServiceList.xxx 가 모듈에 넘깁니다. */
        ModuleCompiler* getModuleCompiler() const { return _moduleCompiler.get(); }

        // 4) LiveReload 콜백 — LiveReloadManager의 델리게이트가 호출
        /** @brief 에디터 모듈을 내리기 직전 — 인스턴스와 API 테이블을 놓습니다. */
        void onBeforeEditorReload();
        /** @brief 에디터 모듈이 다시 올라온 직후 — API 를 다시 묶고 인스턴스를 만듭니다. */
        void onAfterEditorReload( void* pLibraryModule );
        /** @brief 게임 모듈을 내리기 직전 — 상태를 보존하고 인스턴스를 놓습니다. */
        void onBeforeGameReload();
        /**
         * @brief 게임 모듈이 다시 올라온 직후 — API 를 묶고 인스턴스를 만든 뒤 상태를 되돌립니다.
         * @param pLibraryModule 새로 올라온 DLL 핸들. 배포 구성에서는 정적 링크라 쓰이지 않습니다.
         */
        void onAfterGameReload( void* pLibraryModule );
        /** @brief GameFramework·킷 DLL 을 내리기 직전 — 게임과 같은 절차를 밟습니다. */
        void onBeforeGameplayDllReload();
        /**
         * @brief 킷 DLL 이 다시 올라온 직후 — 씬이 캐시한 TypeInfo 포인터를 새 DLL 것으로 다시 묶습니다.
         * @details 이것을 빠뜨리면 살아 있는 오브젝트가 내려간 DLL 의 TypeInfo 를 가리킨다.
         */
        void onAfterGameplayDllReload( void* pLibraryModule );
        /**
         * @brief 캐스케이드가 전부 준비된 뒤 첫 커밋 직전 — 에디터 외 모듈이 섞여 있으면 게임을 먼저 내립니다.
         * @param listModuleName 이번 배치에서 교체될 모듈 이름들.
         */
        void onBeforeCommitBatch( const vector<string>& listModuleName );

        /**
         * @brief 모듈 인스턴스를 안전하게 내립니다 — 워커 배수 → 상태 보존 → 파괴.
         * @param scope 내릴 대상.
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전).
         *        RHI 핫스왑처럼 **같은 모듈로 다시 만들** 때는 false — 테이블을 그대로 재사용합니다.
         * @details 종료·핫리로드·RHI 핫스왑이 모두 이 순서를 지켜야 한다. 사유마다 따로 조립하면
         *          한 곳만 고쳐 놓고 나머지를 잊는다.
         */
        void suspendModules( ModuleScope scope, bool bReleaseApiTable );
        /**
         * @brief 렌더 워커·GPU·태스크를 모두 재웁니다. 모듈을 내리기 전에 반드시 지나가는 자리입니다.
         * @details 디바이스가 없는 RHI 도 여기 들어온다(백엔드 교체 실패) — `hasDevice()` 를 먼저 묻는다.
         */
        void drainRenderWorkers();
        /** @brief 리로드 그래프를 깨진 것으로 표시해 이후 리로드를 막습니다. 배포 구성에서는 아무 일도 하지 않습니다. */
        void poisonLiveReload( const utf8* pReason );

        // 5) 모듈 바인딩
        /** @brief 에디터 DLL 에서 API 표를 받아 묶습니다. ABI 버전·스탬프가 다르면 실패합니다. */
        bool bindEditorApi( void* pLibraryModule );
        /** @brief 게임 API 표를 묶습니다. 배포 구성은 정적 `exportGameApi`, 개발 구성은 DLL 심볼입니다. */
        bool bindGameApi( void* pLibraryModule );

        /** @brief RHI 핫스왑 후 에디터/게임을 재초기화합니다. 실패하면 false. */
        bool reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule );

    private:
        /** @brief 에디터를 실제로 부를 수 있는 상태인가 (모드 켜짐 + 인스턴스 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /**
         * @brief 이번 프레임 게임 로직을 돌려야 하는가를 에디터에 묻습니다.
         * @details 에디터가 없으면 항상 돈다. 에디터가 있으면 Play 중일 때만 돈다 — 편집 중에
         *          게임 update 가 돌면 저장하지 않은 씬을 게임 코드가 바꿔 버린다.
         */
        bool queryGameplayActive() const;
        /** @brief 에디터가 월드를 틱해야 하는가를 묻습니다. Pause이면서 Step이 아니면 false입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임 Game View RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다. */
        void sampleGameViewport();

        /** @brief ModuleService 를 다시 만들어 에디터 모듈에 넘깁니다. */
        void rebindEditorService();
        /** @brief ModuleService 를 다시 만들어 게임 모듈에 넘깁니다(게임에 허용된 것만 채워집니다). */
        void rebindGameService();

        /**
         * @brief 에디터 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다.
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전).
         *        RHI 핫스왑처럼 **같은 모듈로 다시 만들** 때는 false — 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseApiTable );
        /** @brief 게임 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다. 인자 뜻은 위와 같습니다. */
        void destroyGameInstance( bool bReleaseApiTable );

        /**
         * @brief 이미 바인딩된 API 테이블로 인스턴스를 만들고 초기화합니다. 실패하면 정리 후 false.
         * @note **디바이스가 없으면 만들지 않는다** — 초기화에 디바이스를 넘겨야 하는데
         *       `RHI::getDevice()` 는 널 참조라, 없는 상태로 물으면 그 자리에서 죽는다.
         */
        bool createEditorInstance();
        /** @brief 게임 인스턴스를 만들고 초기화합니다. 디바이스 전제는 위와 같습니다. */
        bool createGameInstance();

        /** @brief 게임에게 자기 상태를 직렬화하게 해 둡니다(리로드 사이에 보존할 것). */
        void captureGameState();
        /** @brief 보존해 둔 상태를 새 인스턴스에 되돌립니다. 실패하면 **버리지 않고** 다음 리로드까지 들고 있습니다. */
        void restoreGameState();
        /** @brief RHI 교체 뒤 에디터를 다시 세웁니다. 표가 비었으면 모듈에서 다시 바인딩합니다. */
        bool recreateEditorInstance( void* pEditorModule );
        /** @brief RHI 교체 뒤 게임을 다시 세웁니다. 표가 비었으면 모듈에서 다시 바인딩합니다. */
        bool recreateGameInstance( void* pGameModule );

    private:
        unique_ptr<ModuleCompiler> _moduleCompiler;

        EditorAPI    _editorApi;
        GameAPI      _gameApi;
        EditorHandle _editor;
        GameHandle   _game;

        LiveReloadManager* _pLiveReloadManager; // non-owning
        RHI*               _pRHI;               // non-owning
        IWindow*           _pWindow;            // non-owning
        RenderThread*      _pRenderThread;      // non-owning

        // 핫리로드 시 게임 상태 보존용 재사용 버퍼
        vector<uint8> _listGameSavedState;

        /** @brief 이번 프레임 래치된 모듈 상태. */
        ModuleFrameState _frameState;

        uint8                  _bEnableEditor : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
