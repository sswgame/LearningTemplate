/**
 * @file DevTools/Host/EditorGameModuleManager.h
 * @brief 에디터를 켠 개발 실행(`-EnableEditor`)의 모듈 관리자입니다. 공통 `GameModuleManager`(게임 모듈 · 장치 교체 순서) 위에 호스트 모듈(에디터)의 인스턴스, 리로드,
 *        UI · 렌더 훅, 창 최소 크기, 장치 교체 뒤 재생성(`recreateHostModule`)을 구현합니다.
 * @details 개발 계층(`IHostDevLayer::makeEditorModuleManager`)이 만든다 — DevTools 에만 있어 배포 App 은 에디터 코드를 컴파일하지 않고 공통 `GameModuleManager` 를 그대로 쓴다
 *          (언리얼이 `UEditorEngine` 을 배포 타깃에 넣지 않는 자리). 인스턴스 없이 에디터 모듈만 올려 부르는 헤드리스 작업은 `EditorModuleHeadlessTasks` 다.
 * @note 의존은 이쪽에서 `GameModuleManager` 로 한 방향입니다. 개발 전용 함수(에디터 모듈 등록 — cpp 의 동적 모듈 구역)는 정적 모듈 구성이 짓지 않는다.
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
    struct RenderFramePacket;

    class CameraComponent;
    class Engine;
    class IRHIDevice;

    /**
     * @class EditorGameModuleManager
     * @brief 호스트 모듈(에디터)의 수명 주기와 API 바인딩을 게임 모듈 호스트 위에 구현합니다. 부팅 열(`HostLaunch`)이 공통 형식으로 소유합니다.
     */
    class EditorGameModuleManager final : public GameModuleManager
    {
    public:
        /** @param engine 렌더 훅을 걸 렌더 스레드의 엔진입니다(호스트의 부팅 열 소유 — 이 관리자보다 오래 산다). */
        explicit EditorGameModuleManager( Engine& engine );
        ~EditorGameModuleManager() override;

        /** @brief 매니페스트가 에디터 모듈을 켰는지 기억하고 공통 `GameModuleManager::loadModuleLibraries` 를 부릅니다. */
        [[nodiscard]] bool loadModuleLibraries( ILiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution ) override;
        /**
         * @brief 게임과 에디터 인스턴스를 만들고 에디터 렌더 훅 · 창 최소 크기를 겁니다(게임 모듈 라이브러리는 `loadModuleLibraries` 가 이미 로드했습니다).
         * @details **게임이 먼저, 에디터가 나중**입니다. 게임이 처음 여는 씬(실행 설정의 시작 씬, 타이틀, 시작 맵)을 요청한 뒤에 에디터가 자기 시작 씬
         *          (`-gv_editorStartupScene`)을 요청해야, 마지막 요청을 남기는 씬 매니저에서 에디터의 씬이 열립니다.
         */
        [[nodiscard]] bool initializeClient( ILiveReloadManager* pLiveReloadManager, RHI* pRHI, GenericWindow* pWindow, IEngineRenderDriver* pRenderDriver ) override;

        // 프레임 단위 처리
        /** @brief 메인 스레드에서 에디터 UI 와 플랫폼 창을 갱신하고, 그 결과(게임 뷰포트 RT, 씬 틱 여부)를 프레임 상태에 확정합니다. */
        void updateHostModuleUI() override;
        /** @brief 월드 틱 뒤에 에디터의 Step 을 소비합니다. */
        void endHostModuleFrame() override;
        /** @brief 네이티브 창 이벤트를 에디터에 전달합니다. */
        [[nodiscard]] bool processHostModuleEvent( const NativeWindowEvent& event ) override;
        /** @brief 이번 프레임에 씬 뷰를 그리는 에디터 카메라를 에디터에서 조회합니다. */
        CameraComponent* getSceneViewCamera() const override;

    private:
        // LiveReload 콜백 · API 바인딩 — 중첩 `EditorReloadClient` 와 리로드 잡음 델리게이트만 부른다(밖에서 부르는 곳이 없다).
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

        // 렌더 스레드 훅 — 이 관리자가 사는 동안만 걸린다(`initializeClient` 가 걸고 소멸자가 `shutdown` 뒤에 뗀다 — 떼는 세터가 렌더 스레드를 먼저 비운다).
        /** @brief 에디터 렌더 훅 셋(준비 · 그리기 · Present 뒤)을 렌더 스레드에 걸거나(@p bBind) 뗍니다. 렌더 스레드가 없는 호스트는 할 일이 없다. */
        void bindRenderHooks( bool bBind );
        /** @brief 렌더 패스 밖에서 에디터 UI 의 업로드를 합니다(렌더 스레드가 백버퍼 패스를 열기 직전 — `IEditor::prepareRender`). */
        void onPrepareRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 열린 백버퍼 패스 안에서 에디터 UI 를 그립니다. */
        void onRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief Present 뒤에 에디터의 멀티 뷰포트 · 플랫폼 창을 처리합니다. */
        void onPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

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
        /** @brief 에디터를 실제로 호출할 수 있는 상태인지 확인합니다(인스턴스가 살아 있음). */
        bool hasEditor() const { return _editor != nullptr; }
        /** @brief 에디터가 월드를 틱해야 하는지 묻습니다. Pause 이면서 Step 이 아니면 false 입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임의 게임 뷰와 씬 뷰 RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다(보이지 않는 패널의 뷰는 비어 있습니다). */
        void sampleViewTargets();

        /** @brief 호스트 서브시스템 범위를 다시 만들어 에디터 모듈에 넘깁니다. 개발 도구 서비스는 스스로 호스트 모듈에 보인다고 선언해 부모 창구로 찾힌다(`SubsystemModuleVisibility`). */
        void rebindEditorSubsystems() { rebindModuleSubsystems( _editorAPI.bindService, _editorServices, false ); }
        /**
         * @brief 에디터 인스턴스를 shutdown, destroy 하고 핸들을 비웁니다.
         * @param bReleaseAPITable true 면 API 테이블과 타입 등록까지 해제합니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseAPITable );
        /** @brief 이미 바인딩한 API 테이블로 에디터 인스턴스를 만들고 초기화합니다. 디바이스가 없으면 만들지 않습니다. */
        [[nodiscard]] bool createEditorInstance();

        /**
         * @brief `initializeClient` 의 끝 — 에디터 모듈과 확장 모듈(의존과 함께)을 리로드 그래프에 등록합니다. 확장 하나가 실패해도 에디터는 뜹니다(그 확장만 없습니다).
         * @details 동적 모듈 구성에만 있다 — 정적 구성은 `initializeClient` 가 부르지 않고 오류로 멈춘다.
         * @return 매니페스트가 에디터 모듈을 껐거나 등록이 실패하거나 리로드 그래프가 깨졌으면 false 입니다(기동 실패).
         */
        [[nodiscard]] bool registerEditorModule( ILiveReloadManager* pLiveReloadManager );

        /** @brief 에디터 창의 최소 클라이언트 크기입니다. 메뉴바, 도크 다섯 개, 게임 뷰 툴바가 겹치지 않는 하한입니다(언리얼 메인 프레임과 유니티 에디터 창도 최소 크기를 둔다). */
        static constexpr uint32 kMinClientWidth  = 960;
        static constexpr uint32 kMinClientHeight = 540;

    private:
        /** @brief 에디터 모듈의 주인 창구입니다. 리로드 단계를 이 호스트의 에디터 콜백으로 잇는다(다중 상속 대신 멤버 객체). */
        class EditorReloadClient final : public IReloadableModuleClient
        {
        public:
            explicit EditorReloadClient( EditorGameModuleManager& owner )
                : _owner( owner ) {}
            void               onBeforeReload() override { _owner.onBeforeEditorReload(); }
            void               onAfterReload( ModuleLibraryHandle library ) override { _owner.onAfterEditorReload( library ); }
            void               onReloadFault( uint32 faultCode ) override { _owner.onEditorReloadFault( faultCode ); }
            [[nodiscard]] bool isLibraryUsable( ModuleLibraryHandle library ) override { return _owner.isEditorLibraryUsable( library ); }

        private:
            EditorGameModuleManager& _owner;
        };

        Engine&                       _engine;
        EditorReloadClient            _editorReloadClient{ *this };
        EditorAPI                     _editorAPI;
        EditorHandle                  _editor;
        unique_ptr<SubsystemScope>    _editorServices;      ///< 에디터 모듈에 넘긴 호스트 서비스 범위(`rebindModuleSubsystems` 가 새로 만든다)
        vector<EditorExtensionModule> _listEditorExtension; ///< 켜진 확장 모듈(로드 순서이며, `loadModuleLibraries` 가 모은다).

        uint8                  _bEditorModuleActive  : 1; ///< 매니페스트가 에디터 모듈을 켰는가(`loadModuleLibraries` 가 정한다).
        uint8                  _bFreshEditorInstance : 1; ///< 인스턴스를 새로 만든 뒤 첫 UI 프레임 전이다(`updateHostModuleUI` 가 렌더 스레드를 기다린 뒤 내린다).
        uint8                  _bRenderHooksBound    : 1; ///< 렌더 훅을 걸었다(`bindRenderHooks`)
        [[maybe_unused]] uint8 _reserved             : 5;
    };
} // namespace sw
