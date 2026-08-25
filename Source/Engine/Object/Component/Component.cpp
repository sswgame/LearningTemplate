#include "pch.h"

#include "Engine/Object/Component/Component.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/ECS/Registry.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{

	Component::Component()
		: Component{ false }
	{
	}

	Component::Component( bool bIsSceneComponent )
		: _pOwner{ nullptr }
		, _pCachedTypeInfo{ nullptr }
		, _componentId{ _s_nextComponentId++ }
		, _componentName{}
		, _onTickDelegate{}
		, _tickGroup{ TickGroup::DuringPhysics }
		, _bActive{ true }
		, _bIsSceneComponent{ bIsSceneComponent }
		, _bIsPendingKill{ false }
	{
		initialize();
	}

	Component::Component( Component&& other ) noexcept
		: _pOwner{ std::exchange( other._pOwner, nullptr ) }
		, _pCachedTypeInfo{ other._pCachedTypeInfo }
		, _componentId{ other._componentId }
		, _componentName{ std::move( other._componentName ) }
		, _onTickDelegate{ std::move( other._onTickDelegate ) }
		, _tickGroup{ other._tickGroup }
		, _bActive{ other._bActive.load( std::memory_order_relaxed ) }
		, _bIsSceneComponent{ other._bIsSceneComponent }
		, _bIsPendingKill{ other._bIsPendingKill.load( std::memory_order_relaxed ) }
	{
	}

	Component& Component::operator=( Component&& other ) noexcept
	{
		if ( this != &other )
		{
			_pOwner			 = std::exchange( other._pOwner, nullptr );
			_pCachedTypeInfo = other._pCachedTypeInfo;
			_componentId	 = other._componentId;
			_componentName	 = std::move( other._componentName );
			_onTickDelegate	 = std::move( other._onTickDelegate );
			_tickGroup		 = other._tickGroup;
			_bActive.store( other._bActive.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			_bIsSceneComponent = other._bIsSceneComponent;
			_bIsPendingKill.store( other._bIsPendingKill.load( std::memory_order_relaxed ), std::memory_order_relaxed );
		}
		return *this;
	}

	void Component::setDefaultGamedataPath( string_view path )
	{
		ComponentDefaults::setDefaultsPath( path );
	}

	string_view Component::getDefaultGamedataPath()
	{
		return ComponentDefaults::getDefaultsPath();
	}

	void Component::onBeginPlay()
	{
	}

	void Component::onEndPlay()
	{
	}

	void Component::onTick( float32 deltaTime )
	{
		if ( _bActive != 0 && _onTickDelegate.isBound() )
			_onTickDelegate( deltaTime );
	}

	void Component::onDestroy()
	{
	}

	void Component::onPropertyChanged( hashed_string propertyName )
	{
		(void)propertyName;
	}

	void Component::setCachedTypeInfo( const TypeInfo* pTypeInfo )
	{
		_pCachedTypeInfo = pTypeInfo;
		if ( pTypeInfo != nullptr )
			ComponentDefaults::applyDefaults( this, *pTypeInfo );
	}

	void Component::setActive( bool bActive )
	{
		_bActive.store( bActive, std::memory_order_relaxed );
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	void Component::setTickGroup( TickGroup group )
	{
		_tickGroup = group;
		if ( _pOwner != nullptr )
			_pOwner->markTickOrderDirty();
	}

	sw::ComponentHandle Component::getHandle() const
	{
		if ( _pOwner == nullptr )
			return {};
		uint32			typeId{ 0 };
		const TypeInfo* pTypeInfo = getTypeInfo();
		if ( pTypeInfo != nullptr )
			typeId = pTypeInfo->_typeId;
		return sw::ComponentHandle::make( _pOwner->getEntityId(), typeId );
	}

	const TypeInfo* Component::getTypeInfo() const
	{
		if ( _pCachedTypeInfo != nullptr )
			return _pCachedTypeInfo;
		if ( engine::areEngineServicesBound() )
			return engine::getTypeRegistry().findType( hashed_string( "sw::Component" ) );
		return nullptr;
	}

	Component::EcsDataView Component::ensureEcsData()
	{
		return {};
	}

	Component::EcsDataView Component::getEcsData() const
	{
		return {};
	}

	bool Component::isActive() const
	{
		if ( _bActive == 0 )
			return false;
		return _pOwner == nullptr || _pOwner->isActiveInHierarchy();
	}

	void Component::initialize()
	{
		const TypeInfo* pTypeInfo = getTypeInfo();
		if ( pTypeInfo != nullptr )
			ComponentDefaults::applyDefaults( this, *pTypeInfo );
	}

	std::atomic<uint64> Component::_s_nextComponentId = 1;

} // namespace sw
