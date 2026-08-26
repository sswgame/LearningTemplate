#include "pch.h"

#include "Editor/Inspector/PropertyDrawerRegistry.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Inspector/DefaultPropertyDrawers.h"
#include "Editor/Inspector/IPropertyDrawer.h"

namespace sw
{
	namespace
	{
		PropertyDrawerRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getPropertyDrawerRegistry();
			return nullptr;
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void PropertyDrawerRegistry::registerDrawer( string_view typeName, unique_ptr<IPropertyDrawer> drawer )
	{
		PropertyDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDrawerImpl( typeName, std::move( drawer ) );
	}

	IPropertyDrawer* PropertyDrawerRegistry::getDrawer( string_view typeName )
	{
		PropertyDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getDrawerImpl( typeName );
		return nullptr;
	}

	void PropertyDrawerRegistry::registerDefaultDrawers()
	{
		PropertyDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultDrawersImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void PropertyDrawerRegistry::registerDrawerImpl( string_view typeName, unique_ptr<IPropertyDrawer> drawer )
	{
		if ( typeName.empty() || drawer == nullptr )
			return;
		_mapDrawers[string{ typeName }] = std::move( drawer );
	}

	IPropertyDrawer* PropertyDrawerRegistry::getDrawerImpl( string_view typeName ) const
	{
		if ( typeName.empty() )
			return nullptr;

		auto it = _mapDrawers.find( string{ typeName } );
		if ( it != _mapDrawers.end() )
			return it->second.get();

		return nullptr;
	}

	void PropertyDrawerRegistry::registerDefaultDrawersImpl()
	{
		registerDefaultPropertyDrawers();
	}
} // namespace sw
