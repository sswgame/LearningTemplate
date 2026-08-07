/**
 * @file Component.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "Component.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/GameObject.h"

namespace sw
{
	uint64 Component::s_nextComponentId = 1;

	Component::Component()
		: _componentId( s_nextComponentId++ )
		, _bActive( 1 )
	{
	}

	const TypeInfo* Component::getTypeInfo() const
	{
		return sw::getTypeRegistry().findType( hashed_string( "sw::Component" ) );
	}

	void Component::onBeginPlay()
	{
	}

	void Component::onTick( float32 deltaTime )
	{
		if ( _bActive != 0 && _onTickDelegate.isBound() )
		{
			_onTickDelegate( deltaTime );
		}
	}

	void Component::onDestroy()
	{
	}

	void Component::setActive( bool bActive )
	{
		_bActive = bActive ? 1 : 0;
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	void Component::onPropertyChanged( hashed_string propertyName )
	{

		(void)propertyName;
	}

	void Component::setTickGroup( TickGroup group )
	{
		_tickGroup = group;
		if ( _owner != nullptr )
		{
			_owner->markTickOrderDirty();
		}
	}

	void Component::addTickDependency( Component* targetComp )
	{
		if ( targetComp == nullptr || targetComp == this )
			return;

		for ( Component* dep : _tickDependencies )
		{
			if ( dep == targetComp )
				return;
		}
		_tickDependencies.push_back( targetComp );
	}
}
