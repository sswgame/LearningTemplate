/**
 * @file EditorProbeSupport.cpp
 * @brief 에디터 탐침이 함께 쓰는 시나리오용 전역 변수의 정의와 읽기 보조 함수입니다(선언은 `EditorProbeSupport.h`).
 */
#include "pch.h"

#include "Editor/SelfTest/EditorProbeSupport.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    /** @brief 프로브 `Editor.VisualizerOn` 이 볼 시각화 id 입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeVisualizer, "", "탐침 Editor.VisualizerOn 이 볼 뷰포트 시각화 id (시나리오용)" );
    /** @brief 프로브 `Editor.PanelOpen` 이 볼 패널 id 입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbePanel, "", "탐침 Editor.PanelOpen 이 볼 패널 id (시나리오용)" );
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeObject, "", "탐침 Editor.HierarchyRowIcon · Editor.BillboardIcon 이 볼 오브젝트 이름 (시나리오용)" );
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeOverlay, "view", "탐침 Editor.Overlay* 가 볼 씬 뷰 오버레이 바 id (view · transform · display · tools, 시나리오용)" );
    /** @brief 프로브 `Editor.SelectedProperty` 가 볼 `<컴포넌트 타입>.<프로퍼티>` 입니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeProperty, "", "탐침 Editor.SelectedProperty 가 볼 <컴포넌트 타입>.<프로퍼티> (시나리오용)" );

    EditorContext* getProbeContext( const utf8* pProbe )
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 필수 서비스: 없으면 버그
        if ( pContext == nullptr )
            SW_LOG_ERROR( "[Probe] EditorContext is missing - %# reads nothing", pProbe );
        return pContext;
    }

    const GameObject* findProbedObject()
    {
        const GameObjectManager* pManager = editor::getActiveObjectManager();
        return pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorProbeObject.c_str() ) ) : nullptr;
    }
} // namespace sw::editor
