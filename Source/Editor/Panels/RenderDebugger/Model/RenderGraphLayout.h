/**
 * @file RenderGraphLayout.h
 * @brief 렌더 그래프 사본(`RenderGraphSnapshot`)을 노드 그래프 배치로 옮깁니다 — 레벨 = 열, 같은 레벨의 패스 = 행, 쓴 패스 → 읽는 패스 = 링크(언리얼 RDG Insights 의 패스 그림).
 * @details ImGui 를 모른다(MVVM — 창은 이 값으로 `EditorNodeGraph` 에 노드 · 핀 · 링크를 그린다). 노드 id · 핀 id 는 이 배치가 정해 링크와 어긋나지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Renderer/Graph/RenderGraphSnapshot.h"

namespace sw::editor
{
    /** @brief 패스 노드 하나입니다. */
    struct RenderGraphLayoutNode
    {
        float2 _position{};  ///< 캔버스 좌표(왼쪽 위)
        uint32 _pass{ 0 };   ///< `RenderGraphSnapshot::_listPass` 번호
        uint32 _column{ 0 }; ///< 레벨
        uint32 _row{ 0 };    ///< 레벨 안 차례
    };

    /** @brief 링크 하나입니다 — 쓴 패스에서 그 자원을 다음에 읽는 패스로. 같은 두 패스 사이는 한 번만 잇는다(첫 자원을 이름으로 단다). */
    struct RenderGraphLayoutLink
    {
        uint32 _fromPass{ 0 };
        uint32 _toPass{ 0 };
        uint32 _resource{ 0 }; ///< `RenderGraphSnapshot::_listResource` 번호
    };

    /**
     * @struct RenderGraphLayout
     * @brief 파일 머리말 참고.
     */
    struct RenderGraphLayout
    {
        static constexpr float32 kColumnSpacing = 280.0f; ///< 열 사이(캔버스 px) — 노드 폭(패스 이름 · 전이 · GPU 시간 줄)보다 넓게
        static constexpr float32 kRowSpacing    = 110.0f; ///< 행 사이
        static constexpr int32   kNodeIDBase    = 100000; ///< 노드 id 의 시작 — 링크 id(1 부터)와 겹치지 않게
        static constexpr int32   kIDsPerNode    = 4;      ///< 노드 하나가 쓰는 id 수(노드 · 입력 핀 · 출력 핀)

        vector<RenderGraphLayoutNode> _listNode; ///< 실행 순서(패스 번호 순)
        vector<RenderGraphLayoutLink> _listLink;
        uint32                        _columnCount{ 0 };

        /** @brief @p snapshot 의 배치를 만듭니다. */
        static RenderGraphLayout make( const RenderGraphSnapshot& snapshot );

        /** @brief 패스 @p pass 노드의 id 입니다. */
        static int32 getNodeID( uint32 pass ) { return kNodeIDBase + static_cast<int32>( pass ) * kIDsPerNode; }
        /** @brief 패스 @p pass 의 입력 핀 id 입니다. */
        static int32 getInputPinID( uint32 pass ) { return getNodeID( pass ) + 1; }
        /** @brief 패스 @p pass 의 출력 핀 id 입니다. */
        static int32 getOutputPinID( uint32 pass ) { return getNodeID( pass ) + 2; }
        /** @brief 링크 @p link 번호의 id 입니다(1 부터). */
        static int32 getLinkID( uint32 link ) { return static_cast<int32>( link ) + 1; }
    };
} // namespace sw::editor
