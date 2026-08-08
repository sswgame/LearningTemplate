#pragma once
/**
 * @file EditorWorkspace.h
 * @brief Shared editor selection / asset focus / gizmo / open-window hub
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
	enum class InspectMode : uint8
	{
		GameObject = 0,
		Asset
	};

	uint64&		 selectedObjectId();
	uint64&		 selectedComponentId();
	std::string& selectedObjectName();
	/** @brief Stable component key for Play->Stop rematerialize. */
	std::string& selectedComponentKey();
	void		 clearSelection();

	void selectGameObject( GameObject* obj );
	void selectComponent( GameObject* obj, Component* comp );
	void remapSelectionByObjectName( GameObjectManager* manager );

	std::string& focusedAssetPath();
	void		 setFocusedAssetPath( const char* path );

	InspectMode& inspectMode();
	void		 setInspectMode( InspectMode mode );

	/** @brief 0=Translate 1=Rotate 2=Scale */
	int32& gizmoOperation();
	bool&  gizmoLocalSpace();

	std::string& pendingOpenWindowTitle();
	void		 requestOpenWindow( const char* title );
	bool		 consumeOpenWindow( std::string& outTitle );

	uint64& scrollToComponentId();
	uint64& scrollToObjectId();

	bool& boneHierarchyPopupOpen();
} // namespace sw::editor
