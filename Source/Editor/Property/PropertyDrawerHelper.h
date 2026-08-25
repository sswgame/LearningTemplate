#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	void trackPodPropertyUndo( void* pData, size_t size, const utf8* pLabel );
	void trackStringPropertyUndo( string* pPtr, const utf8* pLabel );
} // namespace sw
