/**
 * @file EditorContext.cpp
 * @brief EditorContext 조회입니다(UI 매니저에 의존하지 않는 부분).
 *
 * @details 생성 · 초기화 · 종료는 EditorContextLifecycle.cpp 에 있습니다. 이유는 그 파일의 주석을 참고하십시오.
 */
#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    void EditorContext::setActive( EditorContext* pContext )
    {
        setEditorScope( pContext != nullptr ? &pContext->_scope : nullptr );
    }
} // namespace sw::editor
