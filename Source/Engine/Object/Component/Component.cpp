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
		, _subTickActiveMask{ 0 }
		, _bActive{ true }
		, _bIsPendingKill{ false }
		, _tickGroup{ TickGroup::DuringPhysics }
		, _bCanEverTick{ SW_TRUE }
		, _reservedFlags{ 0 }
		, _listSubTick{}
	{
		initialize();
	}

	Component::Component( Component&& other ) noexcept
		: _pOwner{ std::exchange( other._pOwner, nullptr ) }
		, _componentId{ other._componentId }
		, _componentName{ std::move( other._componentName ) }
		, _subTickActiveMask{ other._subTickActiveMask.load( std::memory_order_relaxed ) }
		, _bActive{ other._bActive.load( std::memory_order_relaxed ) }
		, _bIsPendingKill{ other._bIsPendingKill.load( std::memory_order_relaxed ) }
		, _tickGroup{ other._tickGroup }
		, _bCanEverTick{ other._bCanEverTick }
		, _reservedFlags{ other._reservedFlags }
		, _listSubTick{ std::move( other._listSubTick ) }
	{
	}

	Component& Component::operator=( Component&& other ) noexcept
	{
		if ( this != &other )
		{
			_pOwner		   = std::exchange( other._pOwner, nullptr );
			_componentId   = other._componentId;
			_componentName = std::move( other._componentName );
			_subTickActiveMask.store( other._subTickActiveMask.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			_tickGroup = other._tickGroup;
			_bActive.store( other._bActive.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			_bCanEverTick  = other._bCanEverTick;
			_reservedFlags = other._reservedFlags;
			_bIsPendingKill.store( other._bIsPendingKill.load( std::memory_order_relaxed ), std::memory_order_relaxed );
			_listSubTick = std::move( other._listSubTick );
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

	void Component::onSubTick( uint32 subTickId, float32 deltaTime )
	{
		(void)subTickId;
		(void)deltaTime;
	}

	SubTickHandle Component::registerSubTick( TickGroup group, uint32 subTickId, TickPhase phase, uint8 priority )
	{
		if ( subTickId == 0 )
			return {};

		if ( subTickId < 64 )
			_subTickActiveMask.fetch_or( 1ULL << subTickId, std::memory_order_relaxed );

		for ( SubTickInfo& info : _listSubTick )
		{
			if ( info._subTickId == subTickId )
			{
				info._group	   = group;
				info._phase	   = phase;
				info._priority = priority;
				info._bActive  = SW_TRUE;
				if ( _pOwner != nullptr )
					_pOwner->markTickOrderDirty();
				return SubTickHandle{ _componentId, subTickId };
			}
		}

		SubTickInfo newInfo{};
		newInfo._subTickId = subTickId;
		newInfo._group	   = group;
		newInfo._phase	   = phase;
		newInfo._priority  = priority;
		newInfo._bActive   = SW_TRUE;
		_listSubTick.push_back( std::move( newInfo ) );

		if ( _pOwner != nullptr )
			_pOwner->markTickOrderDirty();

		return SubTickHandle{ _componentId, subTickId };
	}

	bool Component::unregisterSubTick( uint32 subTickId )
	{
		if ( subTickId < 64 )
			_subTickActiveMask.fetch_and( ~( 1ULL << subTickId ), std::memory_order_relaxed );

		for ( size_t index = 0; index < _listSubTick.size(); ++index )
		{
			if ( _listSubTick[index]._subTickId == subTickId )
			{
				_listSubTick.erase( _listSubTick.begin() + index );
				if ( _pOwner != nullptr )
					_pOwner->markTickOrderDirty();
				return true;
			}
		}
		return false;
	}

	bool Component::addSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle )
	{
		if ( subTickId == 0 || prerequisiteHandle.isValid() == false )
			return false;

		// 자기 자신을 종속성으로 추가하는 것 방지
		if ( prerequisiteHandle._componentId == _componentId && prerequisiteHandle._subTickId == subTickId )
			return false;

		for ( SubTickInfo& info : _listSubTick )
		{
			if ( info._subTickId == subTickId )
			{
				for ( const SubTickHandle& existing : info._listPrerequisite )
				{
					if ( existing == prerequisiteHandle )
						return true;
				}
				info._listPrerequisite.push_back( prerequisiteHandle );
				if ( _pOwner != nullptr )
					_pOwner->markTickOrderDirty();
				return true;
			}
		}
		return false;
	}

	void Component::setSubTickActive( uint32 subTickId, bool bActive )
	{
		if ( subTickId == 0 )
			return;

		if ( subTickId < 64 )
		{
			if ( bActive )
				_subTickActiveMask.fetch_or( 1ULL << subTickId, std::memory_order_release );
			else
				_subTickActiveMask.fetch_and( ~( 1ULL << subTickId ), std::memory_order_release );
		}

		for ( SubTickInfo& info : _listSubTick )
		{
			if ( info._subTickId == subTickId )
			{
				info._bActive = bActive ? SW_TRUE : SW_FALSE;
				if ( _pOwner != nullptr )
					_pOwner->markTickOrderDirty();
				break;
			}
		}
	}

	bool Component::isSubTickActiveSlow( uint32 subTickId ) const
	{
		for ( const SubTickInfo& info : _listSubTick )
		{
			if ( info._subTickId == subTickId )
				return info._bActive == SW_TRUE;
		}
		return false;
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
