#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentRegistry.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorBuiltin.h"

namespace sw
{
	namespace
	{
		InspectorComponentRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getInspectorComponentRegistry();
			return nullptr;
		}
	} // namespace

	void InspectorComponentRegistry::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
	{
		InspectorComponentRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerTypeImpl( typeName, std::move( pInspector ) );
	}

	IInspectorComponent* InspectorComponentRegistry::find( string_view typeName )
	{
		InspectorComponentRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->findImpl( typeName );
		return nullptr;
	}

	void InspectorComponentRegistry::registerDefaults()
	{
		InspectorComponentRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultsImpl();
	}

	void InspectorComponentRegistry::registerTypeImpl( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
	{
		_mapInspectors[string{ typeName }] = std::move( pInspector );
	}

	IInspectorComponent* InspectorComponentRegistry::findImpl( string_view typeName ) const
	{
		const auto it = _mapInspectors.find( string{ typeName } );
		if ( it != _mapInspectors.end() )
			return it->second.get();
		return nullptr;
	}

	void InspectorComponentRegistry::registerDefaultsImpl()
	{
		registerInspectorBuiltinComponents();
	}
} // namespace sw
