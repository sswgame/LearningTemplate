#include "pch.h"

#include "GameFramework/Runtime/Character/Navigation/GridPathSolver.h"

#include "GameFramework/Runtime/Character/Navigation/NavAgent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

namespace sw
{
    GridPathSolver::GridPathSolver( const NavGrid& grid )
        : _pGrid{ &grid }
        , _listPathfinder{}
        , _pChunkPathfinder{ nullptr }
    {
    }

    void GridPathSolver::prepareBatch( vector<GridPathJob>& /*inoutListJob*/, uint32 chunkCount )
    {
        if ( _listPathfinder.size() < chunkCount )
            _listPathfinder.resize( chunkCount );
        _pChunkPathfinder = _listPathfinder.data();
    }

    void GridPathSolver::solve( GridPathJob& inoutJob, uint32 chunkIndex )
    {
        inoutJob._result = NavAgent::planPath( *_pGrid, _pChunkPathfinder[chunkIndex], inoutJob._from, inoutJob._target, inoutJob._listPoint );
    }
} // namespace sw
