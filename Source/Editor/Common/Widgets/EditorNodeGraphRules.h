/**
 * @file EditorNodeGraphRules.h
 * @brief 노드 그래프 프레임워크의 판단 로직입니다. 노드 검색, 핀 연결 규칙, 도달하지 못하는 노드를 다룹니다. ImGui 에 의존하지 않으므로 EditorTest 가 테스트합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 노드 검색 목록의 노드 종류 하나입니다. */
    struct EditorGraphNodeKind
    {
        const utf8* _pName{ nullptr };     ///< 보이는 이름
        const utf8* _pCategory{ nullptr }; ///< 그룹이다. 비어 있으면 "General" 이다
        uint32      _kindID{ 0 };          ///< 패널이 정한 값이다. 선택하면 그대로 반환한다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 핀 하나의 연결 정보입니다. 패널이 핀 번호로 알려 줍니다. */
    struct EditorGraphPinInfo
    {
        uint32 _type{ 0 };       ///< 핀 타입이다. 패널이 정한 번호이며 색 테이블의 인덱스다. `kWildcardPinType` 은 무엇과도 맞는다
        bool   _bInput{ false }; ///< 들어오는 핀이면 true NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 노드 하나의 문제입니다. 빨간 테두리와 툴팁으로 보여 줍니다. 패널이 검증한 결과를 프레임워크에 넘깁니다. */
    struct EditorGraphNodeIssue
    {
        int32  _nodeID{ 0 };
        string _message;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그래프 링크 하나를 노드 사이의 연결로 본 것입니다. 도달하지 못하는 노드를 판정하는 데 씁니다. */
    struct EditorGraphEdge
    {
        int32 _fromNode{ 0 };
        int32 _toNode{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 고른 노드를 맞추는 방식입니다(언리얼 그래프의 Alignment 메뉴와 같은 여섯). */
    enum class EditorGraphAlign : uint8
    {
        Left = 0,             ///< 왼쪽 끝을 가장 왼쪽 노드에 맞춘다
        Top,                  ///< 위쪽 끝을 가장 위 노드에 맞춘다
        CenterHorizontal,     ///< 가로 가운데를 고른 노드들 가운데에 맞춘다(세로 한 줄)
        CenterVertical,       ///< 세로 가운데를 고른 노드들 가운데에 맞춘다(가로 한 줄)
        DistributeHorizontal, ///< 양 끝 노드는 두고 사이 간격을 고르게(가로)
        DistributeVertical,   ///< 양 끝 노드는 두고 사이 간격을 고르게(세로)
        Count,
    };

    /** @brief 캔버스 좌표의 노드 사각형 하나입니다(정렬 · 미니맵 입력). */
    struct EditorGraphNodeRect
    {
        int32  _nodeID{ 0 };
        float2 _position{}; ///< 왼쪽 위
        float2 _size{};
    };

    /** @brief 캔버스 점을 미니맵 점으로 옮기는 배율과 원점입니다(`미니맵 = ( 캔버스 - _canvasOrigin ) * _scale + _mapOffset`). */
    struct EditorGraphMinimapTransform
    {
        float2  _canvasOrigin{};
        float2  _mapOffset{};
        float32 _scale{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorNodeGraphRules
     * @brief 언리얼 SGraphEditor 와 유니티 GraphView 의 공통 판단입니다. 노드 검색, 핀 연결 허용 여부(방향과 타입), 도달하지 못하는 노드를 다룹니다.
     */
    struct SW_EDITOR_API EditorNodeGraphRules
    {
        /** @brief 무엇과도 맞는 핀 타입입니다. */
        static constexpr uint32 kWildcardPinType = 0xFFFFFFFFu;

        /**
         * @brief 검색어 @p filter 에 맞는 종류를 그룹 순서(처음 나온 그룹 순서)로, 그룹 안에서는 목록 순서로 고릅니다. 먼저 출력 목록을 비웁니다.
         * @details 이름이나 그룹이 대소문자를 무시하고 부분 일치하면 맞는다. 빈 검색어는 전부 맞는다. 결과는 @p listKind 에 담는다.
         */
        static void filterNodeKinds( const vector<EditorGraphNodeKind>& listKind, string_view filter, vector<uint32>& outListIndex );
        /**
         * @brief 두 핀을 연결해도 되는지 확인합니다. 되면 true 이고 @p outFromPin 과 @p outToPin 에 나가는 쪽과 들어오는 쪽을 순서대로 담습니다.
         * @param outReason 안 되면 그 이유입니다. 영어 한 줄이며 캔버스 툴팁에 그대로 보입니다.
         */
        static bool canConnect( int32 pinA, const EditorGraphPinInfo& infoA, int32 pinB, const EditorGraphPinInfo& infoB, int32& outFromPin, int32& outToPin,
                                const utf8*& outReason );
        /**
         * @brief 들어오는 핀이 있는데 아무 링크도 닿지 않는 노드를 문제로 모읍니다. 먼저 출력 목록을 비웁니다. 그런 노드는 실행 중에 도달하지 못합니다.
         * @param listInputNode 들어오는 핀이 있는 노드 id 입니다. 시작 노드처럼 들어오는 핀이 없는 노드는 넣지 않습니다.
         */
        static void collectUnreachableNodes( const vector<int32>& listInputNode, const vector<EditorGraphEdge>& listEdge, vector<EditorGraphNodeIssue>& outListIssue );
        /**
         * @brief @p ioListRect 의 위치를 @p align 대로 바꿉니다. 노드가 둘 미만이면(나누기는 셋 미만이면) 그대로 두고 false 입니다.
         * @details 나누기는 양 끝 노드를 두고 노드 사이 빈칸을 같게 한다(크기가 다른 노드도 간격이 같다). 결과의 순서는 입력 순서다.
         */
        static bool alignNodes( vector<EditorGraphNodeRect>& ioListRect, EditorGraphAlign align );
        /**
         * @brief 노드 사각형들과 보이는 사각형(@p viewMin ~ @p viewMax)을 모두 담는 범위를 @p mapSize 안에 비율을 지켜 넣는 변환을 만듭니다.
         * @return 담을 것이 없거나 미니맵 크기가 0 이면 false 입니다.
         */
        static bool fitMinimap( const vector<EditorGraphNodeRect>& listRect, const float2& viewMin, const float2& viewMax, const float2& mapSize,
                                EditorGraphMinimapTransform& outTransform );
        /** @brief 캔버스 점 @p canvasPoint 를 미니맵 점으로 옮깁니다. */
        static float2 canvasToMinimap( const EditorGraphMinimapTransform& transform, const float2& canvasPoint );
        /** @brief 미니맵 점 @p mapPoint 를 캔버스 점으로 되돌립니다. */
        static float2 minimapToCanvas( const EditorGraphMinimapTransform& transform, const float2& mapPoint );
        /** @brief 캔버스 점 @p canvasPoint 에 가운데가 가장 가까운 노드의 id 입니다. 노드가 없으면 비어 있습니다. */
        static optional<int32> findNearestNode( const vector<EditorGraphNodeRect>& listRect, const float2& canvasPoint );
    };
} // namespace sw::editor
