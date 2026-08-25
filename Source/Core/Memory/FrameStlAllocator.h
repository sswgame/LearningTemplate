#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/FrameArenaAllocator.h"

#include "RuntimeAPI/PluginAPI.h"

namespace sw
{
	template <typename T>
	class FrameStlAllocator
	{
	public:
		using value_type	  = T;
		using size_type		  = std::size_t;
		using difference_type = std::ptrdiff_t;

		FrameStlAllocator() noexcept = default;
		template <typename U>
		FrameStlAllocator( const FrameStlAllocator<U>& ) noexcept {}

		T* allocate( size_t n )
		{
			if ( n == 0 )
				return nullptr;
			return static_cast<T*>( ::sw::getFrameDoubleBuffer().allocate( n * sizeof( T ), alignof( T ) ) );
		}

		void deallocate( T* p, size_t n ) noexcept
		{
			// FrameDoubleBuffer doesn't free individual blocks
			(void)p;
			(void)n;
		}

		template <typename U>
		struct rebind
		{
			using other = FrameStlAllocator<U>;
		};
	};

	template <typename T, typename U>
	inline bool operator==( const FrameStlAllocator<T>&, const FrameStlAllocator<U>& ) noexcept
	{
		return true;
	}

	template <typename T, typename U>
	inline bool operator!=( const FrameStlAllocator<T>&, const FrameStlAllocator<U>& ) noexcept
	{
		return false;
	}
} // namespace sw
