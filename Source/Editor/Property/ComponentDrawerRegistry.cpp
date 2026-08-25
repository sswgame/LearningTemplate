#include "pch.h"

#include "Editor/Property/ComponentDrawerRegistry.h"

#include "Editor/Common/EditorContext.h"
#include "Editor/Property/DefaultComponentDrawers.h"
#include "Editor/Property/IComponentDrawer.h"

namespace sw
{
	namespace
	{
		ComponentDrawerRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getComponentDrawerRegistry();
			return nullptr;
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void ComponentDrawerRegistry::registerDrawer( string_view typeName, unique_ptr<IComponentDrawer> drawer )
	{
		ComponentDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDrawerImpl( typeName, std::move( drawer ) );
	}

	IComponentDrawer* ComponentDrawerRegistry::getDrawer( string_view typeName )
	{
		ComponentDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->getDrawerImpl( typeName );
		return nullptr;
	}

	void ComponentDrawerRegistry::registerDefaultDrawers()
	{
		ComponentDrawerRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultDrawersImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void ComponentDrawerRegistry::registerDrawerImpl( string_view typeName, unique_ptr<IComponentDrawer> drawer )
	{
		_mapDrawers[string{ typeName }] = std::move( drawer );
	}

	IComponentDrawer* ComponentDrawerRegistry::getDrawerImpl( string_view typeName ) const
	{
		const auto it = _mapDrawers.find( string{ typeName } );
		if ( it != _mapDrawers.end() )
			return it->second.get();
		return nullptr;
	}

	void ComponentDrawerRegistry::registerDefaultDrawersImpl()
	{
		registerDefaultComponentDrawers();
	}
} // namespace sw
