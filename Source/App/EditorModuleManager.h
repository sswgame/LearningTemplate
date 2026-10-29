/**
 * @file App/EditorModuleManager.h
 * @brief App 전용 모듈 호스트입니다. 공통 `GameModuleManager`(게임 모듈) 위에 에디터 인스턴스, 에디터 리로드, 에디터 UI · Present 훅, RHI 교체 뒤 재초기화를 구현합니다.
 *        인스턴스 없이 에디터 모듈만 올려 부르는 헤드리스 작업은 `EditorModuleManagerHeadless.cpp` 에 있습니다.
 * @note 의존은 이쪽에서 `GameModuleManager` 로 한 방향입니다. 전용 서버(`Server`)는 이 클래스를 링크하지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "ModuleAPI/ABI/EditorAPI.h"

#include "ModuleManager/GameModuleManager.h"

namespace sw
{
    struct ModuleAPISymbols;
    struct NativeWindowEvent;

    class CameraComponent;
    class IRHIDevice;

    /**
     * @class EditorModuleManager
     * @brief 에디터 모듈의 수명 주기와 API 바인딩을 게임 모듈 호스트 위에 구현합니다. App 이 unique_ptr 로 소유합니다.
     * @details 에디터를 켜지 않으면(`initialize` 의 bEnableEditor) 에디터 쪽은 모두 아무 일도 하지 않고, 공통 호스트와 같게 동작합니다.
     */
    class EditorModuleManager final : public GameModuleManager
    {
    public:
        EditorModuleManager();
        ~EditorModuleManager() override;

        /** @brief 매니페스트가 에디터 모듈을 켰는지 기억하고 공통 `GameModuleManager::loadModuleLibrarys` 를 부릅니다. */
        [[nodiscard]] bool loadModuleLibrarys( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution );
        /**
         * @brief LiveReloadManager 에 콜백을 등록하고 게임과 에디터 인스턴스를 만듭니다(게임 모듈 라이브러리는 `loadModuleLibrarys` 가 이미 로드했습니다).
         * @details **게임이 먼저, 에디터가 나중**입니다. 게임이 처음 여는 씬(실행 설정의 시작 씬, 타이틀, 시작 맵)을 요청한 뒤에 에디터가 자기 시작 씬
         *          (`-gv_editorStartupScene`)을 요청해야, 마지막 요청을 남기는 씬 매니저에서 에디터의 씬이 열립니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저(Shipping 에서는 nullptr)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 창
         * @param pRenderThread 렌더 스레드(워커 비우기용)
         * @param bEnableEditor 에디터 모드 여부
         */
        [[nodiscard]] bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor );

        /**
         * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 @p kind 원본을 임포트하거나 대조합니다(`App --import-textures`, `--import-models` 와 `--check-*`).
         * @details 헤드리스 부팅(창과 RHI 없음)에서 부릅니다. 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 해제한 뒤 모듈을 언로드합니다.
         *          Shipping 에는 에디터 모듈이 없어 실패합니다. 인스턴스 상태를 쓰지 않아 구현은 `EditorModuleManagerHeadless.cpp` 에 따로 둡니다
         *          (모듈 라이브러리를 직접 올리고 내리는 의존이 인스턴스 경로와 `SmokeTest` 에 섞이지 않게).
         * @return 원본과 임포트 결과가 맞으면(임포트는 모두 성공하면) true 를 반환합니다.
         */
        [[nodiscard]] static bool importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly );
        /**
         * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 로컬라이제이션 작업 @p task 를 실행합니다(`App --gather-text`, `--check-text`, `--import-po`, `--export-po`).
         * @details 임포트와 같은 경로입니다(`importAssetsWithEditorModule`). 타입 공급자 모듈이 모두 로드된 헤드리스 부팅에서 부릅니다. Shipping 에서는 실패합니다.
         * @param poPath `ImportPo` 가 읽을 PO 파일
         * @param projectArgument `-loc-project` 값(비면 기본 대상 전부)
         * @return 작업이 성공하면(확인 모드는 테이블이 최신이면) true 를 반환합니다.
         */
        [[nodiscard]] static bool runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument );

        // 프레임 단위 처리
        /**
         * @brief 메인 스레드에서 에디터 UI 와 플랫폼 창을 갱신하고, 그 결과(게임 뷰포트 RT, 씬 틱 여부)를 프레임 상태에 확정합니다.
         * @details 에디터가 없으면 바로 돌아오며, 그때 프레임 상태는 "백버퍼 + 씬 틱" 기본값입니다.
         */
        void updateEditorUI( float32 deltaTime );
        /** @brief 월드 틱 뒤에 에디터의 Step 을 소비합니다. */
        void endEditorFrame();
        /** @brief 네이티브 창 이벤트를 에디터에 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief 이번 프레임에 씬 뷰를 그리는 에디터 카메라를 에디터에서 조회합니다. */
        CameraComponent* getSceneViewCamera() const;

        /** @brief 렌더 스레드의 Present 훅에서 에디터 UI 를 그립니다(`render`). 에디터가 없으면 아무것도 하지 않습니다. */
        void renderEditor( IRHIDevice& renderDevice );
        /** @brief Present 뒤에 에디터의 멀티 뷰포트 · 플랫폼 창을 처리합니다. 에디터가 없으면 아무것도 하지 않습니다. */
        void postPresentEditor( IRHIDevice& renderDevice );

        // LiveReload 콜백. 에디터.
        /** @brief 에디터 모듈을 언로드하기 직전에 불립니다. 인스턴스와 API 테이블을 해제합니다. */
        void onBeforeEditorReload();
        /** @brief 에디터 모듈이 다시 로드된 직후에 불립니다. API 를 다시 바인딩하고 인스턴스를 만듭니다. */
        void onAfterEditorReload( void* pLibraryModule );
        /**
         * @brief 새 에디터 이미지가 이 호스트와 같은 API 테이블로 빌드됐는지 확인합니다(ABI 버전, 지문, `exportEditorAPI`). 옛 이미지를 언로드하기 전에 불립니다.
         * @details 여기서 거절하면 옛 에디터가 그대로 돈다. 같은 검사를 옛 이미지를 언로드한 뒤(`onAfterEditorReload`)에야 하면 거절이 곧 에디터를 잃는 일이 된다.
         */
        bool isEditorImageUsable( void* pLibraryModule ) const;
        /** @brief 편집기 모듈을 지금 내리면 안 되는지입니다(편집기 API `isReloadHeld` — 열린 파일 대화상자). 리로드 잡음(`LiveReloadManager::addReloadHold`)으로 건다. */
        bool isEditorReloadHeld() const;
        /**
         * @brief 새 에디터 모듈이 리로드 직후 결함을 냈을 때 불립니다. 인스턴스와 API 테이블을 **모듈을 부르지 않고** 버립니다.
         * @param faultCode 예외 코드(Windows) 또는 시그널 번호(리눅스)
         */
        void onEditorReloadFault( uint32 faultCode );

        /** @brief 에디터 DLL 에서 API 테이블을 받아 바인딩합니다. ABI 버전이나 스탬프가 다르면 실패합니다. */
        [[nodiscard]] bool bindEditorAPI( void* pLibraryModule );
#if !defined( SW_SHIPPING )
        /**
         * @brief 이미 만든 에디터 인스턴스와 그 API 테이블을 호스트에 연결합니다.
         * @details 정상 경로는 `bindEditorAPI` 다음 `createEditorInstance` 이다. 이 함수는 모듈 DLL 과 디바이스 없이 가짜 API 테이블로 호스트의 순서
         *          (시뮬레이션 멈춤, shutdown, destroy)를 테스트하려고 둔다. 에디터 모드(`initialize` 의 bEnableEditor)일 때만 에디터로 쓰인다.
         *          테스트 전용 함수라 배포본에는 없다. 배포본에서 호스트의 API 테이블을 바꿔 끼울 경로를 남기지 않기 위해서다.
         */
        void attachEditorInstance( const EditorAPI& editorAPI, EditorHandle editor );
#endif

        /** @brief RHI 핫스왑 뒤 게임, 에디터 순서(기동과 같다)로 다시 초기화합니다. 실패하면 false 를 반환합니다. */
        [[nodiscard]] bool reinitializeAfterRHISwap( void* pEditorModule, void* pGameModule );

    protected:
        /**
         * @brief 이번 프레임에 게임 로직을 돌려야 하는지 에디터에 묻습니다.
         * @details 에디터가 없으면 항상 돕니다. 에디터가 있으면 Play 중일 때만 돕니다. 편집 중에 게임 update 가 돌면 저장하지 않은
         *          씬을 게임 코드가 바꿔 버립니다.
         */
        bool queryGameplayActive() const override;
        bool controlsWorldPlay() const override { return hasEditor(); }
        /**
         * @brief 무엇을 언로드하든 에디터 시뮬레이션부터 멈춥니다.
         * @details 게임만 언로드하면 에디터가 사라진 게임을 계속 돌리려 합니다. 에디터를 언로드하면 월드 플레이 상태는 에디터보다 오래 사는 `SceneManager` 와
         *          오브젝트 매니저에 있으므로, 새로 만든 에디터는 멈춤 상태로 시작하는데 월드는 계속 플레이 중입니다(편집한 컴포넌트가 onBeginPlay 를 받고,
         *          플레이 스냅샷은 복원되지 않습니다). 멈추면 플레이가 끝나고 스냅샷이 복원되는데, 이 일은 에디터 컨텍스트가 아직 있는 동안 해야 합니다.
         */
        void onBeforeSuspendModules() override;
        void suspendHostModule( bool bReleaseAPITable ) override;
        /**
         * @brief 에디터의 "렌더 대기" 표시를 버립니다.
         * @details 그 표시는 렌더 스레드의 postPresent 만 풀 수 있는데, 그 스레드는 방금 일을 끝내고 쉬고 있다. 알려 주지 않으면 다음 updateUI 나
         *          shutdown 이 waitForDrawSnapshotIdle 에서 영원히 돌아오지 않는다. 에디터 모듈 핫 리로드가 실제로 여기서 멈췄다.
         */
        void onRenderWorkersDrained() override;
        /** @brief 종료의 비우기 앞에서 에디터의 백그라운드 잡에 중단 신호를 올립니다. */
        void onBeforeDrainWorkers( DrainReason reason ) override;

    private:
        /** @brief 에디터 모듈 라이브러리의 ABI · API 표 심볼 이름입니다. 인스턴스 경로와 헤드리스 경로(`EditorModuleManagerHeadless.cpp`)가 같이 씁니다. */
        static const ModuleAPISymbols& getModuleSymbols();
        /** @brief 에디터를 실제로 호출할 수 있는 상태인지 확인합니다(모드가 켜져 있고 인스턴스가 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /** @brief 에디터가 월드를 틱해야 하는지 묻습니다. Pause 이면서 Step 이 아니면 false 입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임의 게임 뷰와 씬 뷰 RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다(보이지 않는 패널의 뷰는 비어 있습니다). */
        void sampleViewTargets();

        /** @brief 호스트 서비스 범위를 다시 만들어 에디터 모듈에 넘깁니다. */
        void rebindEditorService();
        /**
         * @brief 에디터 인스턴스를 shutdown, destroy 하고 핸들을 비웁니다.
         * @param bReleaseAPITable true 면 API 테이블과 타입 등록까지 해제합니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseAPITable );
        /** @brief 이미 바인딩한 API 테이블로 에디터 인스턴스를 만들고 초기화합니다. 디바이스가 없으면 만들지 않습니다. */
        [[nodiscard]] bool createEditorInstance();
        /** @brief RHI 교체 뒤 에디터를 다시 만듭니다. 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        [[nodiscard]] bool recreateEditorInstance( void* pEditorModule );

    private:
        /** @brief 에디터를 켤 때 EditorModule 다음에 로드할 확장 모듈(종류 EditorExtension) 하나입니다. */
        struct EditorExtensionModule
        {
            string         _name;
            vector<string> _listDependency; ///< 매니페스트의 의존(EditorModule, 키트). 리로드 그래프가 의존이 바뀌면 확장을 다시 로드한다.
        };

        /** @brief 확장 모듈을 의존과 함께 리로드 그래프에 등록합니다. 하나가 실패해도 에디터는 뜹니다(그 확장만 없습니다). */
        void registerEditorExtensions( LiveReloadManager* pLiveReloadManager );

        EditorAPI                     _editorAPI;
        EditorHandle                  _editor;
        unique_ptr<SubsystemScope>    _editorServices;      ///< 에디터 모듈에 넘긴 호스트 서비스 범위(`rebindEditorService` 가 새로 만든다)
        vector<EditorExtensionModule> _listEditorExtension; ///< 켜진 확장 모듈(로드 순서이며, `loadModuleLibrarys` 가 모은다).

        uint8                  _bEnableEditor        : 1;
        uint8                  _bEditorModuleActive  : 1; ///< 매니페스트가 에디터 모듈을 켰는가(`loadModuleLibrarys` 가 정한다).
        uint8                  _bFreshEditorInstance : 1; ///< 인스턴스를 새로 만든 뒤 첫 UI 프레임 전이다(`updateEditorUI` 가 렌더 스레드를 기다린 뒤 내린다).
        [[maybe_unused]] uint8 _reserved             : 5;
    };
} // namespace sw
