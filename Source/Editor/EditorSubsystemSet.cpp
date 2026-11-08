/**
 * @file EditorSubsystemSet.cpp
 * @brief 에디터 범위에 들어가는 내장 하위 객체 목록과, 확장 등록과 맞추는 순서입니다.
 * @details 모든 매니저를 만드는 조립점이라 셸과 같은 `Source/Editor` 바로 아래(최상위 층)에 둡니다. `Common/Workspace` 는 ImGui 를 모르는 `EditorModel` 이 짓습니다.
 */
#include "pch.h"

#include "Editor/EditorSubsystemSet.h"

#include "Core/File/ProcessRoots.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Config/EditorToolDefaultsReload.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPanelService.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSelectionSync.h"
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

namespace sw::editor
{
    void EditorSubsystemSet::install( SubsystemScope& scope, EditorContext& context )
    {
        // 하위 객체는 목록이 소유한다. 넣는 순서는 뜻이 없고, 초기화 순서는 각자 선언한 의존이 정하며 종료 · 소멸은 그 역순이다.
        // 애셋 · 설정 핫 리로드(개발 기능)는 에디터가 켜져 있을 때만 감시하고, 버전 관리는 git LFS 를 비동기로 묻는다(각 initializeSubsystem).
        scope.emplaceSubsystem<EditorStateStore>(); // 한 파일 안의 상태(감시 값이 읽는 값 · 캐시)의 주인 — 컨텍스트와 함께 생기고 사라진다
        EditorSelection&    selection    = scope.emplaceSubsystem<EditorSelection>();
        EditorPanelManager& panelManager = scope.emplaceSubsystem<EditorPanelManager>();
        EditorPanelService& panelService = scope.emplaceSubsystem<EditorPanelService>( context, panelManager );
        (void)scope.expose<IEditorPanelService>( panelService ); // 패널 매니저(GUI)를 모르는 레이어가 인터페이스로 찾는다 — 새 범위라 겹치지 않는다
        scope.emplaceSubsystem<EditorWorkspace>( &selection );
        scope.emplaceSubsystem<EditorNotificationManager>();
        scope.emplaceSubsystem<EditorCommandRegistry>();
        // 메인 되돌리기 기록과 복사 버퍼 — 인스턴스가 내려갈 때 호스트의 맡김 칸에 맡기고 새 인스턴스가 되찾는다(리로드 · 백엔드 교체).
        EditorUndoHistory& undoHistory = scope.emplaceSubsystem<EditorUndoHistory>();
        const bool         bStackBound = scope.bind( &undoHistory.getStack() ); // `findSubsystem<CommandStack>()` 이 찾는다
        SW_ASSERT( bStackBound );
        (void)bStackBound;
        scope.emplaceSubsystem<ObjectCopyBuffer>();
        scope.emplaceSubsystem<EditorSelectionSync>();      // 선택 동기화 채널 — 문맥마다 고른 애셋 한 칸(선택은 옮기지 않는다)
        scope.emplaceSubsystem<EditorObjectEditBindings>(); // 커맨드 스택 알림 칸의 주인 — 컨텍스트와 함께 놓는다
        scope.emplaceSubsystem<EditorInputActions>();       // 에디터 단축키의 입력 맵(editor.input.xml) — 명령 등록부 · 패널 매니저 뒤
        scope.emplaceSubsystem<EditorPopupManager>();
        scope.emplaceSubsystem<AssetHotReload>();
        scope.emplaceSubsystem<EditorToolDefaultsReload>(); // `Config/` 감시는 엔진(ConfigReloadSubsystem)이 한다 — 편집기는 도구 기본값 처리기만 건다
        scope.emplaceSubsystem<EditorSourceControl>( ProcessRoots::get()._project.str() );
        scope.emplaceSubsystem<InspectorComponentManager>();
        scope.emplaceSubsystem<InspectorPropertyManager>();
        scope.emplaceSubsystem<EditorDefaultObjects>();
        scope.emplaceSubsystem<EditorNetPlaySession>();
        scope.emplaceSubsystem<EditorRenderViews>();
        scope.emplaceSubsystem<EditorPlaySession>();
        scope.emplaceSubsystem<EditorTheme>();
        scope.emplaceSubsystem<EditorUICapture>();
        scope.emplaceSubsystem<EditorExtensionSubsystems>( scope ); // 확장이 기여한 서브시스템(SW_EDITOR_SUBSYSTEM) — 프레임 앞 syncExtensionRegistrations 가 만든다
        // 확장 모듈이 언로드되기 전에 그 모듈의 등록 줄로 만든 인스턴스를 지운다 — 훑는 매니저들에 의존하므로 그들보다 먼저 종료된다.
        scope.emplaceSubsystem<EditorModuleUnloadListener>( context );
    }

    void EditorSubsystemSet::syncExtensionRegistrations( EditorContext& context )
    {
        context.getSubsystem<EditorExtensionSubsystems>().syncWithRegistry(); // 패널이 그리기 전에 찾을 수 있게 먼저
        context.getSubsystem<EditorPanelManager>().syncWithRegistry();
        context.getSubsystem<EditorPopupManager>().syncWithRegistry();
        context.getSubsystem<InspectorComponentManager>().syncWithRegistry();
        context.getSubsystem<InspectorPropertyManager>().syncWithRegistry();
        EditorCommandGUI::syncWithRegistry();
    }
} // namespace sw::editor
