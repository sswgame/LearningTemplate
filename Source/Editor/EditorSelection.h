#pragma once
/**
 * @file EditorSelection.h
 * @brief Outliner ↔ Inspector 공유 선택 상태 (EditorModule 내부)
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
	class Component;
}

namespace sw::editor
{
	uint64&		 selectedObjectId();
	uint64&		 selectedComponentId();
	std::string& selectedObjectName();
	/** @brief Stable component key (name|typeName + '#' + occurrence) for Play→Stop rematerialize. */
	std::string& selectedComponentKey();
	void		 clearSelection();

	/** @brief Select a GameObject (clears component selection / key). */
	void selectGameObject( GameObject* obj );
	/** @brief Select a component and cache its stable key for Stop rematerialize. */
	void selectComponent( GameObject* obj, Component* comp );

	/**
	 * @brief After Play Stop restore, remap selectedObjectId by cached GameObject name
	 *        and selectedComponentId by cached stable component key when possible.
	 */
	void remapSelectionByObjectName( GameObjectManager* manager );
} // namespace sw::editor
