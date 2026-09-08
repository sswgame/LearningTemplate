/**
 * @file EditorContext.cpp
 * @brief EditorContext 조회 — UI 매니저에 의존하지 않는 부분
 *
 * @details 생성·초기화·종료는 EditorContextLifecycle.cpp 에 있다. 이유는 그 파일 주석 참고.
 */
#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    EditorContext* EditorContext::s_pActiveContext = nullptr;

    EditorContext* EditorContext::get()
    {
        EditorContext* pLocalContext = getService<EditorContext>();
        return pLocalContext != nullptr ? pLocalContext : s_pActiveContext;
    }
} // namespace sw::editor
