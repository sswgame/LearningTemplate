#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    SubsystemScope::SubsystemScope( const ISubsystemLocator* pParent )
        : _collection{}
        , _listEntry{}
        , _listMounted{}
        , _pParent{ pParent }
    {
    }

    SubsystemScope::~SubsystemScope()
    {
        destroyAll();
    }

    void SubsystemScope::destroyAll()
    {
        // 매단 자식을 먼저 지운다 — 자식 서브시스템은 이 범위를 부모로 찾으므로, 이 범위의 서브시스템보다 먼저 내려가야 한다. 매단 역순이다.
        while ( _listMounted.empty() == false )
        {
            unique_ptr<SubsystemScope> child = std::move( _listMounted.back()._scope );
            _listMounted.pop_back();
            child.reset();
        }
        _listMounted.shrink_to_fit();
        // 조회 칸을 먼저 지운다 — 소멸자가 같은 범위를 조회해도 지워지는 서브시스템을 돌려주지 않는다.
        VectorUtil::removeIf( _listEntry, []( const Entry& entry )
        { return entry._bOwned; } );
        // 빌린 칸까지 모두 빠졌으면 저장소도 놓는다(`SubsystemCollection::destroyAll` 과 같은 이유 — 기동 뒤에 자란 칸이 종료 누수 보고에 남지 않게).
        if ( _listEntry.empty() )
            _listEntry.shrink_to_fit();
        _collection.destroyAll();
    }

    void* SubsystemScope::findInScope( uint64 typeHash ) const
    {
        const Entry* pEntry = findOwnEntry( typeHash );
        if ( pEntry != nullptr )
            return pEntry->_pObject;
        return findInMounted( typeHash );
    }

    void* SubsystemScope::findInMounted( uint64 typeHash ) const
    {
        // 매단 자식이 공개한 칸을 매단 순서로 본다(자식끼리 같은 타입을 공개하면 먼저 매단 쪽이 이긴다).
        for ( const MountedScope& mounted : _listMounted )
        {
            void* const pPublished = mounted._scope->findPublished( typeHash );
            if ( pPublished != nullptr )
                return pPublished;
        }
        return nullptr;
    }

    const SubsystemScope::Entry* SubsystemScope::findOwnEntry( uint64 typeHash ) const
    {
        return VectorUtil::findBy<&Entry::_typeHash>( _listEntry, typeHash );
    }

    void* SubsystemScope::findPublished( uint64 typeHash ) const
    {
        const Entry* pEntry = findOwnEntry( typeHash );
        if ( pEntry != nullptr )
            return pEntry->_bPublished ? pEntry->_pObject : nullptr;
        return findInMounted( typeHash );
    }

    bool SubsystemScope::registerEntry( uint64 typeHash, void* pObject, bool bOwned )
    {
        // 겹침은 이 범위 자신의 칸만 본다 — 자식이 공개한 같은 타입이 있어도 이 범위는 제 것을 걸 수 있다(이 범위의 칸이 이긴다).
        if ( findOwnEntry( typeHash ) != nullptr )
            return false;
        _listEntry.push_back( Entry{ typeHash, pObject, bOwned } );
        return true;
    }

    bool SubsystemScope::publishEntry( uint64 typeHash, void* pObject, bool bOwned )
    {
        Entry* const pEntry = VectorUtil::findBy<&Entry::_typeHash>( _listEntry, typeHash );
        if ( pEntry != nullptr )
        {
            if ( pEntry->_pObject != pObject )
                return false;
            pEntry->_bPublished = true;
            return true;
        }
        Entry entry{ typeHash, pObject, bOwned };
        entry._bPublished = true;
        _listEntry.push_back( entry );
        return true;
    }

    SubsystemScope& SubsystemScope::mountChildScope( string_view name )
    {
        if ( SubsystemScope* const pFound = findChildScope( name ) )
            return *pFound;
        _listMounted.push_back( MountedScope{ string{ name }, make_unique<SubsystemScope>( this ) } );
        return *_listMounted.back()._scope;
    }

    void SubsystemScope::unmountChildScope( string_view name )
    {
        for ( size_t index = 0; index < _listMounted.size(); ++index )
        {
            if ( _listMounted[index]._name != name )
                continue;
            // 목록에서 먼저 빼고 지운다 — 자식 서브시스템의 소멸자가 이 범위를 조회해도 지워지는 자식의 칸을 돌려주지 않는다.
            unique_ptr<SubsystemScope> child = std::move( _listMounted[index]._scope );
            _listMounted.erase( _listMounted.begin() + static_cast<ptrdiff_t>( index ) );
            child.reset();
            return;
        }
    }

    SubsystemScope* SubsystemScope::findChildScope( string_view name ) const
    {
        for ( const MountedScope& mounted : _listMounted )
        {
            if ( mounted._name == name )
                return mounted._scope.get();
        }
        return nullptr;
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
        const Entry* pEntry = findOwnEntry( typeHash );
        return pEntry != nullptr && pEntry->_bOwned;
    }
} // namespace sw
