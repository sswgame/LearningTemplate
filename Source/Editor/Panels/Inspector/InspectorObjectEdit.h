/**
 * @file InspectorObjectEdit.h
 * @brief 오브젝트 하나를 바꾸는 인스펙터 편집의 공용 진입점입니다. 인스펙터 패널과 프로퍼티 그리드가 같은 규칙을 쓰도록 한 곳에 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

namespace sw::editor
{
    /**
     * @struct InspectorObjectEdit
     * @brief 인스펙터에서 오브젝트 하나를 바꾸는 편집을 되돌리기 규칙에 맞춰 적용하는 도우미입니다.
     */
    struct InspectorObjectEdit
    {
        /**
         * @brief 오브젝트 하나를 바꾸는 인스펙터 편집입니다. 정지 상태이면 되돌리기 기록과 씬 dirty 를 남기고, 플레이 중이면 바로 바꿉니다.
         * @details 이름, 활성, 부모 해제, 컴포넌트 활성, 컴포넌트 제거가 이 함수를 지난다. 기록도 dirty 도 없이 바꾸면 그 편집은
         *          다른 씬을 열거나 에디터를 끌 때 묻지도 않고 사라지고 Ctrl+Z 로도 돌릴 수 없다. 플레이 중에는 씬 명령이 막히므로
         *          (플레이 사본은 Stop 이 되돌린다) 바로 바꾼다.
         */
        template <typename EditFunc>
        static void applyObjectEdit( GameObject* pObj, string_view undoLabel, EditFunc&& edit )
        {
            if ( pObj == nullptr )
                return;
            if ( EditorPlaySession::areSceneEditsAllowed() == false )
            {
                edit();
                return;
            }
            const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
            edit();
            EditorSceneCommands::commitModify( pObj, beforeSnapshot, undoLabel );
        }
    };
} // namespace sw::editor
