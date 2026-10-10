#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Core/Module/ModuleUnloadListener.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    InspectorComponentManager::InspectorComponentManager()
        : _listEntry{}
        , _syncedGeneration{ kNotSynced }
    {
    }

    InspectorComponentManager::~InspectorComponentManager() = default;

    void InspectorComponentManager::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
    {
        const hashed_string name( typeName );
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

    void InspectorComponentManager::collectForType( const TypeInfo& type, vector<IInspectorComponent*>& outListInspector ) const
    {
        outListInspector.clear();
        vector<const TypeInfo*> listType;
        InspectorPropertyLayout::collectTypeChain( type, listType );
        for ( const TypeInfo* pType : listType )
        {
            IInspectorComponent* pInspector = find( pType->_name.c_str() );
            if ( pInspector != nullptr )
                outListInspector.push_back( pInspector );
        }
    }

    void InspectorComponentManager::registerDefaults()
    {
        _syncedGeneration = kNotSynced;
        syncWithRegistry();
    }

    void InspectorComponentManager::syncWithRegistry()
    {
        using InspectorRegistry            = EditorRegistry<EditorInspectorRegistration>;
        const EditorRegistrationList& list = InspectorRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

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

    uint32 InspectorComponentManager::releaseInspectorsWithin( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listEntry.size(); index-- > 0; )
        {
            if ( IModuleUnloadListener::isAddressWithin( _listEntry[index]._pRegistration, pBegin, pEnd ) == false )
                continue;
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
            ++releasedCount;
        }
        return releasedCount;
    }
} // namespace sw::editor
