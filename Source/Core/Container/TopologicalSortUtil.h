/**
 * @file TopologicalSortUtil.h
 * @brief 의존 그래프의 위상 정렬(Kahn)과 순환 찾기입니다. 엔진 기동 단계(`EngineInitSequence`)와 모듈 적재 순서(`ModuleCatalog`)가 같은 규칙을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct TopologicalSortUtil
     * @brief 노드 번호 · 이름 · 의존 목록으로 순서를 정합니다.
     */
    struct SW_API TopologicalSortUtil
    {
        /**
         * @brief 남은 의존이 0 인 노드 가운데 **이름이 가장 앞인** 것을 먼저 고르는 Kahn 정렬입니다. 결과는 의존과 이름만으로 정해집니다(입력 순서와 무관).
         * @param listName 노드 이름(동점일 때의 정렬 키)
         * @param listDependency 노드마다 먼저 와야 하는 노드 번호
         * @param outListOrder 위상 순서(성공했을 때)
         * @param outListUnsorted 정렬하지 못한 노드 — 순환 안이나 순환에 매달린 노드입니다(실패했을 때)
         * @return 모든 노드를 정렬했으면 true 입니다.
         */
        static bool sortByDependency( const vector<string_view>& listName, const vector<vector<uint32>>& listDependency, vector<uint32>& outListOrder,
                                      vector<uint32>& outListUnsorted );

        /**
         * @brief @p listCandidate 안에서 순환 하나를 찾아 경로(`A, B, …, A`)로 돌려줍니다. 오류 메시지가 "무엇이 무엇을 기다리는가" 를 말하게 합니다.
         * @return 순환을 찾았으면 true 입니다.
         */
        static bool findCycle( const vector<vector<uint32>>& listDependency, const vector<uint32>& listCandidate, vector<uint32>& outListCycle );
    };
} // namespace sw
