/**
 * @file DevTools/Host/EditorModuleHeadlessTasks.h
 * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 부르는 헤드리스 작업(임포트 · 로컬라이제이션 · 에셋 참조 검사)과 그 작업 표입니다.
 * @details 작업 하나는 표의 한 줄이다(`EditorModuleHeadlessTask` — 청하는 인자 · 도는 호스트 · 실행). 엔진의 `Headless` 단계에 올리는 줄(창 · RHI 없이 세우기 · 서버 거절)도
 *          같은 표에서 나온다 — 예전에는 인자 목록이 엔진 헤더(`HeadlessSubsystem.h`)와 App 의 작업 표에 두 벌이라 새 작업마다 두 곳을 고쳤고, 에디터 모듈의 인자가 배포 엔진의 공개 헤더에 있었다.
 *          개발 계층(`IHostDevLayer::runEditorModuleHeadlessTasks`)으로 호스트가 부른다 — 배포에는 이 폴더도 그 인자(`ArgumentList.xxx` 의 `Dev`)도 없다.
 *          모듈을 올리는 길의 정의는 동적 모듈 구성에만 있다(`EditorModuleHeadlessTasks.cpp`). 부르는 자리가 `build::kStaticModules` 로 갈라 정적 구성(웹 · Android 개발)에서는 버려지는 갈래다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/HeadlessTask.h"

#include "ModuleAPI/ABI/EditorAPI.h"

namespace sw
{
    /**
     * @class EditorModuleHeadlessSession
     * @brief 헤드리스 실행 한 번에 에디터 모듈을 한 번만 올리고 끝에 내립니다(임포트 · 로컬라이제이션 · 참조 검사가 함께 쓴다).
     * @details 예전에는 작업마다 모듈을 올리고(정적 등록 · 타입 등록 · ABI 대조) 내렸다 — `--import-textures --import-models --check-asset-refs` 한 실행이 세 번 올렸다.
     *          언리얼 커맨드릿이 엔진 · 모듈을 한 번 올린 뒤 일을 도는 것과 같다. 작업 표를 도는 쪽(`EditorModuleHeadlessTask::runRequested`)이 이 객체를 쥐고 작업에 넘긴다.
     *          정적 모듈 구성(배포 · 웹 · Android 개발)에는 에디터 모듈이 없어 아무것도 올리지 않는다(작업이 그 전에 실패로 답한다).
     */
    class EditorModuleHeadlessSession final
    {
    public:
        EditorModuleHeadlessSession();
        /** @brief 올린 모듈이 있으면 타입 등록을 걷고 내립니다. 이 실행에서 모듈을 쓴 작업 수를 로그로 남긴다(한 번 올림의 확인). */
        ~EditorModuleHeadlessSession();
        EditorModuleHeadlessSession( const EditorModuleHeadlessSession& )            = delete;
        EditorModuleHeadlessSession& operator=( const EditorModuleHeadlessSession& ) = delete;

        /** @brief 에디터 모듈을 처음 부를 때 올려(정적 등록 · 타입 등록 · ABI 대조) 진입점 @p pSymbol 을 찾습니다. 실패하면 nullptr 이고 다시 올리려 하지 않는다. */
        [[nodiscard]] void* findEntry( const utf8* pSymbol, const utf8* pPurpose );
        /** @brief 켜진 에디터 확장 모듈(`EditorExtension`)을 의존 순서로 올립니다(에디터 모듈 뒤 — 올린 것은 소멸이 거꾸로 내린다). 하나라도 못 올리면 false 입니다. */
        [[nodiscard]] bool loadExtensions();
        /** @brief 이 실행에서 모듈을 올린 횟수입니다(0 또는 1 — 테스트 · 로그용). */
        uint32 getLoadCount() const { return _loadCount; }

    private:
        /** @brief 올린 확장 모듈 하나입니다. */
        struct ExtensionLibrary
        {
            string _name;
            void*  _pLibrary{ nullptr };
        };

        vector<ExtensionLibrary> _listExtension;        ///< 올린 순서
        [[maybe_unused]] void*   _pLibrary;             ///< 올린 에디터 모듈의 OS 핸들(없으면 nullptr)
        uint32                   _loadCount;            ///< 올린 횟수
        [[maybe_unused]] uint32  _entryCallCount;       ///< 진입점을 찾은 작업 수
        [[maybe_unused]] uint8   _bLoadAttempted   : 1; ///< 올리기를 한 번 해 봤다(실패해도 다시 하지 않는다)
        [[maybe_unused]] uint8   _bTypesRegistered : 1;
        [[maybe_unused]] uint8   _reserved         : 6;
    };

    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 @p kind 원본을 임포트하거나 대조합니다(`App --import-textures`, `--import-models` 와 `--check-*`).
     * @details 헤드리스 부팅(창과 RHI 없음)에서 부릅니다. 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 해제한 뒤 모듈을 언로드합니다.
     *          Shipping 에는 에디터 모듈이 없어 실패합니다. 인스턴스 상태를 쓰지 않아 모듈 관리자 밖에 둔다
     *          (모듈 라이브러리를 직접 올리고 내리는 의존이 인스턴스 경로에 섞이지 않게).
     * @return 원본과 임포트 결과가 맞으면(임포트는 모두 성공하면) true 를 반환합니다.
     */
    [[nodiscard]] bool importAssetsWithEditorModule( EditorModuleHeadlessSession& session, EditorImportKind kind, bool bCheckOnly );
    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 로컬라이제이션 작업 @p task 를 실행합니다(`App --gather-text`, `--check-text`, `--import-po`, `--export-po`).
     * @details 임포트와 같은 경로입니다(`importAssetsWithEditorModule`). 타입 공급자 모듈이 모두 로드된 헤드리스 부팅에서 부릅니다. Shipping 에서는 실패합니다.
     * @param poPath `ImportPo` 가 읽을 PO 파일
     * @param projectArgument `-loc-project` 값(비면 기본 대상 전부)
     * @return 작업이 성공하면(확인 모드는 테이블이 최신이면) true 를 반환합니다.
     */
    [[nodiscard]] bool runLocalizationWithEditorModule( EditorModuleHeadlessSession& session, EditorLocalizationTask task, string_view poPath, string_view projectArgument );
    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 에셋 참조를 검사합니다(`App --check-asset-refs`). 임포트와 같은 경로이고 Shipping 에서는 실패합니다.
     * @return 끊어진 참조 · 목록에 없는 `AssetPath` 프로퍼티가 하나도 없으면 true 를 반환합니다.
     */
    [[nodiscard]] bool validateAssetReferencesWithEditorModule( EditorModuleHeadlessSession& session );
    /**
     * @brief 창 · GPU 없이 에디터 인스턴스를 세워(`EditorAPI::initializeHeadless`) 에디터 단위 테스트를 돌립니다(`App --test-editor`). Shipping 에서는 실패합니다.
     * @details 에디터 화면(활성 씬 · 프레임)이 필요한 케이스는 태그 `editorApp` 으로 빠져 에디터 자체 테스트 `editor.unitTests` 가 돈다.
     * @return 하나도 지지 않았으면 true 를 반환합니다.
     */
    [[nodiscard]] bool runUnitTestsWithEditorModule( EditorModuleHeadlessSession& session, const CommandLineManager& commandLine );

    /**
     * @struct EditorModuleHeadlessTask
     * @brief 에디터 모듈 작업 표의 한 줄입니다 — 청하는 인자 · 도는 호스트 · 실행. 새 작업은 인자를 `ArgumentList.xxx` 에 더하고 표(`EditorModuleHeadlessTasks.cpp`)에 한 줄을 더합니다.
     * @details 종료 코드 규칙: 청해진 줄을 표 순서대로 모두 돌리고(실패해도 뒤의 줄을 돌린다) 하나라도 실패했으면 실패다.
     */
    struct EditorModuleHeadlessTask
    {
        /** @brief 작업 본문입니다. @p editorModule 은 실행 한 번에 에디터 모듈을 한 번만 올리는 세션이다. 성공이면 true 입니다. */
        using RunFunction = bool ( * )( const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule );

        HeadlessTaskRequestFunction _pIsRequested; ///< 이 명령줄이 작업을 청했는가 — 엔진의 헤드리스 기동 판정과 실행 판정이 같은 함수다
        HeadlessTaskHost            _host;         ///< 도는 호스트 — 그 밖의 호스트에 청해지면 기동이 실패한다(`HeadlessTaskRegistration::selectAndRun`)
        RunFunction                 _pRun;

        /** @brief 표에서 청해진 작업을 차례로 돌립니다(세션 하나를 같이 쓴다). 실패한 작업이 없으면 true 입니다. */
        [[nodiscard]] static bool runRequested( const CommandLineManager& commandLine );
        /** @brief @p listTask 에서 청해진 작업을 차례로 돌립니다. 표와 떼어 둔 것은 규칙(청해진 줄만, 실패해도 계속)을 모듈 없이 테스트하기 위해서입니다. */
        [[nodiscard]] static bool runTable( span<const EditorModuleHeadlessTask> listTask, const CommandLineManager& commandLine, EditorModuleHeadlessSession& editorModule );
    };
} // namespace sw
