#pragma once
/**
 * @file EditorSelection.h
 * @brief Outliner ↔ Inspector 공유 선택 상태 (EditorModule 내부)
 */

#include "Core/Common/Types.h"
#include <string>

namespace sw
{
	class GameObjectManager;
}

namespace sw::editor
{
	uint64&		 selectedObjectId();
	uint64&		 selectedComponentId();
	std::string& selectedObjectName();
	void		 clearSelection();

	/** @brief After Play Stop restore, remap selectedObjectId by cached GameObject name if id changed. */
	void remapSelectionByObjectName( GameObjectManager* manager );
} // namespace sw::editor
