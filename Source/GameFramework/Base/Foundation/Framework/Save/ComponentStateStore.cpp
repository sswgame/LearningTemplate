#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct ComponentStateStoreInternal
        {
            /** @brief 항목 하나의 최소 바이트(이름 길이 4 + id 8 + 본문 길이 4)입니다. 파일이 말한 개수를 그대로 잡지 않는 데 씁니다. */
            static constexpr uint64 kMinEntryBytes = sizeof( uint32 ) + sizeof( uint64 ) + sizeof( uint32 );

            /** @brief 정적 등록 목록입니다. 함수 안 정적 객체라 처음 등록할 때 만들어지고, 모든 등록 객체보다 늦게 소멸합니다. */
            static RegistrationList<StatefulComponentRegistration>& getRegistrationList()
            {
                static RegistrationList<StatefulComponentRegistration> s_list{ "StatefulComponentRegistry", RegistrationOrder::Insertion, true };
                return s_list;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ComponentStateStore" );

    StatefulComponentRegistration::StatefulComponentRegistration( const utf8* pTypeName, CaptureFn pCapture, RestoreFn pRestore )
        : _pTypeName{ pTypeName }
        , _pCapture{ pCapture }
        , _pRestore{ pRestore }
    {
        const RegistrationResult result = ComponentStateStoreInternal::getRegistrationList().add( this, string_view( pTypeName ) );
        if ( result != RegistrationResult::Added )
            SW_LOG_ERROR( "Stateful component type '%#' was not registered (duplicate or empty name) - its state does not ride snapshots", pTypeName );
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
    {
    }

    void ComponentStateStore::add( const hashed_string& typeName, uint64 componentID, vector<uint8>&& bytes )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._typeName == typeName && entry._componentID == componentID )
            {
                entry._bytes = std::move( bytes );
                return;
            }
        }
        Entry entry{};
        entry._typeName    = typeName;
        entry._componentID = componentID;
        entry._bytes       = std::move( bytes );
        _listEntry.push_back( std::move( entry ) );
    }

    const ComponentStateStore::Entry* ComponentStateStore::findEntry( const hashed_string& typeName, uint64 componentID, uint32 orderInType ) const
    {
        const Entry* pByOrder = nullptr;
        uint32       order    = 0;
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._typeName != typeName )
                continue;
            if ( entry._componentID == componentID )
                return &entry;
            if ( order == orderInType )
                pByOrder = &entry;
            ++order;
        }
        return pByOrder;
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
        _listEntry.clear();
        uint32 version = 0;
        uint32 count   = 0;
        archive >> version;
        archive >> count;
        if ( archive.isError() || version != kVersion )
            return false;
        // 파일이 말한 개수를 그대로 잡지 않는다 — 깨진 값 하나가 수십 기가를 요구한다.
        if ( static_cast<uint64>( count ) * ComponentStateStoreInternal::kMinEntryBytes > archive.getRemainingBytes() )
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
        return true;
    }
} // namespace sw
