/**
 * @file HashedStringExports.cpp
 * @brief Core.dll 단독 hashed_string AllocationInfo 인스턴스
 */
#include "pch.h"
#include "Core/Utility/String/hashed_string.h"

namespace sw
{
	SW_API hashed_string::AllocationInfo* getCoreHashedStringAllocationInfo() noexcept
	{
		static hashed_string::AllocationInfo instance;
		return &instance;
	}

	SW_API hashed_wstring::AllocationInfo* getCoreHashedWStringAllocationInfo() noexcept
	{
		static hashed_wstring::AllocationInfo instance;
		return &instance;
	}
}
