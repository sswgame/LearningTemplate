/**
 * @file App/EditorModuleManagerDevTools.cpp
 * @brief 개발 구성의 에디터 모듈 등록(리로드 그래프 · 확장 모듈)과 에디터 범위의 개발 도구 서비스입니다.
 * @details 배포는 `EditorModuleManagerDevToolsShipping.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
 */
#include "pch.h"

#include "App/EditorModuleManager.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"
#include "DevTools/DebugHUD/DebugHUD.h"

#include "Engine/Foundation/EngineServices.h"

#include "ModuleManager/LiveReload/LiveReloadManager.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SW_LOG_CALLER( "EditorModuleManager" );

    bool EditorModuleManager::registerEditorModule( LiveReloadManager* pLiveReloadManager )
    {
        if ( _bEnableEditor == SW_TRUE && _bEditorModuleActive == SW_FALSE )
        {
            // -EnableEditor 를 줬는데 매니페스트가 에디터 모듈을 껐다(프로젝트나 구성 때문이다). 에디터 없이 조용히 시작하지 않는다.
            SW_LOG_ERROR( "-EnableEditor was given, but the module manifests turn %# off for this project", config::kTargetEditorModule );
            return false;
        }
        if ( _bEnableEditor == SW_FALSE || pLiveReloadManager == nullptr )
            return true;

        BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
        {
            SW_MEMORY_SCOPE( Editor );
            holdReloadSubscription( pLiveReloadManager->setOnBeforeReload(
                config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &EditorModuleManager::onBeforeEditorReload, this ) ) );
            holdReloadSubscription( pLiveReloadManager->setOnAfterReload(
                config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &EditorModuleManager::onAfterEditorReload, this ) ) );
            holdReloadSubscription( pLiveReloadManager->setOnReloadFault(
                config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &EditorModuleManager::onEditorReloadFault, this ) ) );
            holdReloadSubscription( pLiveReloadManager->setOnValidateLibrary(
                config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateLibraryDelegate, &EditorModuleManager::isEditorImageUsable, this ) ) );
            // 편집기 코드가 다른 스레드에서 돌고 있으면(열린 파일 대화상자) 닫힐 때까지 리로드를 기다린다 — 변경은 버리지 않는다.
            holdReloadSubscription( pLiveReloadManager->addReloadHold( SW_DELEGATE_METHOD( LiveReloadManager::ReloadHoldDelegate, &EditorModuleManager::isEditorReloadHeld, this ) ) );
            if ( pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
            {
                SW_LOG_ERROR( "Failed to load the Editor Module." );
                return false;
            }

            // 확장 모듈은 EditorModule 과 자기 키트에 의존한다. 의존이 바뀌면 리로드 그래프가 확장을 다시 로드한다(키트와 SWGame 의 관계와 같다).
            // 인스턴스는 없다: 정적 등록자가 EditorModule 의 등록 목록에 오르고, 에디터 매니저가 다음 프레임에 맞춘다(EditorContext::syncExtensionRegistrations).
            for ( const EditorExtensionModule& extension : _listEditorExtension )
            {
                if ( pLiveReloadManager->registerModule( extension._name, extension._listDependency ) == false )
                {
                    // 언리얼도 에디터 모듈 하나가 실패하면 그 플러그인만 끈다. 에디터는 시작한다.
                    SW_LOG_ERROR( "Editor extension module %# could not be loaded - the editor runs without it", extension._name.c_str() );
                    continue;
                }
                SW_LOG_INFO( "Editor extension module %# loaded", extension._name.c_str() );
            }

            if ( pLiveReloadManager->isGraphBroken() )
            {
                SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
                return false;
            }
        }
        return true;
    }

    void EditorModuleManager::bindDevToolServices( SubsystemScope& editorServices ) const
    {
        // 개발 도구는 엔진의 기동 확장(`DevToolsHost`)이 세워 서비스 목록의 행이 아니다 — 에디터가 찾는 것(HUD 조작 · 설정 리로드 창)을 빌려 건다(없는 호스트면 걸지 않는다).
        (void)editorServices.bind( engine::findSubsystem<DebugHUD>() );
        (void)editorServices.bind( engine::findSubsystem<ConfigReloadSubsystem>() );
    }
} // namespace sw
