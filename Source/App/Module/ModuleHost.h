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
         * @brief LiveReloadManager에 콜백을 등록하고 에디터/게임 모듈을 로드합니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저 (nullable for Shipping)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 윈도우
         * @param pRenderThread 렌더 스레드 (drainWorkers 용)
         * @param bEnableEditor 에디터 모드 여부
         */
        bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& listGameKitModule );
        void shutdown();

        // 3) 프레임 단위 처리
        /** @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */
        void updateEditorUI( float32 deltaTime );
        /** @brief 게임 업데이트를 호출합니다. */
        void updateGame( float32 deltaTime );
        /** @brief 물리 등 고정 주기 게임 업데이트를 호출합니다. */
        void fixedUpdateGame( float32 fixedDeltaTime );
        /** @brief 네이티브 윈도우 이벤트를 에디터에 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );

        /** @brief 이번 프레임 Game View RT 핸들과 크기를 에디터에서 조회합니다. */
        void getGameViewport( uint64& renderTarget, uint32& width, uint32& height ) const;
        /** @brief 이번 프레임 Game View에 쓸 카메라를 에디터에서 조회합니다. */
        CameraComponent* getViewportCamera() const;
        /** @brief 에디터가 월드를 틱해야 하면 true입니다. Pause이면서 Step이 아니면 false입니다. */
        bool shouldTickScene() const;
        /** @brief 월드 틱 이후 에디터 Step을 소비합니다. */
        void endEditorFrame();

        /** @brief 에디터 인스턴스 핸들을 반환합니다. App 의 프레젠트 훅이 씁니다. */
        EditorHandle getEditor() const { return _editor; }
        /** @brief EditorAPI 테이블을 반환합니다. App 의 프레젠트 훅이 씁니다. */
        const EditorAPI& getEditorAPI() const { return _editorApi; }
        /** @brief ModuleCompiler 인스턴스를 반환합니다. HostServiceList.xxx 가 모듈에 넘깁니다. */
        ModuleCompiler* getModuleCompiler() const { return _moduleCompiler.get(); }

        // 4) LiveReload 콜백 — LiveReloadManager의 델리게이트가 호출
        void onBeforeEditorReload();
        void onAfterEditorReload( void* pLibraryModule );
        void onBeforeGameReload();
        void onAfterGameReload( void* pLibraryModule );
        void onBeforeGameplayDllReload();
        void onAfterGameplayDllReload( void* pLibraryModule );
        void onBeforeCommitBatch( const vector<string>& listModuleName );
        /** @brief RHI 백엔드 핫스왑 전 기존 에디터 및 게임 런타임 인스턴스를 안전하게 정리합니다. */
        void onBeforeRhiSwap();
        void drainRenderWorkers();
        void poisonLiveReload( const utf8* pReason );

        // 5) 모듈 바인딩
        bool bindEditorAPI( void* pLibraryModule );
        bool bindGameAPI( void* pLibraryModule );

        /** @brief RHI 핫스왑 후 에디터/게임을 재초기화합니다. 실패하면 false. */
        bool reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule );

    private:
        /** @brief 에디터를 실제로 부를 수 있는 상태인가 (모드 켜짐 + 인스턴스 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /**
         * @brief 이번 프레임 게임 로직을 돌려야 하는가.
         * @details 에디터가 없으면 항상 돈다. 에디터가 있으면 Play 중일 때만 돈다 — 편집 중에
         *          게임 update 가 돌면 저장하지 않은 씬을 게임 코드가 바꿔 버린다.
         */
        bool isGameplayActive() const;

        /** @brief ModuleService 를 다시 만들어 에디터/게임 모듈에 넘깁니다. */
        void rebindEditorService();
        void rebindGameService();

        /**
         * @brief 에디터 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다.
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전).
         *        RHI 핫스왑처럼 **같은 모듈로 다시 만들** 때는 false — 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseApiTable );
        void destroyGameInstance( bool bReleaseApiTable );

        /** @brief 이미 바인딩된 API 테이블로 인스턴스를 만들고 초기화합니다. 실패하면 정리 후 false. */
        bool createEditorInstance();
        bool createGameInstance();

        void captureGameState();
        void restoreGameState();
        bool recreateEditorInstance( void* pEditorModule );
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

        uint8                  _bEnableEditor : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
