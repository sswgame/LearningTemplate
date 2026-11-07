#include "pch.h"

#include "GameFramework/Runtime/Character/Navigation/GridPathSolver.h"

#include "GameFramework/Runtime/Character/Navigation/NavAgent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

namespace sw
{
    GridPathSolver::GridPathSolver( const NavGrid& grid )
        : _pGrid{ &grid }
        , _listPathfinder{}
        , _pLanePathfinder{ nullptr }
    {
    }

    void GridPathSolver::prepareBatch( vector<GridPathJob>& /*inoutListJob*/, uint32 laneCount )
    {
        if ( _listPathfinder.size() < laneCount )
            _listPathfinder.resize( laneCount );
        _pLanePathfinder = _listPathfinder.data();
    }

    void GridPathSolver::solve( GridPathJob& inoutJob, uint32 laneIndex )
    {
        inoutJob._result = NavAgent::planPath( *_pGrid, _pLanePathfinder[laneIndex], inoutJob._from, inoutJob._target, inoutJob._listPoint );
    }
} // namespace sw
