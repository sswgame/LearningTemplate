/**
 * @file ProfilerCallTree.h
 * @brief 프로파일러 패널 Call Tree 탭의 집계입니다 — 타임라인 녹화의 사건을 호출 경로(스레드 → 바깥 구간 → 안 구간)로 접습니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/ProfilerTimeline.h"

namespace sw::editor
{
    /** @brief 호출 트리의 노드 하나입니다. 같은 부모 아래 같은 구간(슬롯)은 한 노드로 접는다. */
    struct ProfilerCallNode
    {
        uint64 _totalNanos{ 0 };  ///< 이 경로로 불린 시간의 합
        uint64 _selfNanos{ 0 };   ///< 합에서 자식 구간을 뺀 시간
        uint32 _slot{ 0 };        ///< `FrameProfiler` 슬롯 — 이름은 `FrameProfiler::findScopeName`
        uint32 _parent{ 0 };      ///< 부모 노드 번호(루트는 `kNoParent`)
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

        /** @brief @p listThread 의 사건을 호출 트리로 접어 @p outListNode 에 채웁니다(먼저 비운다). 스레드마다 루트가 여럿일 수 있다. */
        static void compute( const vector<ProfilerTimelineThread>& listThread, vector<ProfilerCallNode>& outListNode );
        /** @brief 형제를 합 시간이 큰 순으로 다시 잇습니다(노드 번호는 그대로, `_firstChild` · `_nextSibling` 만 바뀐다). */
        static void sortChildrenByTotal( vector<ProfilerCallNode>& inoutListNode );
        /** @brief 루트 노드 번호(불린 적이 있는 것만)를 스레드 순 · 합 시간이 큰 순으로 @p outListRoot 에 담습니다. */
        static void collectRoots( const vector<ProfilerCallNode>& listNode, vector<uint32>& outListRoot );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ProfilerCallTreeWindow
     * @brief 최근 N 프레임의 호출 트리를 슬라이딩 합으로 듭니다 — 새 프레임이 들어오면 그 프레임만 접어 더하고, 창을 벗어난 프레임은 기록해 둔 몫을 뺍니다.
     * @details 매 그리기마다 최근 N 프레임 전체를 다시 접고 정렬하던 것을 바꾼 것이다. 노드는 경로(스레드 이름 → 바깥 구간 → 안 구간)마다 하나이고 번호가
     *          바뀌지 않는다(호출이 0 이 된 노드도 남고, 루트 목록 · 그리기에서 빠진다). 형제 정렬과 루트 목록은 `refreshOrder` 를 부를 때만 다시 짓는다.
     */
    class ProfilerCallTreeWindow
    {
    public:
        /** @brief 창 크기를 @p frameCapacity 프레임으로 정하고 모두 비웁니다(0 은 1 로 본다). */
        void reset( uint32 frameCapacity );
        /** @brief 한 프레임의 사건(`ProfilerTimeline::collectRecentFrames( 1, … )`)을 더하고, 창을 넘친 가장 오래된 프레임을 뺍니다. */
        void addFrame( const vector<ProfilerTimelineThread>& listThread );
        /** @brief 형제를 합 시간 큰 순으로 다시 잇고 루트 목록을 다시 짓습니다. 표시 갱신 주기에만 부릅니다. */
        void refreshOrder();

        const vector<ProfilerCallNode>& getNodes() const { return _listNode; }
        /** @brief 마지막 `refreshOrder` 의 루트(스레드 순, 큰 것 먼저)입니다. */
        const vector<uint32>& getRoots() const { return _listRoot; }
        /** @brief 노드의 `_threadIndex` 가 가리키는 스레드 이름입니다. */
        const vector<string>& getThreadNames() const { return _listThreadName; }
        /** @brief 지금 창에 든 프레임 수입니다(채워지는 동안은 창 크기보다 작다). 프레임당 평균의 분모입니다. */
        uint32 getFrameCount() const { return static_cast<uint32>( _listFrame.size() ); }
        /** @brief 창 크기(프레임 수)입니다. */
        uint32 getFrameCapacity() const { return _frameCapacity; }
        /** @brief 창 안에서 불린 적이 있는 노드 수입니다. */
        uint32 countLiveNodes() const;

    private:
        /** @brief 한 프레임이 노드 하나에 더한 몫입니다. 프레임이 창을 벗어날 때 그대로 뺀다. */
        struct Contribution
        {
            uint64 _totalNanos{ 0 };
            uint64 _selfNanos{ 0 };
            uint32 _node{ 0 };
            uint32 _callCount{ 0 };
        };

        /** @brief @p parent 아래(루트면 스레드 @p threadIndex 의 루트 중) 슬롯 @p slot 인 노드 번호입니다. 없으면 만들어 부모의 마지막 자식으로 잇습니다. */
        uint32 getOrCreateNode( uint32 parent, uint32 threadIndex, uint32 slot, uint16 depth );
        /** @brief 이름 @p name 인 스레드 번호입니다. 없으면 더합니다. */
        uint32 getOrCreateThread( const string& name );

        vector<ProfilerCallNode>    _listNode;
        vector<uint32>              _listRootNode;   ///< 모든 루트 노드(만든 순서). 합칠 때 찾는 곳이다
        vector<uint32>              _listRoot;       ///< 표시용 루트(`refreshOrder` 가 정렬한다)
        vector<string>              _listThreadName; ///< 창 안의 스레드 번호 → 이름
        deque<vector<Contribution>> _listFrame;      ///< 창 안의 프레임마다 더한 몫(오래된 것 먼저)
        vector<ProfilerCallNode>    _listFrameNode;  ///< 한 프레임을 접은 트리(다시 쓰는 버퍼)
        vector<uint32>              _listFrameMap;   ///< 한 프레임 트리의 노드 → 창 노드(다시 쓰는 버퍼)
        vector<uint32>              _listThreadMap;  ///< 한 프레임의 스레드 번호 → 창 스레드 번호(다시 쓰는 버퍼)
        uint32                      _frameCapacity{ 1 };
    };
} // namespace sw::editor
