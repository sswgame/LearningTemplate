/**
 * @file FractureGraph.h
 * @brief 파쇄 결과의 뼈대 — 조각(잎) · 묶음(클러스터) 계층과 조각 사이 연결(맞닿은 면 넓이)입니다. 2D · 3D 가 같은 타입을 씁니다.
 * @details 잎은 노드 0..N-1 이고 묶음이 그 뒤, 뿌리가 마지막입니다. 잎 번호는 계층의 깊이 우선 순서라 노드 하나의 잎들은 이어진 구간
 *          `[_firstLeaf, _firstLeaf + _leafCount)` 입니다. 연결은 잎끼리이고 넓이(2D 는 길이)를 듭니다 — 구조 지지 · 끊김 세기가 이것을 씁니다.
 *          언리얼 Chaos 의 Geometry Collection 계층(클러스터 레벨) + 연결 그래프와 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 계층의 노드 하나(잎 = 조각 하나, 묶음 = 자식 노드들)입니다. */
    struct FractureNode
    {
        float3  _centroid{};      ///< 부피 중심(메시 공간)
        float32 _volume{ 0.0f };  ///< 부피(2D 는 넓이)
        int32   _parent{ -1 };    ///< 부모 노드(뿌리는 -1)
        uint32  _firstChild{ 0 }; ///< `FractureGraph::_listChildNode` 안의 첫 자식 자리
        uint32  _childCount{ 0 }; ///< 자식 수(잎은 0)
        uint32  _firstLeaf{ 0 };  ///< 이 노드가 품은 첫 잎
        uint32  _leafCount{ 0 };  ///< 품은 잎 수(잎은 1)
        uint8   _depth{ 0 };      ///< 뿌리 0 부터의 깊이 — 피해 문턱 표의 번호

        /** @brief 잎이면 true 입니다. */
        bool isLeaf() const { return _childCount == 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 맞닿은 두 잎입니다. 작은 번호가 A 입니다. */
    struct FractureLink
    {
        uint32  _leafA{ 0 };
        uint32  _leafB{ 0 };
        float32 _area{ 0.0f }; ///< 맞닿은 면 넓이(2D 는 맞닿은 변 길이)
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 계층 + 잎 연결입니다. 파일 머리말 참고. */
    struct SW_API FractureGraph
    {
        vector<FractureNode> _listNode;      ///< 잎이 앞 `_leafCount` 개, 묶음이 뒤, 뿌리가 마지막
        vector<uint32>       _listChildNode; ///< 노드마다 자식 번호 구간
        vector<FractureLink> _listLink;      ///< 잎 연결(A < B, A · B 순 정렬)
        uint32               _leafCount{ 0 };

        /** @brief 뿌리 노드 번호입니다. 비었으면 0 입니다. */
        uint32 getRootNode() const { return _listNode.empty() ? 0u : static_cast<uint32>( _listNode.size() - 1 ); }
        /** @brief 노드의 자식 번호들입니다. */
        vector_reference<const uint32> getChildren( uint32 node ) const;
        /** @brief 가장 깊은 노드의 깊이 + 1 입니다. */
        uint32 getDepthCount() const;
        /** @brief 구조가 맞는지(잎 구간 · 부모 · 자식 · 연결 범위) 봅니다. 틀리면 @p pOutError 에 까닭을 적고 false 입니다. */
        bool isValid( string* pOutError = nullptr ) const;
        /** @brief 다 비웁니다. */
        void clear();
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 잎(조각)만 있는 그래프에 묶음 계층을 짓는 함수 모음입니다(2D · 3D 공용).
     * @details 레벨마다 묶음 수(위에서 아래로, `[6, 24]` 이면 뿌리 → 6 → 24 → 잎)를 받아 **아래에서 위로** 짓습니다: 아래 레벨의 노드들을
     *          멀리 떨어진 점 고르기(가장 먼 점부터 차례로 — 씨앗 없이 결정적) + 가까운 씨앗 배정 몇 번(k-평균)으로 나누고, 한 묶음 안이 연결로
     *          이어지지 않으면 이어진 덩어리마다 다시 나눕니다(떨어진 조각이 한 덩어리로 날지 않게). 자식이 하나뿐인 묶음은 만들지 않습니다.
     *          끝에 잎을 깊이 우선 순서로 다시 매겨 노드마다 잎 구간이 이어지게 합니다(@p outListLeafOrder — 새 잎 i 는 옛 잎 몇 번이었나).
     */
    struct SW_API FractureGraphUtil
    {
        /**
         * @brief @p inoutGraph 의 잎(노드 = 잎, 묶음 없음)과 연결로 묶음 계층을 짓고 잎을 다시 매깁니다.
         * @param listLevelCount 레벨마다 묶음 수(위 → 아래). 비면 뿌리 하나가 모든 잎을 듭니다.
         * @param outListLeafOrder 새 잎 번호 → 옛 잎 번호. 잎 데이터(형상 · 껍질)를 이 순서로 옮깁니다.
         */
        static void populateHierarchy( FractureGraph& inoutGraph, vector_reference<const uint32> listLevelCount, vector<uint32>& outListLeafOrder );
        /** @brief 연결 목록을 A < B · A · B 순으로 정렬하고 같은 쌍을 합칩니다(넓이 더함). */
        static void normalizeLinks( vector<FractureLink>& inoutListLink );
    };
} // namespace sw
