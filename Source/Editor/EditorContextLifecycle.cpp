/**
 * @file EditorContextLifecycle.cpp
 * @brief EditorContext 의 생성 · 초기화 · 종료입니다. 에디터 UI 매니저들을 실제로 만드는 곳입니다.
 *
 * @details 컨텍스트 조회(`editor::getSubsystem<EditorContext>()` · `EditorContext.cpp`)와 한 파일에 두지 않습니다. 그러면 **컨텍스트를 조회하기만 해도** 패널 · 팝업 ·
 *          인스펙터 매니저가 모두 링크에 끌려옵니다(그 끝은 ImGui 입니다). 생성 · 소멸(매니저 타입이 완전해야 하는 쪽)만
 *          이 TU 에 두어, EditorContext 를 조회만 하는 코드(EditorSceneCommands 단위 테스트 등)가 UI 없이 링크되게 합니다.
 *          모든 매니저를 만드는 조립점이라 셸과 같은 `Source/Editor` 바로 아래(최상위 층)에 둡니다. `Common/Workspace` 는
 *          ImGui 를 모르는 `EditorModel` 이 짓습니다(Source/Editor/CMakeLists.txt).
 */
#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Config/EditorToolDefaultsReload.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPanelService.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/ObjectCopyBuffer.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/EditorModuleUnloadListener.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw::editor
{
    EditorContext::EditorContext()
        : _scope{}
    {
        // 범위가 자기 주인을 안다 — `editor::getSubsystem<EditorContext>()` 는 지금 에디터 범위에서 이것을 찾는다(활성 컨텍스트 정적 포인터를 따로 두지 않는다).
        (void)_scope.bind( this ); // 새 범위라 겹치지 않는다
    }

    EditorContext::~EditorContext()
    {
        shutdown();
    }

    void EditorContext::initialize( EditorToolDefaults& toolDefaults )
    {
        // 도구 시드는 서브시스템이 서며 읽을 수 있다(입력 맵 경로 · 핫 리로드 확장자) — 그보다 먼저 범위에 빌려 건다. 내리는 것은 `shutdown`.
        const bool bToolDefaultsBound = _scope.bind( &toolDefaults );
        SW_ASSERT( bToolDefaultsBound );
        (void)bToolDefaultsBound;
        // 하위 객체는 목록이 소유한다. 넣는 순서는 뜻이 없고, 초기화 순서는 각자 선언한 의존이 정하며 종료 · 소멸은 그 역순이다.
        // 애셋 · 설정 핫 리로드(개발 기능)는 에디터가 켜져 있을 때만 감시하고, 버전 관리는 git LFS 를 비동기로 묻는다(각 initializeSubsystem).
        _scope.addSubsystem( make_unique<EditorStateStore>() ); // 한 파일 안의 상태(감시 값이 읽는 값 · 캐시)의 주인 — 컨텍스트와 함께 생기고 사라진다
        EditorSelection&    selection    = _scope.addSubsystem( make_unique<EditorSelection>() );
        EditorPanelManager& panelManager = _scope.addSubsystem( make_unique<EditorPanelManager>() );
        EditorPanelService& panelService = _scope.addSubsystem( make_unique<EditorPanelService>( *this, panelManager ) );
        (void)_scope.expose<IEditorPanelService>( panelService ); // 패널 매니저(GUI)를 모르는 레이어가 인터페이스로 찾는다 — 새 범위라 겹치지 않는다
        _scope.addSubsystem( make_unique<EditorWorkspace>( &selection ) );
        _scope.addSubsystem( make_unique<EditorNotificationManager>() );
        _scope.addSubsystem( make_unique<EditorCommandRegistry>() );
        // 메인 되돌리기 기록과 복사 버퍼 — 인스턴스가 내려갈 때 호스트의 맡김 칸에 맡기고 새 인스턴스가 되찾는다(리로드 · 백엔드 교체).
        EditorUndoHistory& undoHistory = _scope.addSubsystem( make_unique<EditorUndoHistory>() );
        const bool         bStackBound = _scope.bind( &undoHistory.getStack() ); // `findSubsystem<CommandStack>()` 이 찾는다
        SW_ASSERT( bStackBound );
        (void)bStackBound;
        _scope.addSubsystem( make_unique<ObjectCopyBuffer>() );
        _scope.addSubsystem( make_unique<EditorObjectEditBindings>() ); // 커맨드 스택 알림 칸의 주인 — 컨텍스트와 함께 놓는다
        _scope.addSubsystem( make_unique<EditorInputActions>() );       // 에디터 단축키의 입력 맵(editor.input.xml) — 명령 등록부 · 패널 매니저 뒤
        _scope.addSubsystem( make_unique<EditorPopupManager>() );
        _scope.addSubsystem( make_unique<AssetHotReload>() );
        _scope.addSubsystem( make_unique<EditorToolDefaultsReload>() ); // `Config/` 감시는 엔진(ConfigReloadSubsystem)이 한다 — 편집기는 도구 기본값 처리기만 건다
        _scope.addSubsystem( sw::make_unique<EditorSourceControl>( EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() ) ) );
        _scope.addSubsystem( make_unique<InspectorComponentManager>() );
        _scope.addSubsystem( make_unique<InspectorPropertyManager>() );
        _scope.addSubsystem( make_unique<EditorDefaultObjects>() );
        _scope.addSubsystem( make_unique<EditorNetPlaySession>() );
        _scope.addSubsystem( make_unique<EditorRenderViews>() );
        _scope.addSubsystem( make_unique<EditorPlaySession>() );
        _scope.addSubsystem( make_unique<EditorTheme>() );
        _scope.addSubsystem( make_unique<EditorUICapture>() );
        _scope.addSubsystem( make_unique<EditorExtensionSubsystems>( _scope ) ); // 확장이 기여한 서브시스템(SW_EDITOR_SUBSYSTEM) — 프레임 앞 syncExtensionRegistrations 가 만든다
        // 확장 모듈이 언로드되기 전에 그 모듈의 등록 줄로 만든 인스턴스를 지운다 — 훑는 매니저들에 의존하므로 그들보다 먼저 종료된다.
        _scope.addSubsystem( make_unique<EditorModuleUnloadListener>( *this ) );

        setActive( this );

        // 에디터 UI 문구 — 엔진 테이블과 따로 로드해 에디터가 있는 동안만 쓰인다. 탑재하지 못하면 SW_LOCKEY 위치에 키 이름이 보인다.
        // 주의: 지역화 관리자는 엔진 서비스다. 멤버 `findSubsystem` 은 이 컨텍스트의 범위만 보므로 `editor::findSubsystem`(호스트까지 찾는다)을 쓴다.
        LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>();
        if ( pLocalization == nullptr )
            SW_LOG_WARNING( "No LocalizationManager - editor text shows its keys" );
        else if ( pLocalization->mountProject( AssetID{ EditorPaths::kLocalizationProject }, LocalizationScope::Editor ) == false )
            SW_LOG_WARNING( "Editor localization project '%#' is not (fully) loaded", EditorPaths::kLocalizationProject );

        if ( _scope.getCollection().initializeAll( EngineSubsystemHost::kClient ) == false )
            SW_LOG_ERROR( "Editor subsystems did not all initialize: %#", _scope.getCollection().getError().c_str() );
    }

    void EditorContext::shutdown()
    {
        // 범위를 허물기 전에 진행 중인 워커 일(임포트 · 색인 스캔 · 백그라운드 잡)이 끝나기를 기다린다. 워커가 이 범위의 서비스를 쥔 채 범위가 사라지면 해제된 메모리를 쓴다.
        // 워커 본문은 범위를 직접 찾지 않지만(`findEditorScope`), 제출 시점에 값으로 받은 서비스 포인터는 이 범위가 소유한다.
        // 엔진 전체(`TaskManager::waitAll`)가 아니라 에디터가 낸 잡만 기다린다 — 엔진의 영구 태스크는 끝나지 않는다.
        EditorStateStore* const pStore       = findSubsystem<EditorStateStore>();
        TaskManager* const      pTaskManager = findSubsystem<TaskManager>();
        if ( pStore != nullptr && pTaskManager != nullptr )
            pTaskManager->waitStage( pStore->get<EditorBackgroundWork>()._stage, "EditorContext::shutdown" ); // 중단은 앞서 올렸다(`requestBackgroundStop`) — 오래 걸리면 남은 태스크가 주기적으로 오류로 남는다

        if ( findEditorScope() == &_scope )
            setActive( nullptr );

        LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>(); // 엔진 서비스 — 탑재와 같은 진입점
        if ( pLocalization != nullptr )
            pLocalization->unmountProjects( LocalizationScope::Editor );

        // 초기화한 것을 역순으로 종료하고, 모두를 역순으로 소멸시킨다(넷 플레이 세션의 자식 프로세스 종료도 여기 — 엔진 로거는 아직 있다).
        // 확장이 기여한 서브시스템은 내장 서브시스템을 쓸 수 있다 — 그들보다 먼저 종료한다.
        if ( EditorExtensionSubsystems* pExtensions = findSubsystem<EditorExtensionSubsystems>() )
            pExtensions->releaseAll();
        _scope.destroyAll();                 // 뷰 렌더 타깃은 EditorRenderViews 의 종료가 놓는다
        _scope.unbind<EditorToolDefaults>(); // 셸이 소유한 값 — 다음 `initialize` 가 다시 건다
        _scope.unbind<CommandStack>();       // `EditorUndoHistory` 가 소유한 스택 — 방금 사라졌다
    }

    void EditorContext::setDockLayout( EditorDockLayout* pDockLayout )
    {
        _scope.unbind<EditorDockLayout>();
        if ( pDockLayout != nullptr )
            (void)_scope.bind( pDockLayout ); // 방금 풀었으니 겹치지 않는다
    }

    void EditorContext::syncExtensionRegistrations()
    {
        getSubsystem<EditorExtensionSubsystems>().syncWithRegistry(); // 패널이 그리기 전에 찾을 수 있게 먼저
        getSubsystem<EditorPanelManager>().syncWithRegistry( getSubsystem<EditorRenderViews>().getRHIDevice() );
        getSubsystem<EditorPopupManager>().syncWithRegistry();
        getSubsystem<InspectorComponentManager>().syncWithRegistry();
        getSubsystem<InspectorPropertyManager>().syncWithRegistry();
        EditorCommandGUI::syncWithRegistry();
    }
} // namespace sw::editor
