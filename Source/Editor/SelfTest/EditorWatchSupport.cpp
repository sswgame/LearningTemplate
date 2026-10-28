/**
 * @file EditorWatchSupport.cpp
 * @brief 에디터 감시 값이 함께 쓰는 시나리오용 전역 변수의 정의와 읽기 보조 함수입니다(선언은 `EditorWatchSupport.h`).
 */
#include "pch.h"

#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    /** @brief 프로브 `Editor.VisualizerOn` 이 볼 시각화 id 입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchVisualizer, "", "감시 값 Editor.VisualizerOn 이 볼 뷰포트 시각화 id (시나리오용)" );
    /** @brief 프로브 `Editor.PanelOpen` 이 볼 패널 id 입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchPanel, "", "감시 값 Editor.PanelOpen 이 볼 패널 id (시나리오용)" );
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchObject, "", "감시 값 Editor.HierarchyRowIcon · Editor.BillboardIcon 이 볼 오브젝트 이름 (시나리오용)" );
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchOverlay, "view", "감시 값 Editor.Overlay* 가 볼 씬 뷰 오버레이 바 id (view · transform · display · tools, 시나리오용)" );
    /** @brief 프로브 `Editor.SelectedProperty` 가 볼 `<컴포넌트 타입>.<프로퍼티>` 입니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchProperty, "", "감시 값 Editor.SelectedProperty 가 볼 <컴포넌트 타입>.<프로퍼티> (시나리오용)" );

    EditorContext* getWatchContext( const utf8* pWatch )
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 필수 서비스: 없으면 버그
        if ( pContext == nullptr )
            SW_LOG_ERROR( "[Watch] EditorContext is missing - %# reads nothing", pWatch );
        return pContext;
    }

    const GameObject* findWatchedObject()
    {
        const GameObjectManager* pManager = editor::getActiveObjectManager();
        return pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorWatchObject.c_str() ) ) : nullptr;
    }
} // namespace sw::editor
