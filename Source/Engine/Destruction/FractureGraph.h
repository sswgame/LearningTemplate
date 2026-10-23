/**
 * @file FractureGraph.h
 * @brief 파쇄 결과의 뼈대입니다. 조각(잎)과 묶음(클러스터) 계층, 조각 사이의 연결(맞닿은 면 넓이)을 담습니다. 2D 와 3D 가 같은 타입을 씁니다.
 * @details 노드 0..N-1 이 잎이고, 그 뒤가 묶음이며, 마지막이 뿌리다.
 *          잎 번호는 계층의 깊이 우선 순서이므로, 노드 하나에 속한 잎은 연속 구간 `[_firstLeaf, _firstLeaf + _leafCount)` 이다.
 *          연결은 잎끼리 맺고, 맞닿은 넓이(2D 는 길이)를 가진다. 구조 지지와 끊김 세기가 이 값을 쓴다.
 *          언리얼 Chaos 의 Geometry Collection 계층(클러스터 레벨)과 연결 그래프에 해당한다.
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
    /** @brief 계층의 노드 하나입니다. 잎은 조각 하나이고, 묶음은 자식 노드들입니다. */
    struct FractureNode
    {
        float3  _centroid{};      ///< 부피 중심이다(메시 공간).
        float32 _volume{ 0.0f };  ///< 부피다(2D 는 넓이).
        int32   _parent{ -1 };    ///< 부모 노드다(뿌리는 -1).
        uint32  _firstChild{ 0 }; ///< `FractureGraph::_listChildNode` 안의 첫 자식 인덱스다.
        uint32  _childCount{ 0 }; ///< 자식 수(잎은 0)
        uint32  _firstLeaf{ 0 };  ///< 이 노드가 포함한 첫 잎이다.
        uint32  _leafCount{ 0 };  ///< 포함한 잎 수다(잎은 1).
        uint8   _depth{ 0 };      ///< 뿌리 0 부터 센 깊이다. 피해 임계값 테이블의 인덱스로 쓴다.

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
    /** @brief 노드 계층과 잎 연결입니다. 파일 머리말을 참고하세요. */
    struct SW_API FractureGraph
    {
        vector<FractureNode> _listNode;      ///< 잎이 앞쪽 `_leafCount` 개, 묶음이 그 뒤, 뿌리가 마지막이다.
        vector<uint32>       _listChildNode; ///< 노드마다 자식 번호 구간이다.
        vector<FractureLink> _listLink;      ///< 잎 연결이다(A < B, A 와 B 순으로 정렬).
        uint32               _leafCount{ 0 };

        /** @brief 뿌리 노드 번호입니다. 비었으면 0 입니다. */
        uint32 getRootNode() const { return _listNode.empty() ? 0u : static_cast<uint32>( _listNode.size() - 1 ); }
        /** @brief 노드의 자식 번호들입니다. */
        vector_reference<const uint32> getChildren( uint32 node ) const;
        /** @brief 가장 깊은 노드의 깊이 + 1 입니다. */
        uint32 getDepthCount() const;
        /** @brief 구조가 맞는지(잎 구간, 부모, 자식, 연결 범위) 검사합니다. 틀리면 @p pOutError 에 이유를 적고 false 를 반환합니다. */
        bool isValid( string* pOutError = nullptr ) const;
        /** @brief 모두 비웁니다. */
        void clear();
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 잎(조각)만 있는 그래프에 묶음 계층을 만드는 함수 모음입니다. 2D 와 3D 가 함께 씁니다.
     * @details 레벨마다 묶음 수를 위에서 아래로 받는다. `[6, 24]` 이면 뿌리, 6, 24, 잎 순이다. 계층은 **아래에서 위로** 만든다.
     *          아래 레벨의 노드들은 멀리 떨어진 점을 차례로 고르고(가장 먼 점부터라 시드 없이 결정적이다), 가까운 시드에 배정하는 과정을 몇 번 반복해(k-평균) 나눈다.
     *          한 묶음 안이 연결로 이어지지 않으면 이어진 덩어리마다 다시 나눈다. 떨어진 조각이 한 덩어리로 날지 않게 하려는 것이다.
     *          자식이 하나뿐인 묶음은 만들지 않는다.
     *          마지막에 잎을 깊이 우선 순서로 다시 매겨, 노드마다 잎 구간이 이어지게 한다(@p outListLeafOrder 는 새 잎 i 가 이전 몇 번 잎이었는지 담는다).
     */
    struct SW_API FractureGraphUtil
    {
        /**
         * @brief @p inoutGraph 의 잎(노드 = 잎, 묶음 없음)과 연결로 묶음 계층을 만들고 잎을 다시 매깁니다.
         * @param listLevelCount 레벨마다 묶음 수입니다(위에서 아래로). 비면 뿌리 하나가 모든 잎을 포함합니다.
         * @param outListLeafOrder 새 잎 번호에서 이전 잎 번호로 가는 매핑입니다. 잎 데이터(형상, 껍질)를 이 순서로 옮깁니다.
         */
        static void populateHierarchy( FractureGraph& inoutGraph, vector_reference<const uint32> listLevelCount, vector<uint32>& outListLeafOrder );
        /** @brief 연결 목록을 A < B, A 와 B 순으로 정렬하고 같은 쌍을 합칩니다(넓이를 더합니다). */
        static void normalizeLinks( vector<FractureLink>& inoutListLink );
        /** @brief 검사 실패를 알립니다. @p pOutError 가 있으면 이유를 담고, 언제나 false 를 돌려줍니다(`return reportInvalid(...)` 꼴로 씁니다). */
        static bool reportInvalid( string* pOutError, const utf8* pReason );
    };
} // namespace sw
