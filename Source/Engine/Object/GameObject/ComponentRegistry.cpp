/**
 * @file ComponentRegistry.cpp
 * @brief 타입별 컴포넌트 등록부 구현입니다(타입 · 칸마다 등록 순서 목록).
 */
#include "pch.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    namespace
    {
        struct ComponentRegistryInternal
        {
            /** @brief 등록이 없는 칸을 물으면 돌려주는 빈 목록입니다. */
            static const vector<ComponentRegistry::Entry>& getEmptyList()
            {
                static const vector<ComponentRegistry::Entry> s_listEmpty;
                return s_listEmpty;
            }
        };
    } // namespace

    ComponentRegistry::ComponentRegistry()
        : _listBucket{}
        , _mutex{}
    {
    }

    ComponentRegistry::~ComponentRegistry() = default;

    uint32 ComponentRegistry::computeTypeKey( const TypeInfo* pType )
    {
        return pType != nullptr ? pType->_fullyQualifiedName.getHash() : 0u;
    }

    ComponentRegistry::Bucket* ComponentRegistry::findBucket( uint32 typeKey, uint32 channel ) const
    {
        for ( const unique_ptr<Bucket>& bucket : _listBucket )
        {
            if ( bucket->_typeKey == typeKey && bucket->_channel == channel )
                return bucket.get();
        }
        return nullptr;
    }

    void ComponentRegistry::addEntry( uint32 typeKey, uint32 channel, Component* pComponent )
    {
        if ( pComponent == nullptr || typeKey == 0 )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        Bucket*                 pBucket = findBucket( typeKey, channel );
        if ( pBucket == nullptr )
        {
            _listBucket.push_back( sw::make_unique<Bucket>( Bucket{ typeKey, channel, {} } ) );
            pBucket = _listBucket.back().get();
        }
        for ( const Entry& entry : pBucket->_listEntry )
        {
            if ( entry._pComponent == pComponent )
                return; // 이미 있다(멱등)
        }
        pBucket->_listEntry.push_back( Entry{ pComponent, pComponent->getHandle() } );
    }

    void ComponentRegistry::removeEntry( uint32 typeKey, uint32 channel, const Component* pComponent )
    {
        if ( pComponent == nullptr || typeKey == 0 )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        Bucket*                 pBucket = findBucket( typeKey, channel );
        if ( pBucket == nullptr )
            return;
        vector<Entry>& listEntry = pBucket->_listEntry;
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            if ( listEntry[index]._pComponent != pComponent )
                continue;
            // 순서를 지키며 뺀다 — "활성인 첫 것" 고르기가 등록 순서로 정해진다.
            listEntry.erase( listEntry.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    const vector<ComponentRegistry::Entry>& ComponentRegistry::findEntries( uint32 typeKey, uint32 channel ) const
    {
        const Bucket* pBucket = findBucket( typeKey, channel );
        return pBucket != nullptr ? pBucket->_listEntry : ComponentRegistryInternal::getEmptyList();
    }

    bool ComponentRegistry::containsHandle( uint32 typeKey, uint32 channel, ComponentHandle handle ) const
    {
        if ( handle.isValid() == false )
            return false;
        for ( const Entry& entry : findEntries( typeKey, channel ) )
        {
            if ( entry._handle == handle )
                return true;
        }
        return false;
    }
} // namespace sw
