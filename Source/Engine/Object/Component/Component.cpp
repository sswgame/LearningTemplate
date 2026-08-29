#include "pch.h"

#include "Engine/Object/Component/Component.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	Component::Component()
		: _pOwner{ nullptr }
		, _componentId{ _s_nextComponentId.fetch_add( 1, std::memory_order_relaxed ) }
		, _componentName{}
		, _tickGroup{ TickGroup::DuringPhysics }
		, _bActive{ true }
		, _bCanEverTick{ SW_TRUE }
		, _reservedFlags{ 0 }
		, _bIsPendingKill{ false }
	{
		initialize();
	}

	Component::Component( Component&& other ) noexcept
		: _pOwner{ std::exchange( other._pOwner, nullptr ) }
		, _componentId{ other._componentId }
		, _componentName{ std::move( other._componentName ) }
		, _tickGroup{ other._tickGroup }
		, _bActive{ other._bActive.load( std::memory_order_relaxed ) }
		, _bCanEverTick{ other._bCanEverTick }
		, _reservedFlags{ other._reservedFlags }
		, _bIsPendingKill{ other._bIsPendingKill.load( std::memory_order_relaxed ) }
	{
	}

	Component& Component::operator=( Component&& other ) noexcept
	{
		if ( this != &other )
		{
			_pOwner		   = std::exchange( other._pOwner, nullptr );
			_componentId   = other._componentId;
			_componentName = std::move( other._componentName );
			_tickGroup	   = other._tickGroup;
			_bActive.store( other._bActive.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			_bCanEverTick  = other._bCanEverTick;
			_reservedFlags = other._reservedFlags;
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
		(void)deltaTime;
	}

	void Component::onDestroy()
	{
	}

	void Component::onPropertyChanged( hashed_string propertyName )
	{
		(void)propertyName;
	}

	void Component::applyTypeDefaults( const TypeInfo* pTypeInfo )
	{
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

	void Component::setCanEverTick( bool bCanEverTick )
	{
		const uint8 newValue = bCanEverTick ? SW_TRUE : SW_FALSE;
		if ( _bCanEverTick == newValue )
			return;
		_bCanEverTick = newValue;
		if ( _pOwner != nullptr )
			_pOwner->markTickOrderDirty();
	}

	sw::ComponentHandle Component::getHandle() const
	{
		if ( _pOwner == nullptr )
			return {};
		return sw::ComponentHandle::makeOwned( _pOwner->getObjectId(), _componentId );
	}

	const TypeInfo* Component::getTypeInfo() const
	{
		if ( engine::areEngineServicesBound() == false )
			return nullptr;
		if ( _componentName.empty() == false )
		{
			const TypeInfo* pType = engine::getTypeRegistry().findType( _componentName );
			if ( pType != nullptr )
				return pType;
		}
		return engine::getTypeRegistry().findType<Component>();
	}

	bool Component::isActive() const
	{
		if ( _bActive.load( std::memory_order_relaxed ) == false )
			return false;
		return _pOwner == nullptr || _pOwner->isActiveInHierarchy();
	}

	void Component::initialize()
	{
		const TypeInfo* pTypeInfo = getTypeInfo();
		if ( pTypeInfo != nullptr )
			ComponentDefaults::applyDefaults( this, *pTypeInfo );
	}

	atomic<uint64> Component::_s_nextComponentId = 1;

} // namespace sw
