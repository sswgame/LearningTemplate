#include "pch.h"

#include "Engine/Navigation/Scene/NavPathSolver.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
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
                job._pNavMesh = nullptr;
                continue;
            }
            job._pNavMesh     = pRuntime->_pNavMesh.get();
            job._searchExtent = pRuntime->_searchExtent;
            if ( job._bCustomFilter == false )
                job._filter = pRuntime->_filter;
        }
    }

    void NavPathSolver::solve( NavPathJob& inoutJob, uint32 /*chunkIndex*/ ) const
    {
        if ( inoutJob._pNavMesh == nullptr )
        {
            inoutJob._path = NavPath{};
            return;
        }
        (void)inoutJob._pNavMesh->findPath( inoutJob._start, inoutJob._end, inoutJob._searchExtent, inoutJob._filter, inoutJob._path ); // 상태는 `_path._status` 에도 있다
    }
} // namespace sw
