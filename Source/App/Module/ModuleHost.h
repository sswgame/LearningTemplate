/**
 * @file ModuleHost.h
 * @brief 에디터 · 게임 모듈의 수명 주기를 관리합니다(LiveReload 콜백 · API 바인딩).
 *
 * @note 호스트 ↔ 모듈 계약(RuntimeAPI)의 수명 주기를 App 쪽에서 맡습니다. Engine 은 Editor 를 모르고, App 은 EditorAPI ·
 *       GameAPI 의 세부 사항을 모릅니다. Dev 모드에서는 LiveReloadManager 와 함께 핫 리로드 전후 콜백을 처리합니다.
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

    /** @brief 모듈 인스턴스를 내릴 범위입니다. */
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
     *          beginFrame(게임 업데이트 전)에서, 게임 뷰포트와 씬 틱 여부는 updateEditorUi(에디터가 이번 프레임 입력을 처리한 뒤)에서
     *          채웁니다. 순서를 바꾸면 에디터의 Step 한 칸이 틱 없이 소비됩니다.
     */
    struct ModuleFrameState
    {
        /** @brief 오프스크린 Game View RT 식별자입니다. 0 이면 백버퍼에 바로 그립니다. */
        uint64 _gameViewportTarget;
        /** @brief Game View RT 의 가로 픽셀 수입니다. RT 가 0 이면 의미가 없습니다. */
        uint32 _gameViewportWidth;
        /** @brief Game View RT 의 세로 픽셀 수입니다. RT 가 0 이면 의미가 없습니다. */
        uint32 _gameViewportHeight;
        /** @brief 이번 프레임에 게임 모듈의 update/fixedUpdate 를 돌려야 하면 1 입니다. */
        uint8 _bGameplayActive : 1;
        /** @brief 이번 프레임에 씬 GameObject 를 틱해야 하면 1 입니다. */
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
     * @brief 에디터 · 게임 모듈의 수명 주기와 API 바인딩을 감쌉니다.
     *
     * @details
     *   App 이 ModuleHost 를 unique_ptr 로 소유합니다.
     *   - initialize() 에서 LiveReloadManager 에 콜백을 등록합니다.
     *   - 핫 리로드 때 onBefore/onAfter 콜백이 자동으로 불립니다.
     */
    class ModuleHost
    {
    public:
        // 1) 생성 · 소멸과 initialize/shutdown
        ModuleHost();
        ~ModuleHost();

        ModuleHost( const ModuleHost& )            = delete;
        ModuleHost& operator=( const ModuleHost& ) = delete;

        /**
         * @brief 이미 로드된 모듈의 핸들입니다. 핫 리로드가 없으면(Shipping) 항상 nullptr 입니다.
         * @details 모듈 수명을 아는 것은 여기입니다. 부르는 쪽이 LiveReloadManager 를 직접 알 필요가 없습니다. RHI 백엔드 교체가
         *          모듈을 다시 세울 때 이 핸들로 같은 DLL 을 다시 바인딩합니다.
         */
        void* getLoadedModuleHandle( string_view moduleName ) const;

        /**
         * @brief LiveReloadManager 에 콜백을 등록하고 에디터 · 게임 모듈을 로드합니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저(Shipping 에서는 nullptr)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 창
         * @param pRenderThread 렌더 스레드(워커 비우기용)
         * @param bEnableEditor 에디터 모드 여부
         * @param listGameKitModule 함께 로드할 GameFramework 키트 모듈 목록
         */
        bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& listGameKitModule );
        /**
         * @brief 모듈 인스턴스를 내리고, 등록부에 걸어 둔 콜백을 **모두** 떼어 냅니다.
         * @details 콜백은 이 객체의 메서드를 가리킵니다. `App` 이 이 객체를 등록부보다 먼저 지우므로, 여기서 떼지 않으면 그 사이에
         *          리로드가 돌 때 이미 사라진 객체를 부르게 됩니다.
         */
        void shutdown();

        // 2) 프레임 단위 처리
        /**
         * @brief 이번 프레임의 게임플레이 활성 여부를 확정합니다. 게임 업데이트보다 먼저 부릅니다.
         * @details 고정 스텝이 여러 번 돌아도 에디터에 다시 묻지 않도록 여기서 한 번 고정합니다.
         */
        void beginFrame();
        /** @brief 게임 업데이트를 부릅니다. */
        void updateGame( float32 deltaTime );
        /** @brief 물리 같은 고정 주기 게임 업데이트를 부릅니다. */
        void fixedUpdateGame( float32 fixedDeltaTime );
        /**
         * @brief 메인 스레드에서 에디터 UI 와 플랫폼 창을 갱신하고, 그 결과(게임 뷰포트 RT · 씬 틱 여부)를 프레임 상태에 확정합니다.
         * @details 에디터가 없으면 바로 돌아오며, 그때 프레임 상태는 "백버퍼 + 씬 틱" 기본값입니다.
         */
        void updateEditorUi( float32 deltaTime );
        /** @brief 월드 틱 뒤에 에디터의 Step 을 소비합니다. */
        void endEditorFrame();
        /** @brief 네이티브 창 이벤트를 에디터에 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );

        /** @brief 이번 프레임의 모듈 상태입니다. beginFrame 뒤에만 유효합니다. */
        const ModuleFrameState& getFrameState() const { return _frameState; }
        /** @brief 이번 프레임 Game View 에 쓸 카메라를 에디터에서 조회합니다. */
        CameraComponent* getViewportCamera() const;
        /** @brief 부팅할 때 정한 에디터 모드 여부입니다. */
        bool isEditorEnabled() const { return _bEnableEditor == SW_TRUE; }

        /** @brief 에디터 인스턴스 핸들을 반환합니다. App 의 Present 훅이 씁니다. */
        EditorHandle getEditor() const { return _editor; }
        /** @brief EditorAPI 테이블을 반환합니다. App 의 Present 훅이 씁니다. */
        const EditorAPI& getEditorApi() const { return _editorApi; }
        /** @brief ModuleCompiler 인스턴스를 반환합니다. HostServiceList.xxx 가 모듈에 넘깁니다. */
        ModuleCompiler* getModuleCompiler() const { return _moduleCompiler.get(); }

        // 3) LiveReload 콜백. LiveReloadManager 의 델리게이트가 부른다
        /** @brief 에디터 모듈을 내리기 직전에 불립니다. 인스턴스와 API 테이블을 놓습니다. */
        void onBeforeEditorReload();
        /** @brief 에디터 모듈이 다시 올라온 직후에 불립니다. API 를 다시 바인딩하고 인스턴스를 만듭니다. */
        void onAfterEditorReload( void* pLibraryModule );
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
         */
        void onBeforeCommitBatch( const vector<string>& listModuleName );

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
        void poisonLiveReload( const utf8* pReason );

        // 4) 모듈 바인딩
        /** @brief 에디터 DLL 에서 API 테이블을 받아 바인딩합니다. ABI 버전 · 스탬프가 다르면 실패합니다. */
        bool bindEditorApi( void* pLibraryModule );
        /** @brief 게임 API 테이블을 바인딩합니다. 배포 구성은 정적 `exportGameApi`, 개발 구성은 DLL 심볼을 씁니다. */
        bool bindGameApi( void* pLibraryModule );

        /** @brief RHI 핫스왑 뒤 에디터 · 게임을 다시 초기화합니다. 실패하면 false 입니다. */
        bool reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule );

    private:
        /** @brief 에디터를 실제로 부를 수 있는 상태인지 확인합니다(모드가 켜져 있고 인스턴스가 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /**
         * @brief 이번 프레임에 게임 로직을 돌려야 하는지 에디터에 묻습니다.
         * @details 에디터가 없으면 항상 돕니다. 에디터가 있으면 Play 중일 때만 돕니다. 편집 중에 게임 update 가 돌면 저장하지 않은
         *          씬을 게임 코드가 바꿔 버립니다.
         */
        bool queryGameplayActive() const;
        /** @brief 에디터가 월드를 틱해야 하는지 묻습니다. Pause 이면서 Step 이 아니면 false 입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임 Game View RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다. */
        void sampleGameViewport();

        /** @brief ModuleService 를 다시 만들어 에디터 모듈에 넘깁니다. */
        void rebindEditorService();
        /** @brief ModuleService 를 다시 만들어 게임 모듈에 넘깁니다(게임에 허용된 것만 채워집니다). */
        void rebindGameService();

        /**
         * @brief 에디터 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다.
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseApiTable );
        /** @brief 게임 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다. 인자 뜻은 위와 같습니다. */
        void destroyGameInstance( bool bReleaseApiTable );

        /**
         * @brief 이미 바인딩한 API 테이블로 인스턴스를 만들고 초기화합니다. 실패하면 정리한 뒤 false 를 반환합니다.
         * @note **디바이스가 없으면 만들지 않습니다.** 초기화할 때 디바이스를 넘겨야 하는데 `RHI::getDevice()` 는 널 참조를
         *       반환하므로, 디바이스가 없는 상태에서 물으면 그 자리에서 죽습니다.
         */
        bool createEditorInstance();
        /** @brief 게임 인스턴스를 만들고 초기화합니다. 디바이스 전제는 위와 같습니다. */
        bool createGameInstance();

        /** @brief 게임이 자기 상태를 직렬화해 두게 합니다(리로드 사이에 보존할 것). */
        void captureGameState();
        /** @brief 보존해 둔 상태를 새 인스턴스에 되돌립니다. 실패하면 **버리지 않고** 다음 리로드까지 들고 있습니다. */
        void restoreGameState();
        /** @brief RHI 교체 뒤 에디터를 다시 세웁니다. 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        bool recreateEditorInstance( void* pEditorModule );
        /** @brief RHI 교체 뒤 게임을 다시 세웁니다. 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        bool recreateGameInstance( void* pGameModule );

    private:
        unique_ptr<ModuleCompiler> _moduleCompiler;

        EditorAPI    _editorApi;
        GameAPI      _gameApi;
        EditorHandle _editor;
        GameHandle   _game;

        LiveReloadManager* _pLiveReloadManager; // 소유하지 않는다
        RHI*               _pRHI;               // 소유하지 않는다
        IWindow*           _pWindow;            // 소유하지 않는다
        RenderThread*      _pRenderThread;      // 소유하지 않는다

        // 핫 리로드 때 게임 상태를 보존하는 재사용 버퍼
        vector<uint8> _listGameSavedState;

        /** @brief 이번 프레임에 고정한 모듈 상태입니다. */
        ModuleFrameState _frameState;

        uint8                  _bEnableEditor : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
