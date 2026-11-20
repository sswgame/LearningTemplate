#include "pch.h"

#include "Engine/Navigation/Scene/NavPathSolver.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Navigation/Scene/NavStreamRoute.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"

namespace sw
{
    void NavPathSolver::prepareBatch( vector<NavPathJob>& inoutListJob, uint32 /*chunkCount*/ ) const
    {
        for ( NavPathJob& job : inoutListJob )
        {
            const NavMeshRuntime* pRuntime = _pNavigation->findRuntime( job._agentType );
            if ( pRuntime == nullptr || pRuntime->_pNavMesh == nullptr )
            {
                job._pRuntime = nullptr;
                continue;
            }
            job._pRuntime     = pRuntime;
            job._searchExtent = pRuntime->_searchExtent;
            if ( job._bCustomFilter == false )
                job._filter = pRuntime->_filter;
        }
    }

    void NavPathSolver::solve( NavPathJob& inoutJob, uint32 /*chunkIndex*/ ) const
    {
        if ( inoutJob._pRuntime == nullptr )
        {
            inoutJob._path = NavPath{};
            return;
        }
        // 동기 질의(`SceneNavigation::findStreamingPath`)와 같은 함수 — 스트리밍 씬의 오르지 않은 타일 너머 끝을 실패로 끝내지 않는다. 상태는 `_path._status` 에도 있다.
        (void)NavStreamRoute::findPath( *inoutJob._pRuntime, inoutJob._start, inoutJob._end, inoutJob._searchExtent, inoutJob._filter, inoutJob._path );
    }
} // namespace sw
