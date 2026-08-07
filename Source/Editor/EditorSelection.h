#pragma once
/**
 * @file EditorSelection.h
 * @brief Outliner ↔ Inspector 공유 선택 상태 (EditorModule 내부)
 */

#include "Core/Common/Types.h"

namespace sw::editor
{
	uint64& selectedObjectId();
	uint64& selectedComponentId();
	void	clearSelection();
} // namespace sw::editor
