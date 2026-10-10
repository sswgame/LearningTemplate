/**
 * @file EditorNodeGraphRules.h
 * @brief 노드 그래프 틀의 판단(찾아 넣기 검색 · 핀 연결 규칙 · 닿지 않는 노드)입니다. ImGui 가 없어 EditorTest 가 시험합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 찾아 넣기 목록의 노드 종류 하나입니다. */
    struct EditorGraphNodeKind
    {
        const utf8* _pName{ nullptr };     ///< 보이는 이름
        const utf8* _pCategory{ nullptr }; ///< 묶음(비면 "General")
        uint32      _kindID{ 0 };          ///< 패널이 정한 값 — 고르면 돌려준다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 핀 하나의 연결 정보입니다. 패널이 핀 번호로 알려 준다. */
    struct EditorGraphPinInfo
    {
        uint32 _type{ 0 };       ///< 핀 타입(패널이 정한 번호 — 색 표의 칸). `kWildcardPinType` 은 무엇과도 맞는다
        bool   _bInput{ false }; ///< 들어오는 핀이면 true
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 노드 하나의 문제입니다(빨간 테두리 + 툴팁). 패널이 검증한 결과를 틀에 넘긴다. */
    struct EditorGraphNodeIssue
    {
        int32  _nodeID{ 0 };
        string _message;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그래프 링크 하나를 노드 사이로 본 것입니다(닿지 않는 노드 판정용). */
    struct EditorGraphEdge
    {
        int32 _fromNode{ 0 };
        int32 _toNode{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorNodeGraphRules
     * @brief 언리얼 SGraphEditor · 유니티 GraphView 의 공통 판단입니다 — 노드 찾아 넣기 검색, 핀 연결 허락(방향 · 타입), 닿지 않는 노드.
     */
    struct SW_EDITOR_API EditorNodeGraphRules
    {
        /** @brief 무엇과도 맞는 핀 타입입니다. */
        static constexpr uint32 kWildcardPinType = 0xFFFFFFFFu;

        /**
         * @brief 검색어 @p filter 에 맞는 종류를 묶음 순(처음 나온 묶음 순)으로, 묶음 안에서는 목록 순으로 고릅니다(먼저 비운다).
         * @details 이름이나 묶음에 대소문자 무시 부분 일치면 맞습니다. 빈 검색어는 전부입니다. 결과는 @p listKind 의 자리입니다.
         */
        static void filterNodeKinds( const vector<EditorGraphNodeKind>& listKind, string_view filter, vector<uint32>& outListIndex );
        /**
         * @brief 두 핀을 이어도 되는지 묻습니다. 되면 true 이고 @p outFromPin · @p outToPin 에 나가는 쪽 → 들어오는 쪽 순서를 둡니다.
         * @param outReason 안 되면 이유(영어 한 줄 — 캔버스 툴팁에 그대로 보인다)입니다.
         */
        static bool canConnect( int32 pinA, const EditorGraphPinInfo& infoA, int32 pinB, const EditorGraphPinInfo& infoB, int32& outFromPin, int32& outToPin,
                                const utf8*& outReason );
        /**
         * @brief 들어오는 핀이 있는데 아무 링크도 닿지 않는 노드를 문제로 모읍니다(먼저 비운다). 그런 노드는 실행 중에 닿지 않는다.
         * @param listInputNode 들어오는 핀이 있는 노드 id(시작 노드처럼 들어오는 핀이 없는 노드는 넣지 않는다).
         */
        static void collectUnreachableNodes( const vector<int32>& listInputNode, const vector<EditorGraphEdge>& listEdge, vector<EditorGraphNodeIssue>& outListIssue );
    };
} // namespace sw::editor
