/**
 * @file ProfilerCallTree.h
 * @brief 프로파일러 패널 Call Tree 탭의 집계입니다 — 타임라인 녹화의 사건을 호출 경로(스레드 → 바깥 구간 → 안 구간)로 접습니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Profiling/ProfilerTimeline.h"

namespace sw::editor
{
    /** @brief 호출 트리의 노드 하나입니다. 같은 부모 아래 같은 구간(슬롯)은 한 노드로 접는다. */
    struct ProfilerCallNode
    {
        uint64 _totalNanos{ 0 };  ///< 이 경로로 불린 시간의 합
        uint64 _selfNanos{ 0 };   ///< 합에서 자식 구간을 뺀 시간
        uint32 _slot{ 0 };        ///< `FrameProfiler` 슬롯 — 이름은 `FrameProfiler::findScopeName`
        uint32 _parent{ 0 };      ///< 부모 노드 번호(뿌리는 `kNoParent`)
        uint32 _threadIndex{ 0 }; ///< 녹화의 스레드 번호
        uint32 _callCount{ 0 };   ///< 불린 횟수
        uint32 _firstChild{ 0 };  ///< 첫 자식 노드 번호(없으면 `kNoParent`)
        uint32 _nextSibling{ 0 }; ///< 다음 형제 노드 번호(없으면 `kNoParent`)
        uint16 _depth{ 0 };       ///< 겹(스레드의 바깥 구간이 0)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ProfilerCallTree
     * @brief 녹화한 사건을 호출 트리로 접습니다(유니티 Profiler Hierarchy · 언리얼 Insights Callers/Callees).
     * @details 사건은 스레드마다 시작 시각 순이고 겹(`_depth`)을 지닌다. 겹 d 의 사건은 바로 앞의 겹 d - 1 사건 안에 있다 — 쌓은 경로의 길이를 겹에
     *          맞춰 줄이고 그 끝 노드 아래에 접는다. 자식은 처음 나온 순서로 잇는다. 형제 정렬(시간 큰 순)은 `sortChildrenByTotal` 이 한다.
     */
    struct ProfilerCallTree
    {
        /** @brief 부모 · 자식 · 형제가 없음을 뜻하는 노드 번호입니다. */
        static constexpr uint32 kNoParent = 0xFFFFFFFFu;

        /** @brief @p listThread 의 사건을 호출 트리로 접어 @p outListNode 에 채웁니다(먼저 비운다). 스레드마다 뿌리가 여럿일 수 있다. */
        static void compute( const vector<ProfilerTimelineThread>& listThread, vector<ProfilerCallNode>& outListNode );
        /** @brief 형제를 합 시간이 큰 순으로 다시 잇습니다(노드 번호는 그대로, `_firstChild` · `_nextSibling` 만 바뀐다). */
        static void sortChildrenByTotal( vector<ProfilerCallNode>& inoutListNode );
        /** @brief 뿌리 노드 번호를 스레드 순 · 합 시간이 큰 순으로 @p outListRoot 에 담습니다. */
        static void collectRoots( const vector<ProfilerCallNode>& listNode, vector<uint32>& outListRoot );
    };
} // namespace sw::editor
