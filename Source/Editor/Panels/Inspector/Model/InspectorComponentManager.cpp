#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    InspectorComponentManager::InspectorComponentManager()
        : _listEntry{}
        , _mapTypeInspector{}
        , _syncedGeneration{ kNotSynced }
        , _cacheTypeGeneration{ 0 }
    {
    }

    InspectorComponentManager::~InspectorComponentManager() = default;

    void InspectorComponentManager::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
    {
        const hashed_string name( typeName );
        resetTypeCache( _cacheTypeGeneration ); // 확장 목록이 바뀐다 — 타입별 목록이 옛 확장을 가리키지 않게 버린다
        for ( InspectorComponentEntry& entry : _listEntry )
        {
            if ( entry._typeName == name )
            {
                entry._pInstance     = std::move( pInspector );
                entry._pRegistration = nullptr;
                return;
            }
        }
        InspectorComponentEntry entry{};
        entry._typeName  = name;
        entry._pInstance = std::move( pInspector );
        _listEntry.push_back( std::move( entry ) );
    }

    IInspectorComponent* InspectorComponentManager::find( string_view typeName ) const
    {
        const hashed_string name( typeName );
        for ( const InspectorComponentEntry& entry : _listEntry )
        {
            if ( entry._typeName == name )
                return entry._pInstance.get();
        }
        return nullptr;
    }

    const vector<IInspectorComponent*>& InspectorComponentManager::getInspectorsForType( const TypeInfo& type )
    {
        // 모듈 리로드는 TypeInfo 주소를 바꾼다 — 레지스트리 세대가 바뀌면 타입별 목록을 버린다. 확장 목록이 바뀌면 `resetTypeCache` 가 버린다.
        const uint32 typeGeneration = TypeRegistry::getGeneration();
        if ( typeGeneration != _cacheTypeGeneration )
            resetTypeCache( typeGeneration );
        const auto iter = _mapTypeInspector.find( &type );
        if ( iter != _mapTypeInspector.end() )
            return iter->second;

        vector<IInspectorComponent*> listInspector;
        vector<const TypeInfo*>      listType;
        InspectorPropertyLayout::collectTypeChain( type, listType );
        for ( const TypeInfo* pType : listType )
        {
            IInspectorComponent* pInspector = find( pType->_name.c_str() );
            if ( pInspector != nullptr )
                listInspector.push_back( pInspector );
        }
        return _mapTypeInspector.emplace( &type, std::move( listInspector ) ).first->second;
    }

    void InspectorComponentManager::resetTypeCache( uint32 typeGeneration )
    {
        _mapTypeInspector.clear();
        _cacheTypeGeneration = typeGeneration;
    }

    void InspectorComponentManager::registerDefaults()
    {
        _syncedGeneration = kNotSynced;
        syncWithRegistry();
    }

    void InspectorComponentManager::syncWithRegistry()
    {
        using InspectorRegistry                     = EditorExtensionRegistry<EditorInspectorRegistration>;
        const EditorExtensionRegistrationList& list = InspectorRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();
        resetTypeCache( _cacheTypeGeneration ); // 아래에서 확장을 지우고 만든다

        // 1) 등록 목록에서 사라진 줄의 확장을 지운다
        for ( size_t index = _listEntry.size(); index-- > 0; )
        {
            const InspectorComponentEntry& entry = _listEntry[index];
            if ( entry._pRegistration == nullptr || InspectorRegistry::find( entry._pRegistration->_pID ) == entry._pRegistration )
                continue;
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
        }
        // 2) 새 줄은 만든다 — 같은 타입에 직접 건 확장이 있으면 등록 줄이 바꾼다
        for ( uint32 index = 0; index < InspectorRegistry::getCount(); ++index )
        {
            const EditorInspectorRegistration& registration = InspectorRegistry::getAt( index );
            bool                               bExisting    = false;
            for ( const InspectorComponentEntry& entry : _listEntry )
            {
                bExisting = bExisting || entry._pRegistration == &registration;
            }
            if ( bExisting )
                continue;
            registerType( registration._pGetComponentType()->_name.c_str(), registration._pCreate() );
            for ( InspectorComponentEntry& entry : _listEntry )
            {
                if ( entry._typeName == registration._pGetComponentType()->_name )
                    entry._pRegistration = &registration;
            }
        }
    }

} // namespace sw::editor
