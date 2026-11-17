// 노드 그래프 문서 패널의 Model 쪽 편집 알림 입구입니다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/Model/EditorGraphEditSession.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

namespace sw::editor
{
    void EditorGraphEditSession::recordDocumentEdit( string_view label ) const
    {
        _pRecorder->notifyEdited( label );
    }
} // namespace sw::editor
