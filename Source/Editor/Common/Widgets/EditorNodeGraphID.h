/**
 * @file EditorNodeGraphID.h
 * @brief 노드 그래프 패널이 쓰는 정수 id ↔ imgui-node-editor id 변환입니다.
 *
 * @details 그래프 패널은 노드 · 핀 · 링크를 자기 자료 구조에서 int32 로 들고, 캔버스에 넘길 때만 ed::NodeId 류로
 *          감쌉니다. 그 변환을 패널마다 복사하지 않도록 여기에 모읍니다(대화 · 애니메이션 · 오디오 · 행동 트리 · 기믹).
 *
 * @note 이 헤더는 imgui-node-editor 헤더를 포함하지 않습니다. id 형식은 전방 선언만 하고 본문은 EditorNodeGraphID.cpp(EditorModule)에
 *       둡니다. 부르는 .cpp 가 imgui-node-editor 헤더를 직접 include 해 완전한 형식을 봅니다. id 는 상태 없는 값 감싸기라
 *       확장 모듈의 imgui-node-editor 사본과 섞여도 상관없습니다(사본 문제는 `EditorGraphDocumentPanel.h`).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace ax::NodeEditor
{
    struct NodeId;
    struct PinId;
    struct LinkId;
} // namespace ax::NodeEditor

namespace sw::editor
{
    /** @brief 노드 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    SW_EDITOR_API ax::NodeEditor::NodeId toNodeID( int32 id );

    /** @brief 핀 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    SW_EDITOR_API ax::NodeEditor::PinId toPinID( int32 id );

    /** @brief 링크 id 를 캔버스가 쓰는 형식으로 감쌉니다. */
    SW_EDITOR_API ax::NodeEditor::LinkId toLinkID( int32 id );
} // namespace sw::editor
