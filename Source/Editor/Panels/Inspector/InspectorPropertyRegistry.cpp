#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyRegistry.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorBuiltin.h"

namespace sw::editor
{
	namespace
	{
		InspectorPropertyRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getInspectorPropertyRegistry();
			return nullptr;
		}
	} // namespace

	void InspectorPropertyRegistry::registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty )
	{
		InspectorPropertyRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerTypeImpl( typeName, std::move( pProperty ) );
	}

	IInspectorProperty* InspectorPropertyRegistry::find( string_view typeName )
	{
		InspectorPropertyRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->findImpl( typeName );
		return nullptr;
	}

	void InspectorPropertyRegistry::registerDefaults()
	{
		InspectorPropertyRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultsImpl();
	}

	void InspectorPropertyRegistry::registerTypeImpl( string_view typeName, unique_ptr<IInspectorProperty> pProperty )
	{
		if ( typeName.empty() || pProperty == nullptr )
			return;
		_mapProperties[string{ typeName }] = std::move( pProperty );
	}

	IInspectorProperty* InspectorPropertyRegistry::findImpl( string_view typeName ) const
	{
		if ( typeName.empty() )
			return nullptr;

		auto it = _mapProperties.find( string{ typeName } );
		if ( it != _mapProperties.end() )
			return it->second.get();
		return nullptr;
	}

	void InspectorPropertyRegistry::registerDefaultsImpl()
	{
		registerInspectorBuiltinProperties();
	}
} // namespace sw::editor
