/**
 * @file EditorContextLifecycle.cpp
 * @brief EditorContext 의 생성 · 초기화 · 종료입니다. 에디터 UI 매니저들을 실제로 만드는 곳입니다.
 *
 * @details EditorContext::get() 과 한 파일에 두지 말 것 — 그러면 **컨텍스트를 조회하기만 해도** 패널 · 팝업 ·
 *          인스펙터 매니저가 모두 링크에 끌려옵니다(그 끝은 ImGui 입니다). 생성 · 소멸(매니저 타입이 완전해야 하는 쪽)만
 *          이 TU 에 두어, EditorContext 를 조회만 하는 코드(EditorSceneCommands 단위 테스트 등)가 UI 없이 링크되게 합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/EditorPaths.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorPanelHost.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorModuleUnloadListener.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw::editor
{
    EditorContext::EditorContext()
        : _scope{}
    {
        // 범위가 자기 주인을 안다 — `EditorContext::get()` 은 지금 에디터 범위에서 이것을 찾는다(활성 컨텍스트 정적 포인터를 따로 두지 않는다).
        (void)_scope.bind( this ); // 새 범위라 겹치지 않는다
    }

    EditorContext::~EditorContext()
    {
        shutdown();
    }

    void EditorContext::initialize()
    {
        // 하위 객체는 목록이 소유한다. 넣는 순서는 뜻이 없고, 초기화 순서는 각자 선언한 의존이 정하며 종료 · 소멸은 그 역순이다.
        // 애셋 · 설정 핫 리로드(개발 기능)는 에디터가 켜져 있을 때만 감시하고, 버전 관리는 git LFS 를 비동기로 묻는다(각 initializeSubsystem).
        EditorSelection&    selection    = _scope.addSubsystem( make_unique<EditorSelection>() );
        EditorPanelManager& panelManager = _scope.addSubsystem( make_unique<EditorPanelManager>() );
        EditorPanelHost&    panelHost    = _scope.addSubsystem( make_unique<EditorPanelHost>( *this, panelManager ) );
        (void)_scope.expose<IEditorPanelHost>( panelHost ); // 패널 매니저(GUI)를 모르는 레이어가 인터페이스로 찾는다 — 새 범위라 겹치지 않는다
        _scope.addSubsystem( make_unique<EditorWorkspace>( &selection ) );
        _scope.addSubsystem( make_unique<EditorNotificationManager>() );
        _scope.addSubsystem( make_unique<EditorCommandRegistry>() );
        _scope.addSubsystem( make_unique<EditorPopupManager>() );
        _scope.addSubsystem( make_unique<AssetHotReload>() );
        _scope.addSubsystem( make_unique<ConfigHotReload>() );
        _scope.addSubsystem( make_unique<EditorAssetValidation>() );
        _scope.addSubsystem( sw::make_unique<EditorSourceControl>( EditorPaths::getProjectRootPath() ) );
        _scope.addSubsystem( make_unique<InspectorComponentManager>() );
        _scope.addSubsystem( make_unique<InspectorPropertyManager>() );
        _scope.addSubsystem( make_unique<EditorDefaultObjects>() );
        _scope.addSubsystem( make_unique<EditorNetPlaySession>() );
        _scope.addSubsystem( make_unique<EditorRenderViews>() );
        _scope.addSubsystem( make_unique<EditorPlaySessionState>() );
        _scope.addSubsystem( make_unique<EditorTheme>() );
        _scope.addSubsystem( make_unique<EditorExtensionSubsystems>( _scope ) ); // 확장이 기여한 서브시스템(SW_EDITOR_SUBSYSTEM) — 프레임 앞 syncExtensionRegistrations 가 만든다
        // 확장 모듈이 언로드되기 전에 그 모듈의 등록 줄로 만든 인스턴스를 지운다 — 훑는 매니저들에 의존하므로 그들보다 먼저 내려간다.
        _scope.addSubsystem( make_unique<EditorModuleUnloadListener>( *this ) );

        setActive( this );

        // 에디터 UI 문구 — 엔진 표와 따로 올려 에디터가 있는 동안만 쓰인다(없으면 SW_LOCTEXT 가 영어 원문을 돌려준다).
        if ( LocalizationManager* pLocalization = findSubsystem<LocalizationManager>() )
        {
            if ( pLocalization->mountProject( EditorPaths::kLocalizationProject, LocalizationScope::Editor ) == false )
                SW_LOG_WARNING( "Editor localization project '%#' is not (fully) loaded", EditorPaths::kLocalizationProject );
        }

        if ( _scope.getCollection().initializeAll( EngineSubsystemHost::kClient ) == false )
            SW_LOG_ERROR( "Editor subsystems did not all initialize: %#", _scope.getCollection().getError().c_str() );
    }

    void EditorContext::shutdown()
    {
        if ( findEditorScope() == &_scope )
            setActive( nullptr );

        if ( LocalizationManager* pLocalization = findSubsystem<LocalizationManager>() )
            pLocalization->unmountProjects( LocalizationScope::Editor );

        // 초기화한 것을 역순으로 종료하고, 모두를 역순으로 소멸시킨다(넷 플레이 세션의 자식 프로세스 종료도 여기 — 엔진 로거는 아직 있다).
        // 확장이 기여한 서브시스템은 내장 서브시스템을 쓸 수 있다 — 그들보다 먼저 내린다.
        if ( EditorExtensionSubsystems* pExtensions = findSubsystem<EditorExtensionSubsystems>() )
            pExtensions->releaseAll();
        _scope.destroyAll(); // 뷰 렌더 타깃은 EditorRenderViews 의 종료가 놓는다
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
