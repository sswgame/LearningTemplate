/**
 * @file EditorSelection.cpp
 */
#include "EditorSelection.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"

namespace sw::editor
{
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

	void clearSelection()
	{
		selectedObjectId()	  = 0;
		selectedComponentId() = 0;
		selectedObjectName().clear();
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
			return;
		}

		GameObject* obj = manager->findGameObjectByName( hashed_string( name.c_str() ) );
		if ( obj == nullptr )
		{
			clearSelection();
			return;
		}

		selectedObjectId()	  = obj->getObjectId();
		selectedComponentId() = 0; // component ids are not stable across play restore
	}
} // namespace sw::editor
