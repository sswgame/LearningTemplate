/**
 * @file EditorModuleHost.h
 * @brief App 전용 모듈 호스트입니다 — 공통 `ModuleHost`(게임 모듈) 위에 에디터 인스턴스 · 에디터 리로드 · 에디터 UI · RHI 교체 뒤 재초기화를 얹습니다.
 * @note 의존은 이쪽에서 `ModuleHost` 로 한 방향입니다. 전용 서버(`Server`)는 이 클래스를 링크하지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "ModuleHost/ModuleHost.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

namespace sw
{
    struct NativeWindowEvent;

    class CameraComponent;

    /**
     * @class EditorModuleHost
     * @brief 에디터 모듈의 수명 주기와 API 바인딩을 게임 모듈 호스트 위에 얹습니다. App 이 unique_ptr 로 소유합니다.
     * @details 에디터를 켜지 않으면(`initialize` 의 bEnableEditor) 에디터 쪽은 모두 아무 일도 하지 않고, 공통 호스트와 같게 돕니다.
     */
    class EditorModuleHost final : public ModuleHost
    {
    public:
        EditorModuleHost();
        ~EditorModuleHost() override;

        /** @brief 매니페스트가 에디터 모듈을 켰는지 기억하고 공통 `ModuleHost::loadModuleImages` 를 부릅니다. */
        [[nodiscard]] bool loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution );
        /**
         * @brief LiveReloadManager 에 콜백을 등록하고 게임 · 에디터 인스턴스를 만듭니다(게임 모듈 이미지는 `loadModuleImages` 가 이미 올렸다).
         * @details **게임이 먼저, 에디터가 나중**입니다 — 게임이 처음 여는 씬(실행 설정의 시작 씬 · 타이틀 · 시작 맵)을 요청한 뒤에 에디터가 제 시작 씬
         *          (`-gv_editorStartupScene`)을 요청해야, 마지막 요청을 남기는 씬 매니저에서 에디터의 것이 열립니다.
         * @param pLiveReloadManager Dev 모드 전용 모듈 매니저(Shipping 에서는 nullptr)
         * @param pRHI 활성 RHI
         * @param pWindow 플랫폼 창
         * @param pRenderThread 렌더 스레드(워커 비우기용)
         * @param bEnableEditor 에디터 모드 여부
         */
        bool initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor );

        /**
         * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 @p kind 원본을 임포트하거나 대조합니다(`App --import-textures` · `--import-models` 와 `--check-*`).
         * @details 헤드리스 부팅(창 · RHI 없음)에서 부릅니다. 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 걷은 뒤 모듈을 내립니다.
         *          Shipping 에는 에디터 모듈이 없어 실패합니다.
         * @return 원본과 임포트 결과가 맞으면(임포트는 모두 성공하면) true
         */
        [[nodiscard]] static bool importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly );
        /**
         * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 로컬라이제이션 작업 @p task 를 돌립니다(`App --gather-text` · `--check-text` · `--import-po` · `--export-po`).
         * @details 임포트와 같은 길입니다(`importAssetsWithEditorModule`). 타입 공급자 모듈이 모두 올라온 헤드리스 부팅에서 부릅니다. Shipping 에서는 실패합니다.
         * @param poPath `ImportPo` 가 읽을 PO 파일 · @param projectArgument `-loc-project` 값(비면 기본 대상 전부)
         * @return 작업이 성공하면(확인 모드는 표가 최신이면) true
         */
        [[nodiscard]] static bool runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument );

        // 프레임 단위 처리
        /**
         * @brief 메인 스레드에서 에디터 UI 와 플랫폼 창을 갱신하고, 그 결과(게임 뷰포트 RT · 씬 틱 여부)를 프레임 상태에 확정합니다.
         * @details 에디터가 없으면 바로 돌아오며, 그때 프레임 상태는 "백버퍼 + 씬 틱" 기본값입니다.
         */
        void updateEditorUi( float32 deltaTime );
        /** @brief 월드 틱 뒤에 에디터의 Step 을 소비합니다. */
        void endEditorFrame();
        /** @brief 네이티브 창 이벤트를 에디터에 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief 이번 프레임 씬 뷰를 그리는 에디터 카메라를 에디터에서 조회합니다. */
        CameraComponent* getSceneViewCamera() const;

        /** @brief 에디터 인스턴스 핸들을 반환합니다. App 의 Present 훅이 씁니다. */
        EditorHandle getEditor() const { return _editor; }
        /** @brief EditorAPI 테이블을 반환합니다. App 의 Present 훅이 씁니다. */
        const EditorAPI& getEditorApi() const { return _editorApi; }

        // LiveReload 콜백 — 에디터
        /** @brief 에디터 모듈을 내리기 직전에 불립니다. 인스턴스와 API 테이블을 놓습니다. */
        void onBeforeEditorReload();
        /** @brief 에디터 모듈이 다시 올라온 직후에 불립니다. API 를 다시 바인딩하고 인스턴스를 만듭니다. */
        void onAfterEditorReload( void* pLibraryModule );
        /**
         * @brief 새 에디터 이미지가 이 호스트와 같은 API 표로 빌드됐는지 봅니다(ABI 버전 · 지문 · `exportEditorApi`). 옛 이미지를 내리기 전에 불립니다.
         * @details 여기서 거절하면 옛 에디터가 그대로 돈다. 같은 검사를 옛 것을 내린 뒤(`onAfterEditorReload`)에야 하면 거절이 곧 에디터를 잃는 일이 된다.
         */
        bool isEditorImageUsable( void* pLibraryModule ) const;
        /**
         * @brief 새 에디터 모듈이 리로드 직후 결함을 냈을 때 불립니다. 인스턴스와 API 표를 **모듈을 부르지 않고** 버립니다.
         * @param faultCode 예외 코드(Windows) · 시그널 번호(리눅스)
         */
        void onEditorReloadFault( uint32 faultCode );

        /** @brief 에디터 DLL 에서 API 테이블을 받아 바인딩합니다. ABI 버전 · 스탬프가 다르면 실패합니다. */
        bool bindEditorApi( void* pLibraryModule );
#if !defined( SW_SHIPPING )
        /**
         * @brief 이미 만든 에디터 인스턴스와 그 API 표를 호스트에 붙입니다.
         * @details 정상 경로는 `bindEditorApi` → `createEditorInstance` 다. 이 창구는 모듈 DLL · 디바이스 없이 가짜 API 표로 호스트의 순서
         *          (시뮬레이션 멈춤 → shutdown → destroy)를 시험하려고 둔다. 에디터 모드(`initialize` 의 bEnableEditor)일 때만 에디터로 쓰인다.
         *          시험 전용 창구라 배포본에는 없다 — 배포본에서 호스트의 API 표를 바꿔 끼울 길을 남기지 않는다.
         */
        void attachEditorInstance( const EditorAPI& editorApi, EditorHandle editor );
#endif

        /** @brief RHI 핫스왑 뒤 게임 → 에디터 순서(기동과 같다)로 다시 초기화합니다. 실패하면 false 입니다. */
        bool reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule );

    protected:
        /**
         * @brief 이번 프레임에 게임 로직을 돌려야 하는지 에디터에 묻습니다.
         * @details 에디터가 없으면 항상 돕니다. 에디터가 있으면 Play 중일 때만 돕니다. 편집 중에 게임 update 가 돌면 저장하지 않은
         *          씬을 게임 코드가 바꿔 버립니다.
         */
        bool queryGameplayActive() const override;
        bool controlsWorldPlay() const override { return hasEditor(); }
        /**
         * @brief 무엇을 내리든 에디터 시뮬레이션부터 멈춥니다.
         * @details 게임만 내리면 에디터가 사라진 게임을 계속 돌리려 하고, 에디터를 내리면 — 월드 플레이 상태는 에디터보다 오래 사는 `SceneManager` ·
         *          오브젝트 매니저에 있으므로 — 새로 만든 에디터는 멈춤으로 시작하는데 월드는 계속 플레이 중이다(편집한 컴포넌트가 onBeginPlay 를 받고,
         *          플레이 스냅샷은 복원되지 않는다). 멈춤이 플레이를 끝내고 스냅샷을 되돌린다 — 에디터 컨텍스트가 아직 있는 동안이어야 한다.
         */
        void onBeforeSuspendModules() override;
        void suspendHostModule( bool bReleaseApiTable ) override;
        /**
         * @brief 에디터의 "렌더 대기" 표시를 버립니다.
         * @details 그 표시는 렌더 스레드의 postPresent 만 풀 수 있는데, 그 스레드는 방금 일을 끝내고 쉬고 있다. 알려 주지 않으면 다음 updateUi 나
         *          shutdown 이 waitForDrawSnapshotIdle 에서 영원히 돌아오지 않는다. 에디터 모듈 핫 리로드가 실제로 여기서 멈췄다.
         */
        void onRenderWorkersDrained() override;

    private:
        /** @brief 에디터를 실제로 부를 수 있는 상태인지 확인합니다(모드가 켜져 있고 인스턴스가 살아 있음). */
        bool hasEditor() const { return _bEnableEditor == SW_TRUE && _editor != nullptr; }
        /** @brief 에디터가 월드를 틱해야 하는지 묻습니다. Pause 이면서 Step 이 아니면 false 입니다. */
        bool queryTickScene() const;
        /** @brief 이번 프레임 게임 뷰 · 씬 뷰 RT 핸들과 크기를 에디터에서 조회해 프레임 상태에 담습니다(보이지 않는 패널의 뷰는 비어 온다). */
        void sampleViewTargets();

        /** @brief ModuleService 를 다시 만들어 에디터 모듈에 넘깁니다. */
        void rebindEditorService();
        /**
         * @brief 에디터 인스턴스를 shutdown → destroy 하고 핸들을 비웁니다.
         * @param bReleaseApiTable true 면 API 테이블과 타입 등록까지 놓습니다(모듈 언로드 직전). RHI 핫스왑처럼 **같은 모듈로 다시
         *        만들** 때는 false 이고, 테이블을 그대로 재사용합니다.
         */
        void destroyEditorInstance( bool bReleaseApiTable );
        /** @brief 이미 바인딩한 API 테이블로 에디터 인스턴스를 만들고 초기화합니다. 디바이스가 없으면 만들지 않습니다. */
        [[nodiscard]] bool createEditorInstance();
        /** @brief RHI 교체 뒤 에디터를 다시 세웁니다. 테이블이 비었으면 모듈에서 다시 바인딩합니다. */
        [[nodiscard]] bool recreateEditorInstance( void* pEditorModule );

    private:
        EditorAPI    _editorApi;
        EditorHandle _editor;

        uint8                  _bEnableEditor       : 1;
        uint8                  _bEditorModuleActive : 1; ///< 매니페스트가 에디터 모듈을 켰는가(`loadModuleImages` 가 정한다)
        [[maybe_unused]] uint8 _reserved            : 6;
    };
} // namespace sw
