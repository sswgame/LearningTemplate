/**
 * @file UnionFindUtil.h
 * @brief 부모 번호 배열로 표현한 유니온 파인드(경로 압축)입니다. 같은 형태의 사본이 다시 생기지 않도록 한 곳에 둡니다.
 *
 * @details 배열은 `listParent[i] = i`(스스로가 루트)로 채워서 시작합니다. 합칠 때 작은 번호를 루트로 삼으므로
 *          결과가 합치는 순서에 좌우되지 않습니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct UnionFindUtil
     * @brief 부모 번호 배열(`vector<uint32>`)에 쓰는 유니온 파인드 도우미입니다.
     */
    struct UnionFindUtil
    {
        /** @brief `item` 의 루트를 찾고, 가는 경로의 노드를 루트에 바로 연결합니다(경로 압축). */
        template <typename TVector>
        static uint32 findRoot( TVector& inoutListParent, uint32 item )
        {
            uint32 root = item;
            while ( inoutListParent[root] != root )
            {
                root = inoutListParent[root];
            }
            while ( inoutListParent[item] != root )
            {
                const uint32 next     = inoutListParent[item];
                inoutListParent[item] = root;
                item                  = next;
            }
            return root;
        }

        /** @brief 두 노드의 집합을 합칩니다. 작은 번호의 루트가 합친 집합의 루트가 됩니다. */
        template <typename TVector>
        static void unite( TVector& inoutListParent, uint32 lhs, uint32 rhs )
        {
            const uint32 rootLhs = findRoot( inoutListParent, lhs );
            const uint32 rootRhs = findRoot( inoutListParent, rhs );
            if ( rootLhs == rootRhs )
                return;
            if ( rootLhs < rootRhs )
                inoutListParent[rootRhs] = rootLhs;
            else
                inoutListParent[rootLhs] = rootRhs;
        }
    };
} // namespace sw
