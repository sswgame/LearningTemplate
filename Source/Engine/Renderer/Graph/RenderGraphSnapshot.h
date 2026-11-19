/**
 * @file RenderGraphSnapshot.h
 * @brief 컴파일한 렌더 그래프의 결과(실행 순서 패스 · 레벨 · 큐 · 전이 수, 자원 · 수명 · 접근)를 값으로 옮긴 사본입니다 — 에디터의 렌더 그래프 그림과 개발 명령이 읽는다.
 * @details 언리얼 RDG Insights 의 패스 · 자원 · 수명 · 전이 표, 유니티 Render Graph Viewer 의 자원 접근 격자 자리다. 그래프는 렌더 스레드의 것이라 그 스레드에서 `build` 하고
 *          값만 다른 스레드로 넘긴다(`RenderDebugRegistry`). 전이 수는 실행과 같은 추론(`RenderGraphExecutionContext::transitionTo`)을 빈 문맥으로 다시 돌려 센다 — 들인 자원의 처음 접근은 심는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Renderer/Graph/RenderGraph.h"

namespace sw
{
    /** @brief 패스 하나가 자원 하나를 쓰는 방식입니다(자원은 `RenderGraphSnapshot::_listResource` 번호). */
    struct RenderGraphSnapshotAccess
    {
        uint32            _resource{ 0 };
        RenderGraphAccess _access{ RenderGraphAccess::ShaderRead };
    };

    /** @brief 실행 순서의 패스 하나입니다. */
    struct RenderGraphSnapshotPass
    {
        string                            _name;
        vector<RenderGraphSnapshotAccess> _listAccess;                      ///< 선언한 접근(선언 순서)
        uint32                            _level{ 0 };                      ///< 의존 레벨(같은 레벨은 서로 의존하지 않아 나란히 기록한다)
        uint32                            _transitionCount{ 0 };            ///< 이 패스 앞에서 난 전이 수(같은 상태의 반복은 세지 않는다)
        uint32                            _asyncBatch{ 0 };                 ///< 비동기 컴퓨트 묶음 번호(`_queue` 가 AsyncCompute 일 때만)
        RHIQueueKind                      _queue{ RHIQueueKind::Graphics }; ///< 계획한 큐(비동기를 받지 않는 그래프는 모두 그래픽스)
    };

    /** @brief 자원 하나입니다. */
    struct RenderGraphSnapshotResource
    {
        string                  _name;
        uint32                  _firstPass{ 0 }; ///< 처음 쓰이는 패스(실행 순서 번호)
        uint32                  _lastPass{ 0 };  ///< 마지막으로 쓰이는 패스
        RenderGraphResourceKind _kind{ RenderGraphResourceKind::Transient };
        RenderGraphViewableKind _viewable{ RenderGraphViewableKind::Texture };
        uint8                   _bImported{ SW_FALSE }; ///< 그래프 밖에서 들인 자원이다(`RenderGraph::importResource`)
    };

    /**
     * @struct RenderGraphSnapshot
     * @brief 그래프 하나의 사본입니다. 파일 머리말 참고.
     */
    struct SW_API RenderGraphSnapshot
    {
        string                              _graphName;            ///< 어느 그래프인가(뷰 그래프 · 프레임 그래프)
        vector<RenderGraphSnapshotPass>     _listPass;             ///< 실행 순서
        vector<RenderGraphSnapshotResource> _listResource;         ///< 처음 쓰이는 차례
        uint32                              _levelCount{ 0 };      ///< 레벨 수
        uint32                              _transitionCount{ 0 }; ///< 모든 패스의 전이 수 합

        /** @brief 컴파일한 @p graph 의 사본을 만듭니다. 컴파일 전이면 패스가 없다. */
        static RenderGraphSnapshot build( const RenderGraph& graph, string_view graphName );
        /** @brief 이름 @p name 의 자원 번호입니다. 없으면 nullopt 입니다. */
        optional<uint32> findResource( string_view name ) const;
    };
} // namespace sw
