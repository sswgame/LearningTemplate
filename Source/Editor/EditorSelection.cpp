/**
 * @file EditorSelection.cpp
 */
#include "EditorSelection.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Reflection/ReflectionCore.h"

namespace sw::editor
{
	namespace
	{
		std::string componentTypeBaseName( const Component* comp )
		{
			if ( comp == nullptr )
				return "Component";

			if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
			{
				if ( typeInfo->_fullyQualifiedName.empty() == false )
					return typeInfo->_fullyQualifiedName.c_str();
			}

			if ( comp->getComponentName().empty() == false )
				return comp->getComponentName().c_str();

			return "Component";
		}

		std::string componentBaseKey( const Component* comp )
		{
			if ( comp != nullptr && comp->getComponentName().empty() == false )
				return comp->getComponentName().c_str();
			return componentTypeBaseName( comp );
		}

		std::string makeStableComponentKey( const Component* comp, int32 occurrenceIndex )
		{
			std::string key = componentBaseKey( comp );
			key += '#';
			key += std::to_string( occurrenceIndex );
			return key;
		}

		std::string computeStableComponentKey( const GameObject* gameObject, const Component* target )
		{
			if ( gameObject == nullptr || target == nullptr )
				return {};

			std::unordered_map<std::string, int32> occurrence;
			for ( Component* comp : gameObject->getAllComponents() )
			{
				if ( comp == nullptr )
					continue;

				const std::string base = componentBaseKey( comp );
				const int32		  occ  = occurrence[base]++;
				if ( comp == target )
					return makeStableComponentKey( comp, occ );
			}
			return {};
		}

		Component* findComponentByStableKey( GameObject* gameObject, const std::string& key )
		{
			if ( gameObject == nullptr || key.empty() )
				return nullptr;

			std::unordered_map<std::string, int32> occurrence;
			for ( Component* comp : gameObject->getAllComponents() )
			{
				if ( comp == nullptr )
					continue;

				const std::string base	   = componentBaseKey( comp );
				const int32		  occ	   = occurrence[base]++;
				const std::string stableKey = makeStableComponentKey( comp, occ );
				if ( stableKey == key )
					return comp;
			}
			return nullptr;
		}
	} // namespace

	uint64& selectedObjectId()
	{
		static uint64 s_id = 0;
		return s_id;
	}

	uint64& selectedComponentId()
	{
		static uint64 s_id = 0;
		return s_id;
	}

	std::string& selectedObjectName()
	{
		static std::string s_name;
		return s_name;
	}

	std::string& selectedComponentKey()
	{
		static std::string s_key;
		return s_key;
	}

	void clearSelection()
	{
		selectedObjectId()	  = 0;
		selectedComponentId() = 0;
		selectedObjectName().clear();
		selectedComponentKey().clear();
	}

	void selectGameObject( GameObject* obj )
	{
		if ( obj == nullptr )
			return;
		selectedObjectId()	  = obj->getObjectId();
		selectedComponentId() = 0;
		selectedObjectName()	  = obj->getName().c_str();
		selectedComponentKey().clear();
	}

	void selectComponent( GameObject* obj, Component* comp )
	{
		if ( obj == nullptr || comp == nullptr )
			return;
		selectedObjectId()		= obj->getObjectId();
		selectedComponentId()	= comp->getComponentId();
		selectedObjectName()		= obj->getName().c_str();
		selectedComponentKey()	= computeStableComponentKey( obj, comp );
	}

	void remapSelectionByObjectName( GameObjectManager* manager )
	{
		if ( manager == nullptr )
			return;

		const std::string& name = selectedObjectName();
		if ( name.empty() )
		{
			// Component instances are rebuilt on restore — drop stale component selection.
			selectedComponentId() = 0;
			selectedComponentKey().clear();
			return;
		}

		GameObject* obj = manager->findGameObjectByName( hashed_string( name.c_str() ) );
		if ( obj == nullptr )
		{
			clearSelection();
			return;
		}

		selectedObjectId() = obj->getObjectId();

		const std::string& key = selectedComponentKey();
		if ( key.empty() )
		{
			selectedComponentId() = 0;
			return;
		}

		if ( Component* rematerialized = findComponentByStableKey( obj, key ) )
			selectedComponentId() = rematerialized->getComponentId();
		else
			selectedComponentId() = 0;
	}
} // namespace sw::editor
