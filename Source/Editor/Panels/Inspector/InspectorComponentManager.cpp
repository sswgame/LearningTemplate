#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw::editor
{
    void InspectorComponentManager::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
    {
        _registry.addOrReplace( hashed_string( typeName ), std::move( pInspector ) );
    }

    IInspectorComponent* InspectorComponentManager::find( string_view typeName ) const
    {
        const unique_ptr<IInspectorComponent>* pInspector = _registry.find( hashed_string( typeName ) );
        return pInspector != nullptr ? pInspector->get() : nullptr;
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
