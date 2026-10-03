#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    void InspectorComponentManager::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
    {
        _mapInspector[string{ typeName }] = std::move( pInspector );
    }

    IInspectorComponent* InspectorComponentManager::find( string_view typeName ) const
    {
        const auto it = _mapInspector.find( string{ typeName } );
        if ( it != _mapInspector.end() )
            return it->second.get();
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
        using InspectorRegistry = EditorRegistry<EditorInspectorRegistration>;
        for ( uint32 index = 0; index < InspectorRegistry::getCount(); ++index )
        {
            const EditorInspectorRegistration& registration = InspectorRegistry::getAt( index );
            registerType( registration._pGetComponentType()->_name.c_str(), registration._pCreate() );
        }
    }
} // namespace sw::editor
