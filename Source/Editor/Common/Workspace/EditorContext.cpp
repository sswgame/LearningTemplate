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
    EditorContext* EditorContext::get()
    {
        // 컨텍스트는 자기 범위에 자신을 건다(생성자) — 활성 컨텍스트는 지금 에디터 범위의 주인이다. 테스트가 그 위에 건 범위는 상위로 올라가 찾는다.
        return locateSubsystem<EditorContext>( findEditorScope() );
    }

    void EditorContext::setActive( EditorContext* pContext )
    {
        setEditorScope( pContext != nullptr ? &pContext->_scope : nullptr );
    }
} // namespace sw::editor
