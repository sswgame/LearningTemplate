/**
 * @file ComponentRegistry.cpp
 * @brief 타입별 컴포넌트 레지스트리 구현입니다(타입 · 칸마다 등록 순서 목록).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/TypeInfo.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief 등록이 없는 칸을 물으면 돌려주는 빈 목록입니다. */
        const vector<ComponentRegistry::Entry>& getEmptyList()
        {
            static const vector<ComponentRegistry::Entry> s_listEmpty;
            return s_listEmpty;
        }
    } // namespace

    ComponentRegistry::ComponentRegistry()
        : _mapBucket{}
        , _mutex{}
    {
    }

    ComponentRegistry::~ComponentRegistry() = default;

    uint32 ComponentRegistry::computeTypeKey( const TypeInfo* pType )
    {
        return pType != nullptr ? pType->_fullyQualifiedName.getHash() : 0u;
    }

    void ComponentRegistry::addComponent( Component& component )
    {
        const TypeInfo* pType = component.findCachedTypeInfo();
        if ( pType == nullptr )
            return;
        const uint32 channel = component.getRegistryChannel();
        for ( const hashed_string& typeName : pType->getRegisteredComponentTypes() )
        {
            addEntry( typeName.getHash(), channel, &component );
        }
        for ( const TypeInterfaceEntry& entry : pType->getInterfacesWithBase() )
        {
            addEntry( entry._interfaceName.getHash(), 0, &component );
        }
    }

    void ComponentRegistry::removeComponent( const Component& component )
    {
        const TypeInfo* pType = component.findCachedTypeInfo();
        if ( pType == nullptr )
            return;
        const uint32 channel = component.getRegistryChannel();
        for ( const hashed_string& typeName : pType->getRegisteredComponentTypes() )
        {
            removeEntry( typeName.getHash(), channel, &component );
        }
        for ( const TypeInterfaceEntry& entry : pType->getInterfacesWithBase() )
        {
            removeEntry( entry._interfaceName.getHash(), 0, &component );
        }
    }

    void* ComponentRegistry::findPort( const Entry& entry, const TypeInfo* pInterface )
    {
        return entry._pComponent != nullptr ? entry._pComponent->findInterfacePort( pInterface ) : nullptr;
    }

    ComponentRegistry::Bucket* ComponentRegistry::findBucket( uint32 typeKey, uint32 channel ) const
    {
        const auto found = _mapBucket.find( makeBucketKey( typeKey, channel ) );
        return found != _mapBucket.end() ? found->second.get() : nullptr;
    }

    void ComponentRegistry::addEntry( uint32 typeKey, uint32 channel, Component* pComponent )
    {
        if ( pComponent == nullptr || typeKey == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        Bucket*           pBucket = findBucket( typeKey, channel );
        if ( pBucket == nullptr )
            pBucket = _mapBucket.try_emplace( makeBucketKey( typeKey, channel ), sw::make_unique<Bucket>() ).first->second.get();
        if ( pBucket->_mapComponentToSerial.try_emplace( pComponent, pBucket->_nextSerial ).second == false )
            return; // 이미 있다(멱등)
        pBucket->_listEntry.push_back( Entry{ pComponent, pComponent->getHandle() } );
        pBucket->_listSerial.push_back( pBucket->_nextSerial );
        ++pBucket->_nextSerial;
    }

    void ComponentRegistry::removeEntry( uint32 typeKey, uint32 channel, const Component* pComponent )
    {
        if ( pComponent == nullptr || typeKey == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        Bucket*           pBucket = findBucket( typeKey, channel );
        if ( pBucket == nullptr )
            return;
        const auto found = pBucket->_mapComponentToSerial.find( pComponent );
        if ( found == pBucket->_mapComponentToSerial.end() )
            return;
        // 순서를 지키며 뺀다 — 모두 도는 순회의 차례가 실행마다 같다. 순번이 오름차순이라 자리는 이분 검색이고, 뒤쪽은 바이트로 당긴다.
        const auto      itSerial = std::lower_bound( pBucket->_listSerial.begin(), pBucket->_listSerial.end(), found->second );
        const ptrdiff_t index    = itSerial - pBucket->_listSerial.begin();
        pBucket->_listSerial.erase( itSerial );
        pBucket->_listEntry.erase( pBucket->_listEntry.begin() + index );
        pBucket->_mapComponentToSerial.erase( found );
    }

    const vector<ComponentRegistry::Entry>& ComponentRegistry::findEntries( uint32 typeKey, uint32 channel ) const
    {
        const Bucket* pBucket = findBucket( typeKey, channel );
        return pBucket != nullptr ? pBucket->_listEntry : getEmptyList();
    }

    void ComponentRegistry::reportDuplicateUnique( const TypeInfo* pType, const Component& first, const Component& second )
    {
        const string_view typeName = pType != nullptr ? string_view( pType->_fullyQualifiedName.c_str() ) : string_view( "?" );
        SW_LOG_ERROR_ONCE_FOR_ID( "ComponentRegistry.DuplicateUnique", typeName, "the scene has more than one %# ('%#', '%#') - it is one per scene, so none is used", typeName.data(),
                                  first.getOwnerName().c_str(), second.getOwnerName().c_str() );
    }

    bool ComponentRegistry::containsHandle( uint32 typeKey, uint32 channel, ComponentHandle handle ) const
    {
        if ( handle.isValid() == false )
            return false;
        return VectorUtil::containsBy<&Entry::_handle>( findEntries( typeKey, channel ), handle );
    }
} // namespace sw
