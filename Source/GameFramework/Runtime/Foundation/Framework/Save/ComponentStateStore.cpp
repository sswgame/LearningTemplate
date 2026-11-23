#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/Reflection.h"

namespace sw
{
    namespace
    {
        struct ComponentStateStoreInternal
        {
            /** @brief 항목 하나의 최소 바이트(이름 길이 4 + id 8 + 본문 길이 4)입니다. 파일이 말한 개수를 그대로 잡지 않는 데 씁니다. */
            static constexpr uint32 kMinEntryBytes = sizeof( uint32 ) + sizeof( uint64 ) + sizeof( uint32 );

            /** @brief 정적 등록 목록입니다. 함수 안 정적 객체라 처음 등록할 때 만들어지고, 모든 등록 객체보다 늦게 소멸합니다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrationList, StatefulComponentRegistration, "StatefulComponentRegistry", RegistrationOrder::Insertion, true )
        };
    } // namespace
} // namespace sw

namespace sw
{
    StatefulComponentRegistration::StatefulComponentRegistration( const utf8* pTypeName, CaptureFn pCapture, RestoreFn pRestore, DespawnFn pDespawn )
        : _pTypeName{ pTypeName }
        , _pCapture{ pCapture }
        , _pRestore{ pRestore }
        , _pDespawn{ pDespawn }
    {
        (void)ComponentStateStoreInternal::getRegistrationList().addLogged( this, "Stateful component type", string_view( pTypeName ) );
    }

    StatefulComponentRegistration::~StatefulComponentRegistration()
    {
        (void)ComponentStateStoreInternal::getRegistrationList().remove( this ); // 거절된 중복 등록은 목록에 없다
    }

    const vector<StatefulComponentRegistration*>& StatefulComponentRegistration::getRegistrations()
    {
        return ComponentStateStoreInternal::getRegistrationList().getItems();
    }
} // namespace sw

namespace sw
{
    ComponentStateStore::ComponentStateStore()
        : _listEntry{}
        , _mapKeyToIndex{}
    {
    }

    void ComponentStateStore::add( const hashed_string& typeName, uint64 componentID, vector<uint8>&& bytes )
    {
        const auto inserted = _mapKeyToIndex.try_emplace( EntryKey{ typeName, componentID }, static_cast<uint32>( _listEntry.size() ) );
        if ( inserted.second == false )
        {
            _listEntry[inserted.first->second]._bytes = std::move( bytes );
            return;
        }
        Entry entry{};
        entry._typeName    = typeName;
        entry._componentID = componentID;
        entry._bytes       = std::move( bytes );
        _listEntry.push_back( std::move( entry ) );
    }

    const ComponentStateStore::Entry* ComponentStateStore::findEntry( const hashed_string& typeName, uint64 componentID ) const
    {
        const auto found = _mapKeyToIndex.find( EntryKey{ typeName, componentID } );
        return found != _mapKeyToIndex.end() ? &_listEntry[found->second] : nullptr;
    }

    void ComponentStateStore::rebuildIndex()
    {
        _mapKeyToIndex.clear();
        _mapKeyToIndex.reserve( _listEntry.size() );
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            (void)_mapKeyToIndex.try_emplace( EntryKey{ _listEntry[index]._typeName, _listEntry[index]._componentID }, static_cast<uint32>( index ) );
        }
    }

    void ComponentStateStore::remapComponentIDs( const vector<SavedComponentID>& listRemap )
    {
        unordered_map<uint64, uint64> mapSavedToLive;
        mapSavedToLive.reserve( listRemap.size() );
        for ( const SavedComponentID& remap : listRemap )
        {
            (void)mapSavedToLive.try_emplace( remap._savedID, remap._liveID ); // 같은 저장 id 가 둘이면 앞 것
        }
        vector<Entry> listRemapped;
        listRemapped.reserve( _listEntry.size() );
        for ( Entry& entry : _listEntry )
        {
            const auto found = mapSavedToLive.find( entry._componentID );
            if ( found == mapSavedToLive.end() )
                continue;
            entry._componentID = found->second;
            listRemapped.push_back( std::move( entry ) );
        }
        _listEntry = std::move( listRemapped );
        rebuildIndex();
    }

    void ComponentStateStore::appendComponentIDRemap( const ObjectIdentity& identity, const GameObject& object, vector<SavedComponentID>& outListRemap )
    {
        for ( const Component* pComponent : object.getComponents() )
        {
            const bool bSkipComponent = pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->getTypeInfo() == nullptr;
            if ( bSkipComponent )
                continue;
            const hashed_string typeFqn = pComponent->getTypeInfo()->_fullyQualifiedName;
            for ( const ObjectIdentity::ComponentEntry& saved : identity._listComponent )
            {
                // 지역 id 키로 맞춘다(`ComponentStableKey::matchesKey`) — 옛 세이브의 `이름#n` 은 맞지 않아 표에 들지 않는다.
                if ( saved._typeFqn != typeFqn || ComponentStableKey::matchesKey( pComponent, saved._stableKey ) == false )
                    continue;
                outListRemap.push_back( SavedComponentID{ saved._componentID, pComponent->getComponentID() } );
                break;
            }
        }
    }

    void ComponentStateStore::write( Archive& outArchive ) const
    {
        outArchive << kVersion;
        outArchive << static_cast<uint32>( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            outArchive << string_view( entry._typeName.c_str() );
            outArchive << entry._componentID;
            outArchive << entry._bytes;
        }
    }

    bool ComponentStateStore::read( Archive& archive )
    {
        clear();
        uint32 version = 0;
        uint32 count   = 0;
        archive >> version;
        if ( archive.isError() || version != kVersion )
            return false;
        // 파일이 말한 개수를 그대로 잡지 않는다 — 깨진 값 하나가 수십 기가를 요구한다.
        count = archive.readCount( ComponentStateStoreInternal::kMinEntryBytes );
        if ( archive.isError() )
            return false;
        _listEntry.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            string typeName;
            Entry  entry{};
            archive >> typeName;
            archive >> entry._componentID;
            archive >> entry._bytes;
            if ( archive.isError() )
            {
                _listEntry.clear();
                return false;
            }
            entry._typeName = hashed_string( typeName );
            _listEntry.push_back( std::move( entry ) );
        }
        rebuildIndex();
        return true;
    }
} // namespace sw
