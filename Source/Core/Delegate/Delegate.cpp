#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Core/CoreMinimal.h"

namespace sw
{
	SW_API uint64 allocateDelegateHandleId()
	{
		static std::atomic<uint64> s_nextHandleId{ 1 };
		return s_nextHandleId.fetch_add( 1, std::memory_order_relaxed );
	}
} // namespace sw
