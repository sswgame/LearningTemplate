/**
 * @file DialogueGraphEdit.h
 * @brief 대화 그래프를 고치는 명령입니다. ImGui 없이 그래프 값만 다룹니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw::editor
{
    /**
     * @struct DialogueGraphEdit
     * @brief 대화 그래프 명령 묶음입니다.
     */
    struct DialogueGraphEdit
    {
        /** @brief 그래프를 새 문서의 예시(시작 · 대사 · 선택 · 동작 · 끝)로 바꿉니다. */
        static void makeDefaultGraph( DialogueGraphAsset& outGraph );
        /** @brief @p nodeID 노드를 더합니다. 위치는 번호로 어긋나게 놓아 겹치지 않게 합니다. */
        static void addNode( DialogueGraphAsset& inoutGraph, DialogueAssetNodeType type, int32 nodeID );
        /** @brief 선택지를 지웁니다. 그 선택지에서 나가던 링크도 지우고, 뒤 선택지의 링크는 한 칸 당겨 같은 선택지를 가리키게 합니다. */
        static void removeChoice( DialogueGraphAsset& inoutGraph, DialogueAssetNode& inoutNode, size_t choiceIndex );
    };
} // namespace sw::editor
