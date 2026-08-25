/**
 * @file InlineAllocator.h
 * @brief SBO(Small Buffer Optimization)를 위한 인라인 스택 버퍼 할당자
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include <memory>
#include <new>
#include <type_traits>

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	// STL 컨테이너 모드에서는 std::vector의 이동(Move) 버그를 막기 위해 일반 할당자로 강등(Fallback)합니다.
	template <typename T, size_t N>
	using InlineAllocator = std::allocator<T>;
#else
	/**
	 * @brief 내부 스택 버퍼를 가지는 할당자
	 * @details sw::vector 내부에서 감지하여 SBO를 활성화하는 데 사용됩니다.
	 */
	template <typename T, size_t N>
	struct InlineAllocator
	{
		using value_type = T;

		alignas( T ) uint8 _buffer[N * sizeof( T )];
		bool _isUsed = false;

		InlineAllocator()										 = default;
		InlineAllocator( const InlineAllocator& )				 = default;
		InlineAllocator( InlineAllocator&& ) noexcept			 = default;
		InlineAllocator& operator=( const InlineAllocator& )	 = default;
		InlineAllocator& operator=( InlineAllocator&& ) noexcept = default;

		template <class U>
		struct rebind
		{
			using other = InlineAllocator<U, N>;
		};

		T* allocate( size_t n )
		{
			if ( _isUsed == false && n <= N )
			{
				_isUsed = true;
				return reinterpret_cast<T*>( _buffer );
			}
			return sw::Allocator<T>().allocate( n );
		}

		void deallocate( T* p, size_t n ) noexcept
		{
			if ( p == reinterpret_cast<T*>( _buffer ) )
			{
				_isUsed = false;
			}
			else
			{
				sw::Allocator<T>().deallocate( p, n );
			}
		}

		// sw::vector 가 SBO 여부를 감지할 수 있도록 버퍼 포인터 제공
		T*	   get_inline_buffer() { return reinterpret_cast<T*>( _buffer ); }
		size_t get_inline_capacity() const { return N; }
	};
#endif
} // namespace sw
