/**
 * @file InspectorPropertyUndo.h
 * @brief 인스펙터 프로퍼티 편집 Undo (ImGui 활성화/해제 기준)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
	void trackPodPropertyUndo( void* pData, size_t size, const utf8* pLabel );
	void trackStringPropertyUndo( string* pPtr, const utf8* pLabel );
} // namespace sw::editor
