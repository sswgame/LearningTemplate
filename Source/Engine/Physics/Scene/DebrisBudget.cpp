#include "pch.h"

#include "Engine/Physics/Scene/DebrisBudget.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    DebrisOwnerID DebrisBudget::addOwner()
    {
        ++_nextOwner;
        return static_cast<DebrisOwnerID>( _nextOwner );
    }

    void DebrisBudget::removeOwner( DebrisOwnerID owner )
    {
        for ( size_t index = _listRecord.size(); index > 0; --index )
        {
            if ( _listRecord[index - 1]._owner != owner )
                continue;
            _pendingRemovalCount -= _listRecord[index - 1]._bRemovalPending ? 1u : 0u;
            _listRecord.erase( _listRecord.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void DebrisBudget::addBody( DebrisOwnerID owner, uint32 groupID, bool bLarge )
    {
        SW_ASSERT( owner != DebrisOwnerID::None );
        Record record;
        record._owner   = owner;
        record._groupID = groupID;
        record._age     = _nextAge++;
        record._bLarge  = bLarge;
        _listRecord.push_back( record );
    }

    void DebrisBudget::removeBody( DebrisOwnerID owner, uint32 groupID )
    {
        for ( size_t index = 0; index < _listRecord.size(); ++index )
        {
            const Record& record = _listRecord[index];
            if ( record._owner != owner || record._groupID != groupID )
                continue;
            _pendingRemovalCount -= record._bRemovalPending ? 1u : 0u;
            _listRecord.erase( _listRecord.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    void DebrisBudget::planRemovals( uint32 maxBodyCount )
    {
        const uint32 keptCount = getBodyCount() - _pendingRemovalCount;
        if ( keptCount <= maxBodyCount )
            return;
        uint32 overCount = keptCount - maxBodyCount;
        _listOrder.clear();
        for ( uint32 index = 0; index < static_cast<uint32>( _listRecord.size() ); ++index )
        {
            if ( _listRecord[index]._bRemovalPending == false )
                _listOrder.push_back( index );
        }
        // 작은 것 먼저, 그다음 오래된 것 먼저 — 차례는 씬 전체의 나이라 어느 오브젝트가 먼저 틱했는지와 상관없다.
        VectorUtil::sort( _listOrder, [this]( uint32 lhs, uint32 rhs )
        {
            const Record& left  = _listRecord[lhs];
            const Record& right = _listRecord[rhs];
            if ( left._bLarge != right._bLarge )
                return left._bLarge == false;
            return left._age < right._age;
        } );
        for ( size_t orderIndex = 0; orderIndex < _listOrder.size() && overCount > 0; ++orderIndex, --overCount )
        {
            _listRecord[_listOrder[orderIndex]]._bRemovalPending = true;
            ++_pendingRemovalCount;
        }
    }

    void DebrisBudget::takeRemovals( DebrisOwnerID owner, vector<uint32>& outListGroupID )
    {
        if ( _pendingRemovalCount == 0 )
            return;
        for ( Record& record : _listRecord )
        {
            if ( record._owner != owner || record._bRemovalPending == false )
                continue;
            outListGroupID.push_back( record._groupID );
        }
    }
} // namespace sw
