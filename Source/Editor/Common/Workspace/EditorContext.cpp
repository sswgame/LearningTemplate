/**
 * @file EditorContext.cpp
 * @brief EditorContext 의 범위와 생명 주기(생성 · 초기화 · 종료 · 활성 전환)입니다.
 * @details 어느 하위 객체를 넣는지는 모르고 셸이 넘긴 조립점(`EditorSubsystemSet::install`)에 맡긴다. 그래서 컨텍스트를 쓰기만 하는 코드(단위 테스트)가
 *          패널 · 팝업 · 인스펙터 매니저(그 끝은 ImGui)를 링크에 끌어오지 않는다. 언리얼 `UEditorEngine` 이 서브시스템 컬렉션을 들기만 하는 것과 같다.
 */
#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include "Core/Diagnostics/SubsystemTimingReport.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw::editor
{
    void EditorContext::setActive( EditorContext* pContext )
    {
        setEditorScope( pContext != nullptr ? &pContext->_scope : nullptr );
    }

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

    void EditorContext::initialize( EditorToolDefaults& toolDefaults, EditorIconGlyphTable& iconGlyphs, const EditorComponentIconTable& componentIcons, const SubsystemInstaller install )
    {
        // 도구 시드 · 아이콘 표는 서브시스템이 서며 읽을 수 있다(입력 맵 경로 · 핫 리로드 확장자 · 아이콘 이름) — 그보다 먼저 범위에 빌려 건다. 내리는 것은 `shutdown`.
        const bool bShellDataBound = _scope.bind( &toolDefaults ) && _scope.bind( &iconGlyphs ) && _scope.bind( &componentIcons );
        SW_ASSERT( bShellDataBound );
        (void)bShellDataBound;
        // 어느 하위 객체가 있는지는 조립점(`install`, 셸이 넘긴다)이 정한다 — 컨텍스트는 범위와 생명 주기만 안다(패널 · 팝업 · 인스펙터 매니저를 모른다).
        SW_ASSERT( install != nullptr );
        install( _scope, *this );

        setActive( this );

        // 에디터 UI 문구 — 엔진 테이블과 따로 로드해 에디터가 있는 동안만 쓰인다. 탑재하지 못하면 SW_LOCKEY 위치에 키 이름이 보인다.
        // 주의: 지역화 관리자는 엔진 서비스다. 멤버 `findSubsystem` 은 이 컨텍스트의 범위만 보므로 `editor::findSubsystem`(호스트까지 찾는다)을 쓴다.
        LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>();
        if ( pLocalization == nullptr )
            SW_LOG_WARNING_ID( "EditorContext.NoLocalizationManagerEditorText", "No LocalizationManager - editor text shows its keys" );
        else if ( pLocalization->mountProject( AssetID{ EditorPaths::kLocalizationProject }, LocalizationScope::Editor ) == false )
            SW_LOG_WARNING_ID( "EditorContext.EditorLocalizationProjectNot", "Editor localization project '%#' is not (fully) loaded", EditorPaths::kLocalizationProject );

        // 종료 시간을 서브시스템마다 잰다 — 백엔드 교체 · 리로드가 에디터를 내리고 다시 세우므로 그 몫이 어디서 드는지 줄 하나로 보인다.
        SubsystemTimingReport::enableTiming( _scope.getCollection() );
        if ( _scope.getCollection().initializeAll( EngineSubsystemHost::kClient ) == false )
            SW_LOG_ERROR_ID( "EditorContext.EditorSubsystemsDidNot", "Editor subsystems did not all initialize: %#", _scope.getCollection().getError().c_str() );
    }

    void EditorContext::shutdown()
    {
        // 범위를 허물기 전에 진행 중인 워커 일(임포트 · 색인 스캔 · 백그라운드 잡)이 끝나기를 기다린다. 워커가 이 범위의 서비스를 쥔 채 범위가 사라지면 해제된 메모리를 쓴다.
        // 워커 본문은 범위를 직접 찾지 않지만(`findEditorScope`), 제출 시점에 값으로 받은 서비스 포인터는 이 범위가 소유한다.
        // 엔진 전체(`TaskManager::waitAll`)가 아니라 에디터가 낸 잡만 기다린다 — 엔진의 영구 태스크는 끝나지 않는다.
        const Stopwatch         waitWatch;
        EditorStateStore* const pStore       = findSubsystem<EditorStateStore>();
        TaskManager* const      pTaskManager = findSubsystem<TaskManager>();
        if ( pStore != nullptr && pTaskManager != nullptr )
            pTaskManager->waitStage( pStore->get<EditorBackgroundJob>()._stage, "EditorContext::shutdown" ); // 중단은 앞서 올렸다(`requestBackgroundStop`) — 오래 걸리면 남은 태스크가 주기적으로 오류로 남는다
        const uint64 waitMicroseconds = waitWatch.getElapsedMicroseconds();

        if ( findEditorScope() == &_scope )
            setActive( nullptr );

        LocalizationManager* const pLocalization = editor::findSubsystem<LocalizationManager>(); // 엔진 서비스 — 탑재와 같은 진입점
        if ( pLocalization != nullptr )
            pLocalization->unmountProjects( LocalizationScope::Editor );

        // 초기화한 것을 역순으로 종료하고, 모두를 역순으로 소멸시킨다(넷 플레이 세션의 자식 프로세스 종료도 여기 — 엔진 로거는 아직 있다).
        // 확장이 기여한 서브시스템은 내장 서브시스템을 쓸 수 있다 — 그들보다 먼저 종료한다.
        if ( EditorExtensionSubsystems* pExtensions = findSubsystem<EditorExtensionSubsystems>() )
            pExtensions->releaseAll();
        _scope.shutdownAll();
        SW_LOG_INFO_ID( "EditorContext.ShutdownMs", "Editor context shutdown (ms): background work %#, subsystems: %#", waitMicroseconds / 1000,
                        SubsystemTimingReport::describe( _scope.getCollection().getShutdownTimings() ).c_str() );
        _scope.destroyAll();                 // 뷰 렌더 타깃은 EditorRenderViews 의 종료가 놓는다
        _scope.unbind<EditorToolDefaults>(); // 셸이 소유한 값 — 다음 `initialize` 가 다시 건다
        _scope.unbind<EditorIconGlyphTable>();
        _scope.unbind<const EditorComponentIconTable>();
        _scope.unbind<CommandStack>(); // `EditorUndoHistory` 가 소유한 스택 — 방금 사라졌다
    }

    void EditorContext::setDockLayout( EditorDockLayout* pDockLayout )
    {
        _scope.unbind<EditorDockLayout>();
        if ( pDockLayout != nullptr )
            (void)_scope.bind( pDockLayout ); // 방금 풀었으니 겹치지 않는다
    }
} // namespace sw::editor
