#include "pch.h"

#include "Core/CoreMinimal.h"

namespace sw
{
	namespace
	{
		hashed_string::AllocationInfo*	s_pInstance		= nullptr;
		hashed_wstring::AllocationInfo* s_pInstanceWide = nullptr;
	} // namespace

	template <>
	hashed_string::AllocationInfo& hashed_string::getAllocationInfo() noexcept
	{
		return *s_pInstance;
	}

	template <>
	hashed_wstring::AllocationInfo& hashed_wstring::getAllocationInfo() noexcept
	{
		return *s_pInstanceWide;
	}

	void HashedStringPool::initialize() noexcept
	{
		SW_ASSERT( s_pInstance == nullptr && s_pInstanceWide == nullptr );
		static hashed_string::AllocationInfo  s_instance;
		static hashed_wstring::AllocationInfo s_instanceWide;
		s_pInstance		= &s_instance;
		s_pInstanceWide = &s_instanceWide;
	}

	void HashedStringPool::shutdown() noexcept
	{
		SW_ASSERT( s_pInstance != nullptr && s_pInstanceWide != nullptr );
		s_pInstance->clear();
		s_pInstanceWide->clear();
		s_pInstance		= nullptr;
		s_pInstanceWide = nullptr;
	}
} // namespace sw
