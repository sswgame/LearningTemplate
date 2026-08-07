/**
 * @file EditorSelection.cpp
 */
#include "EditorSelection.h"

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

	void clearSelection()
	{
		selectedObjectId()	  = 0;
		selectedComponentId() = 0;
	}
} // namespace sw::editor
