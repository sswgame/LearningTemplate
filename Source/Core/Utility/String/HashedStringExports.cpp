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
		static hashed_string::AllocationInfo s_instance;
		return &s_instance;
	}

	SW_API hashed_wstring::AllocationInfo* getCoreHashedWStringAllocationInfo() noexcept
	{
		static hashed_wstring::AllocationInfo s_instance;
		return &s_instance;
	}

	SW_API void shutdownHashedStringPools() noexcept
	{
		// Touch both pools first so a late first-use cannot allocate after App teardown.
		getCoreHashedStringAllocationInfo()->clear();
		getCoreHashedWStringAllocationInfo()->clear();
	}
} // namespace sw
