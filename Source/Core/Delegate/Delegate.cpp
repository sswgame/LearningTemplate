#include "pch.h"

#include "Core/CoreMinimal.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
	SW_API uint64 allocateDelegateHandleId()
	{
		static std::atomic<uint64> s_nextHandleId{ 1 };
		return s_nextHandleId.fetch_add( 1, std::memory_order_relaxed );
	}
} // namespace sw
