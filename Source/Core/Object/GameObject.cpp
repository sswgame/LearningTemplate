/**
 * @file GameObject.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "ComponentManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Game/Scene/SceneManager.h"
namespace sw
{
	uint64 GameObject::_s_nextObjectId = 1;

	GameObject::GameObject()
		: _objectId( _s_nextObjectId++ )
		, _name( "GameObject" )
		, _bActive( 1 )
		, _bIsActiveInHierarchy( 1 )
		, _bIsTickOrderDirty( 1 )
	{
	}

	GameObject::GameObject( hashed_string name )
		: _objectId( _s_nextObjectId++ )
		, _name( name )
		, _bActive( 1 )
		, _bIsActiveInHierarchy( 1 )
		, _bIsTickOrderDirty( 1 )
	{
	}

	GameObject::~GameObject()
	{
		clearComponents();
	}

	const TypeInfo* GameObject::getTypeInfo() const
	{
		return sw::getTypeRegistry().findType( hashed_string( "sw::GameObject" ) );
	}

	void GameObject::beginPlay()
	{
		for ( Component* comp : _flatComponents )
		{
			if ( comp != nullptr && comp->isActive() )
			{
				comp->onBeginPlay();
			}
		}
	}

	void GameObject::tick( float32 deltaTime )
	{

		if ( _bActive == 0 || _bIsActiveInHierarchy == 0 )
			return;

		if ( _bIsTickOrderDirty != 0 )
		{
			std::sort( _flatComponents.begin(), _flatComponents.end(), []( const Component* a, const Component* b )
			{
				if ( a == nullptr || b == nullptr )
					return false;
				return static_cast<uint8>( a->getTickGroup() ) < static_cast<uint8>( b->getTickGroup() );
			} );
			_bIsTickOrderDirty = 0;
		}

		for ( Component* comp : _flatComponents )
		{
			if ( comp != nullptr && comp->isActive() )
			{
				comp->onTick( deltaTime );
			}
		}
	}

	void GameObject::destroyDeferred()
	{
		if ( auto scene = sw::getSceneManager().getActiveScene() )
		{
			if ( auto objManager = scene->getObjectManager() )
			{
				objManager->destroyObjectDeferred( this );
			}
		}
	}

	void GameObject::onPropertyChanged( hashed_string propertyName )
	{
		(void)propertyName;
	}

	void GameObject::setActive( bool bActive )
	{
		_bActive			  = bActive ? 1 : 0;
		_bIsActiveInHierarchy = bActive ? 1 : 0;

		for ( Component* comp : _flatComponents )
		{
			if ( comp != nullptr )
			{
				comp->setActive( bActive );
			}
		}
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	Component* GameObject::addComponentByName( hashed_string componentTypeName )
	{
		Component* newComp = sw::getComponentManager().createComponentByName( componentTypeName );
		if ( newComp != nullptr )
		{
			newComp->setOwner( this );
			newComp->setComponentName( componentTypeName );

			if ( _flatComponents.capacity() <= _flatComponents.size() )
			{
				_flatComponents.reserve( _flatComponents.size() + 4 );
			}

			_components[componentTypeName].push_back( std::unique_ptr<Component>( newComp ) );
			_flatComponents.push_back( newComp );
			_bIsTickOrderDirty = 1;
			return newComp;
		}
		return nullptr;
	}

	bool GameObject::removeComponent( Component* comp )
	{
		if ( comp == nullptr )
			return false;

		_bIsTickOrderDirty = 1;

		for ( std::vector<Component*>::iterator flatIter = _flatComponents.begin(); flatIter != _flatComponents.end(); ++flatIter )
		{
			if ( *flatIter == comp )
			{
				_flatComponents.erase( flatIter );
				break;
			}
		}

		hashed_string compName = comp->getComponentName();
		auto		  mapIter  = _components.find( compName );
		if ( mapIter != _components.end() )
		{
			std::vector<std::unique_ptr<Component>>& vec = mapIter->second;
			for ( std::vector<std::unique_ptr<Component>>::iterator vecIter = vec.begin(); vecIter != vec.end(); ++vecIter )
			{
				if ( vecIter->get() == comp )
				{
					comp->onDestroy();
					std::swap( *vecIter, vec.back() );
					vec.pop_back();
					return true;
				}
			}
		}

		for ( std::unordered_map<hashed_string, std::vector<std::unique_ptr<Component>>>::iterator mIter = _components.begin(); mIter != _components.end(); ++mIter )
		{
			std::vector<std::unique_ptr<Component>>& vec = mIter->second;
			for ( std::vector<std::unique_ptr<Component>>::iterator vIter = vec.begin(); vIter != vec.end(); ++vIter )
			{
				if ( vIter->get() == comp )
				{
					comp->onDestroy();
					std::swap( *vIter, vec.back() );
					vec.pop_back();
					return true;
				}
			}
		}
		return false;
	}

	uint32 GameObject::getComponentCount() const
	{
		return static_cast<uint32>( _flatComponents.size() );
	}

	void GameObject::clearComponents()
	{
		for ( Component* comp : _flatComponents )
		{
			if ( comp != nullptr )
			{
				comp->onDestroy();
			}
		}
		_flatComponents.clear();
		_components.clear();
	}
}
