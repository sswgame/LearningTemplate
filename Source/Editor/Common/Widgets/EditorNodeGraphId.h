/**
 * @file EditorNodeGraphId.h
 * @brief 노드 그래프 패널이 쓰는 정수 id ↔ imgui-node-editor id 변환
 *
 * @details 그래프 패널은 노드·핀·링크를 자기 자료구조에서 int32 로 들고, 캔버스에 넘길 때만
 *          ed::NodeId 류로 감싼다. 그 변환이 패널마다 한 벌씩 복사돼 있었다(다이얼로그·애니메이션).
 *          그래프 패널이 하나 더 생기면 또 한 벌이 늘어날 자리라 여기 모은다.
 *
 * @note EditorNodeGraph.h 는 imgui-node-editor 헤더를 포함하지 않는다(전방 선언만 쓴다).
 *       그 성질을 깨지 않으려고 변환만 이 헤더로 갈라 둔다 — 캔버스를 직접 그리는 쪽만 포함한다.
 */
#pragma once
#include "Core/Common/Types.h"

#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    /** @brief 노드 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    inline ax::NodeEditor::NodeId toNodeId( int32 id )
    {
        return ax::NodeEditor::NodeId( static_cast<uintptr_t>( id ) );
    }

    /** @brief 핀 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    inline ax::NodeEditor::PinId toPinId( int32 id )
    {
        return ax::NodeEditor::PinId( static_cast<uintptr_t>( id ) );
    }

    /** @brief 링크 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    inline ax::NodeEditor::LinkId toLinkId( int32 id )
    {
        return ax::NodeEditor::LinkId( static_cast<uintptr_t>( id ) );
    }
} // namespace sw::editor
