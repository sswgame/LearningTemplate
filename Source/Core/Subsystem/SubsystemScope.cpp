#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    SubsystemScope::SubsystemScope( const ISubsystemLocator* pParent )
        : _collection{}
        , _listEntry{}
        , _pParent{ pParent }
    {
    }

    SubsystemScope::~SubsystemScope()
    {
        destroyAll();
    }

    void SubsystemScope::destroyAll()
    {
        // 조회 칸을 먼저 지운다 — 소멸자가 같은 범위를 조회해도 지워지는 서브시스템을 돌려주지 않는다.
        VectorUtil::removeIf( _listEntry, []( const Entry& entry )
        { return entry._bOwned; } );
        _collection.destroyAll();
    }

    void* SubsystemScope::findInScope( uint64 typeHash ) const
    {
        const Entry* pEntry = VectorUtil::findBy<&Entry::_typeHash>( _listEntry, typeHash );
        return pEntry != nullptr ? pEntry->_pObject : nullptr;
    }

    bool SubsystemScope::registerEntry( uint64 typeHash, void* pObject, bool bOwned )
    {
        if ( findInScope( typeHash ) != nullptr )
            return false;
        _listEntry.push_back( Entry{ typeHash, pObject, bOwned } );
        return true;
    }

    void SubsystemScope::unregisterBorrowed( uint64 typeHash )
    {
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._typeHash == typeHash && _listEntry[index]._bOwned == false )
            {
                _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    bool SubsystemScope::isOwnedEntry( uint64 typeHash ) const
    {
        const Entry* pEntry = VectorUtil::findBy<&Entry::_typeHash>( _listEntry, typeHash );
        return pEntry != nullptr && pEntry->_bOwned;
    }
} // namespace sw
