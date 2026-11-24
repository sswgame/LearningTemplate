/**
 * @file App/ClientModuleManager.h
 * @brief 클라이언트 호스트(App — 개발 · 배포 모두)의 모듈 관리자입니다. 공통 `GameModuleManager`(게임 모듈 · 장치 교체 순서) 위에 호스트 모듈(에디터)의 인스턴스, 리로드,
 *        UI · Present 훅, 장치 교체 뒤 재생성(`recreateHostModule`)을 구현합니다. 에디터를 켜지 않은 실행(배포 게임 포함)에서는 에디터 몫이 아무 일도 하지 않는다.
 *        인스턴스 없이 에디터 모듈만 올려 부르는 헤드리스 작업은 모듈 관리자의 일이 아니라 `EditorModuleHeadlessTasks` 에 있다.
 * @note 의존은 이쪽에서 `GameModuleManager` 로 한 방향입니다. 전용 서버(`Server`)는 이 클래스를 링크하지 않습니다.
 * @note 개발 전용 함수(에디터 모듈 등록 — `ClientModuleManager.cpp` 의 동적 모듈 구역)는 정적 모듈 구성이 짓지 않는다 — 부르는 자리가 `build::kStaticModules` 로 가른다.
 *       배포 게임도 이 매니저로 장치 분실 재생성을 탄다(언리얼 `FEngineLoop` 이 에디터 · 게임 공통으로 모듈을 드는 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/View/FrameViewports.h"

#include "ModuleAPI/ABI/EditorAPI.h"

#include "ModuleManager/GameModuleManager.h"

namespace sw
{
    struct ModuleAPISymbols;
    struct NativeWindowEvent;

    class CameraComponent;
    class IRHIDevice;

    /**
     * @class ClientModuleManager
     * @brief 호스트 모듈(에디터)의 수명 주기와 API 바인딩을 게임 모듈 호스트 위에 구현합니다. App 이 unique_ptr 로 소유합니다.
     * @details 에디터를 켜지 않으면(`initialize` 의 bEnableEditor) 에디터 쪽은 모두 아무 일도 하지 않고, 공통 호스트와 같게 동작합니다.
     */
    class ClientModuleManager final : public GameModuleManager
    {
    public:
        ClientModuleManager();
        ~ClientModuleManager() override;

        /** @brief 매니페스트가 에디터 모듈을 켰는지 기억하고 공통 `GameModuleManager::loadModuleLibraries` 를 부릅니다. */
        [[nodiscard]] bool loadModuleLibraries( ILiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution ) override;
        /**
         * @brief LiveReloadManager 에 콜백을 등록하고 게임과 에디터 인스턴스를 만듭니다(게임 모듈 라이브러리는 `loadModuleLibraries` 가 이미 로드했습니다).
         * @details **게임이 먼저, 에디터가 나중**입니다. 게임이 처음 여는 씬(실행 설정의 시작 씬, 타이틀, 시작 맵)을 요청한 뒤에 에디터가 자기 시작 씬
         *          (`-gv_editorStartupScene`)을 요청해야, 마지막 요청을 남기는 씬 매니저에서 에디터의 씬이 열립니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저(Shipping 에서는 nullptr)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 창
         * @param pRenderDriver 렌더러 창구(워커 비우기용)
         * @param bEnableEditor 에디터 모드 여부
         */
        [[nodiscard]] bool initialize( ILiveReloadManager* pLiveReloadManager, RHI* pRHI, GenericWindow* pWindow, IEngineRenderDriver* pRenderDriver, bool bEnableEditor );
        /** @brief 에디터 모듈 라이브러리의 ABI · API 표 심볼 이름입니다. 인스턴스 경로와 헤드리스 작업(`EditorModuleHeadlessTasks`)이 같이 씁니다. */
        [[nodiscard]] static const ModuleAPISymbols& getModuleSymbols();

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
        /** @brief 에디터가 이번 프레임에 그려 달라고 요청한 게임 뷰와 씬 뷰 RT 입니다(`updateEditorUI` 가 샘플한다). 보이지 않는 패널의 뷰는 비어 있고, 둘 다 비면 백버퍼에 바로 그립니다. */
        const FrameViewports& getViews() const { return _views; }
        /** @brief 이번 프레임에 씬 뷰를 그리는 에디터 카메라를 에디터에서 조회합니다. */
        CameraComponent* getSceneViewCamera() const;

        /** @brief 렌더 스레드의 Present 훅에서 에디터 UI 를 그립니다(`render`). 에디터가 없으면 아무것도 하지 않습니다. */
        /** @brief 렌더 패스 밖에서 에디터 UI 의 업로드를 합니다(렌더 스레드가 백버퍼 패스를 열기 직전 — `IEditor::prepareRender`). */
        void prepareRenderEditor( IRHIDevice& renderDevice );
        void renderEditor( IRHIDevice& renderDevice );
        /** @brief Present 뒤에 에디터의 멀티 뷰포트 · 플랫폼 창을 처리합니다. 에디터가 없으면 아무것도 하지 않습니다. */
        void postPresentEditor( IRHIDevice& renderDevice );

        // LiveReload 콜백. 에디터.
        /** @brief 에디터 모듈을 언로드하기 직전에 불립니다. 인스턴스와 API 테이블을 해제합니다. */
        void onBeforeEditorReload();
        /** @brief 에디터 모듈이 다시 로드된 직후에 불립니다. API 를 다시 바인딩하고 인스턴스를 만듭니다. */
        void onAfterEditorReload( ModuleLibraryHandle library );
        /**
         * @brief 새 에디터 이미지가 이 호스트와 같은 API 테이블로 빌드됐는지 확인합니다(ABI 버전, 지문, `exportEditorAPI`). 옛 이미지를 언로드하기 전에 불립니다.
         * @details 여기서 거절하면 옛 에디터가 그대로 돈다. 같은 검사를 옛 이미지를 언로드한 뒤(`onAfterEditorReload`)에야 하면 거절이 곧 에디터를 잃는 일이 된다.
         */
        bool isEditorLibraryUsable( ModuleLibraryHandle library ) const;
        /** @brief 편집기 모듈을 지금 내리면 안 되는지입니다(편집기 API `isReloadHeld` — 열린 파일 대화상자). 리로드 잡음(`LiveReloadManager::addReloadHold`)으로 건다. */
        bool isEditorReloadHeld() const;
        /**
         * @brief 새 에디터 모듈이 리로드 직후 결함을 냈을 때 불립니다. 인스턴스와 API 테이블을 **모듈을 부르지 않고** 버립니다.
         * @param faultCode 예외 코드(Windows) 또는 시그널 번호(리눅스)
         */
        void onEditorReloadFault( uint32 faultCode );

        /** @brief 에디터 DLL 에서 API 테이블을 받아 바인딩합니다. ABI 버전이나 스탬프가 다르면 실패합니다. */
        [[nodiscard]] bool bindEditorAPI( ModuleLibraryHandle library );

    private:
        /** @brief 에디터를 켤 때 EditorModule 다음에 로드할 확장 모듈(종류 EditorExtension) 하나입니다. */
        struct EditorExtensionModule
        {
            string         _name;
            vector<string> _listDependency; ///< 매니페스트의 의존(EditorModule, 키트). 리로드 그래프가 의존이 바뀌면 확장을 다시 로드한다.
        };

    protected:
        /**
         * @brief 이번 프레임에 게임 로직을 돌려야 하는지 에디터에 묻습니다.
         * @details 에디터가 없으면 항상 돕니다. 에디터가 있으면 Play 중일 때만 돕니다. 편집 중에 게임 update 가 돌면 저장하지 않은
         *          씬을 게임 코드가 바꿔 버립니다.
         */
        bool queryGameplayActive() const override;
        bool canControlWorldPlay() const override { return hasEditor(); }
        /**
         * @brief 무엇을 언로드하든 에디터 시뮬레이션부터 멈춥니다.
         * @details 게임만 언로드하면 에디터가 사라진 게임을 계속 돌리려 합니다. 에디터를 언로드하면 월드 플레이 상태는 에디터보다 오래 사는 `SceneManager` 와
         *          오브젝트 매니저에 있으므로, 새로 만든 에디터는 멈춤 상태로 시작하는데 월드는 계속 플레이 중입니다(편집한 컴포넌트가 onBeginPlay 를 받고,
         *          플레이 스냅샷은 복원되지 않습니다). 멈추면 플레이가 끝나고 스냅샷이 복원되는데, 이 일은 에디터 컨텍스트가 아직 있는 동안 해야 합니다.
         */
        [[nodiscard]] bool onBeforeSuspendModules( FlushReason reason ) override;
        void               suspendHostModule( bool bReleaseAPITable ) override;
        /** @brief 장치를 다시 만든 뒤 에디터를 다시 만듭니다(에디터를 켜지 않았으면 할 일이 없다). 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        [[nodiscard]] bool recreateHostModule() override;
        /**
         * @brief 에디터의 "렌더 대기" 표시를 버립니다.
         * @details 그 표시는 렌더 스레드의 postPresent 만 풀 수 있는데, 그 스레드는 방금 일을 끝내고 쉬고 있다. 알려 주지 않으면 다음 updateUI 나
         *          shutdown 이 낸 스냅샷을 기다리며(`DrawSnapshotHandoff`) 영원히 돌아오지 않는다. 에디터 모듈 핫 리로드가 실제로 여기서 멈췄다.
         */
        void onRenderWorkersFlushed() override;
        /** @brief 종료의 비우기 앞에서 에디터의 백그라운드 잡에 중단 신호를 올립니다. */
        void onBeforeFlushWorkers( FlushReason reason ) override;

    private:
        /** @brief 에디터를 실제로 호출할 수 있는 상태인지 확인합니다(모드가 켜져 있고 인스턴스가 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /** @brief 에디터가 월드를 틱해야 하는지 묻습니다. Pause 이면서 Step 이 아니면 false 입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임의 게임 뷰와 씬 뷰 RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다(보이지 않는 패널의 뷰는 비어 있습니다). */
        void sampleViewTargets();

        /** @brief 호스트 서비스 범위를 다시 만들어 에디터 모듈에 넘깁니다. 개발 도구 서비스는 스스로 호스트 모듈에 보인다고 선언해 부모 창구로 찾힌다(`SubsystemModuleVisibility`). */
        void rebindEditorService() { rebindModuleService( _editorAPI.bindService, _editorServices, false ); }
        /**
         * @brief 에디터 인스턴스를 shutdown, destroy 하고 핸들을 비웁니다.
         * @param bReleaseAPITable true 면 API 테이블과 타입 등록까지 해제합니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseAPITable );
        /** @brief 이미 바인딩한 API 테이블로 에디터 인스턴스를 만들고 초기화합니다. 디바이스가 없으면 만들지 않습니다. */
        [[nodiscard]] bool createEditorInstance();

        /**
         * @brief `initialize` 의 끝 — 에디터를 켰으면 에디터 모듈과 확장 모듈(의존과 함께)을 리로드 그래프에 등록합니다. 확장 하나가 실패해도 에디터는 뜹니다(그 확장만 없습니다).
         * @details 개발 구성에만 있다 — 배포는 `initialize` 가 부르지 않고 `-EnableEditor` 면 오류로 멈춘다.
         * @return 매니페스트가 에디터 모듈을 껐거나 등록이 실패하거나 리로드 그래프가 깨졌으면 false 입니다(기동 실패).
         */
        [[nodiscard]] bool registerEditorModule( ILiveReloadManager* pLiveReloadManager );

    private:
        /** @brief 에디터 모듈의 주인 창구입니다. 리로드 단계를 이 호스트의 에디터 콜백으로 잇는다(다중 상속 대신 멤버 객체). */
        class EditorReloadClient final : public IReloadableModuleClient
        {
        public:
            explicit EditorReloadClient( ClientModuleManager& owner )
                : _owner( owner ) {}
            void               onBeforeReload() override { _owner.onBeforeEditorReload(); }
            void               onAfterReload( ModuleLibraryHandle library ) override { _owner.onAfterEditorReload( library ); }
            void               onReloadFault( uint32 faultCode ) override { _owner.onEditorReloadFault( faultCode ); }
            [[nodiscard]] bool isLibraryUsable( ModuleLibraryHandle library ) override { return _owner.isEditorLibraryUsable( library ); }

        private:
            ClientModuleManager& _owner;
        };

        EditorReloadClient            _editorReloadClient{ *this };
        EditorAPI                     _editorAPI;
        EditorHandle                  _editor;
        unique_ptr<SubsystemScope>    _editorServices;      ///< 에디터 모듈에 넘긴 호스트 서비스 범위(`rebindModuleService` 가 새로 만든다)
        FrameViewports                _views;               ///< 이번 프레임에 샘플한 뷰 요청
        vector<EditorExtensionModule> _listEditorExtension; ///< 켜진 확장 모듈(로드 순서이며, `loadModuleLibraries` 가 모은다).

        uint8                  _bEnableEditor        : 1;
        uint8                  _bEditorModuleActive  : 1; ///< 매니페스트가 에디터 모듈을 켰는가(`loadModuleLibraries` 가 정한다).
        uint8                  _bFreshEditorInstance : 1; ///< 인스턴스를 새로 만든 뒤 첫 UI 프레임 전이다(`updateEditorUI` 가 렌더 스레드를 기다린 뒤 내린다).
        [[maybe_unused]] uint8 _reserved             : 5;
    };
} // namespace sw
