#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Core/CoreMinimal.h"

namespace sw
{
	DelegateHandle DelegateHandle::allocate()
	{
		static std::atomic<uint64> s_nextHandleId{ 1 };
		DelegateHandle			   handle{};
		handle._id = s_nextHandleId.fetch_add( 1, std::memory_order_relaxed );
		return handle;
	}
} // namespace sw
