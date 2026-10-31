#include "pch.h"

#include "GameFramework/Runtime/Character/Navigation/FlowFieldCache.h"

#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

namespace sw
{
    const FlowField* FlowFieldCache::acquire( const NavGrid& grid, const int2& goal )
    {
        Slot* pFree = nullptr;
        for ( Slot& slot : _listSlot )
        {
            if ( slot._userCount > 0 && slot._goal == goal )
            {
                ++slot._userCount;
                return &slot._field;
            }
            if ( slot._userCount == 0 && pFree == nullptr )
                pFree = &slot;
        }
        if ( pFree == nullptr )
            pFree = &_listSlot.emplace_back();
        pFree->_goal      = goal;
        pFree->_userCount = 1;
        (void)pFree->_field.computeToCell( grid, goal );
        return &pFree->_field;
    }

    void FlowFieldCache::release( const FlowField* pField )
    {
        for ( Slot& slot : _listSlot )
        {
            if ( &slot._field == pField && slot._userCount > 0 )
            {
                --slot._userCount;
                return;
            }
        }
    }

    void FlowFieldCache::refreshStale( const NavGrid& grid )
    {
        for ( Slot& slot : _listSlot )
        {
            if ( slot._userCount > 0 && slot._field.isStale( grid ) )
                (void)slot._field.computeToCell( grid, slot._goal );
        }
    }
} // namespace sw
