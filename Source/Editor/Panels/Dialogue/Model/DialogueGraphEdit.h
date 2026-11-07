/**
 * @file DialogueGraphEdit.h
 * @brief 대화 그래프를 고치는 명령입니다. ImGui 없이 그래프 값만 다룹니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw::editor
{
    /**
     * @struct DialogueGraphEdit
     * @brief 대화 그래프 명령 묶음입니다.
     */
    struct DialogueGraphEdit
    {
        /** @brief Resource 상대 경로 @p path 의 대화 그래프 문서를 읽습니다(`EditorToolAssetCommands::loadDialogueGraph`). 창(View)이 커맨드 헤더를 보지 않게 여기를 지난다. */
        static ToolAssetLoadResult loadGraph( DialogueGraphAsset& outGraph, string_view path );
        /** @brief 대화 그래프 문서를 @p path 에 씁니다. 실패하면 커맨드가 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool saveGraph( const DialogueGraphAsset& graph, string_view path );
        /** @brief 그래프를 새 문서의 예시(시작 · 대사 · 선택 · 동작 · 끝)로 바꿉니다. */
        static void makeDefaultGraph( DialogueGraphAsset& outGraph );
        /** @brief @p nodeID 노드를 더합니다. 위치는 번호로 어긋나게 놓아 겹치지 않게 합니다. 화자를 쓰는 종류면 목록의 첫 화자를 넣습니다(목록이 비면 화자 없음). */
        static void addNode( DialogueGraphAsset& inoutGraph, DialogueAssetNodeKind type, int32 nodeID );
        /** @brief 새 id(`makeNextSpeakerID`)로 화자를 더하고 그 id 를 돌려줍니다. */
        static uint32 addSpeaker( DialogueGraphAsset& inoutGraph, string_view name );
        /** @brief 화자를 지웁니다. 그 화자를 쓰던 노드는 화자 없음이 됩니다(없는 id 를 가리킨 채 남으면 읽기 오류다). */
        static void removeSpeaker( DialogueGraphAsset& inoutGraph, uint32 speakerID );
        /** @brief 선택지를 지웁니다. 그 선택지에서 나가던 링크도 지우고, 뒤 선택지의 링크는 한 칸 당겨 같은 선택지를 가리키게 합니다. */
        static void removeChoice( DialogueGraphAsset& inoutGraph, DialogueAssetNode& inoutNode, size_t choiceIndex );
    };
} // namespace sw::editor
