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
    class EditorGraphEditSession;
    /**
     * @struct DialogueGraphEdit
     * @brief 대화 그래프 명령 묶음입니다.
     */
    struct DialogueGraphEdit
    {
        // ------------------------------------------------------------------------------
        // 편집 명령 — 고친 뒤 이 명령이 정한 되돌리기 이름으로 @p session 에 알린다(감사 E-16 — 창은 알림 · 이름을 모른다)
        // ------------------------------------------------------------------------------
        /** @brief 창이 노드를 하나 더했습니다 — "Add Dialogue Node". */
        static void commitNodeAdded( const EditorGraphEditSession& session );
        /** @brief 그래프를 기본으로 되돌렸습니다 — "Reset Dialogue Graph". */
        static void commitGraphReset( const EditorGraphEditSession& session );
        /** @brief 핀 @p fromPin → @p toPin 링크(번호 @p linkID)를 더하고 "Link Dialogue Nodes" 로 알립니다. */
        static void linkPins( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, int32 linkID, int32 fromPin, int32 toPin );
        /** @brief 화자 @p speakerID 의 이름을 @p name 으로 정하고 "Rename Dialogue Speaker" 로 알립니다. 없는 id 면 고치지 않고 false 입니다. */
        static bool renameSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, uint32 speakerID, string_view name );
        /** @brief 화자를 더하고 "Add Dialogue Speaker" 로 알립니다. 새 화자 id 입니다. */
        static uint32 addSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, string_view name );
        /** @brief 화자를 지우고(노드의 화자도 비운다) "Remove Dialogue Speaker" 로 알립니다. */
        static void removeSpeaker( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, uint32 speakerID );
        /** @brief 노드 대사를 @p text 로 정하고 "Edit Dialogue Node" 로 알립니다. 칸을 고치는 프레임마다 불러도 된다 — 문서 창의 상호작용 구간이 놓을 때 한 단계로 묶는다. */
        static void setNodeText( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view text );
        /** @brief 노드 조건식을 @p condition 으로 정하고 "Edit Dialogue Node" 로 알립니다. */
        static void setNodeCondition( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view condition );
        /** @brief 선택지 @p choiceIndex 의 글을 @p text 로 정하고 "Edit Dialogue Node" 로 알립니다. 범위 밖이면 고치지 않고 false 입니다. */
        static bool setChoiceText( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, size_t choiceIndex, string_view text );
        /** @brief 노드의 화자를 @p speakerID 로 정하고 "Edit Dialogue Node" 로 알립니다. */
        static void setNodeSpeaker( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, uint32 speakerID );
        /** @brief 노드의 동작 명령을 @p command 로 정하고 "Edit Dialogue Node" 로 알립니다. */
        static void setNodeActionCommand( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view command );
        /** @brief 선택지 @p choiceIndex 를 지우고(그 핀의 링크도) "Edit Dialogue Node" 로 알립니다. */
        static void removeChoice( DialogueGraphAsset& inoutGraph, const EditorGraphEditSession& session, DialogueAssetNode& inoutNode, size_t choiceIndex );
        /** @brief 선택지 @p text 를 끝에 더하고 "Edit Dialogue Node" 로 알립니다. */
        static void addChoice( DialogueAssetNode& inoutNode, const EditorGraphEditSession& session, string_view text );

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
