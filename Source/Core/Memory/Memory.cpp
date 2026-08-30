#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

namespace sw
{
#if !defined( SW_SHIPPING )
	/**
	 * @struct AllocHeader
	 * @brief 할당에 사용된 메모리 블록 직전에 위치하는 메타데이터 헤더
	 */
	struct alignas( 16 ) AllocHeader
	{
		size_t	  _size;	  ///< 요청된 사용자 데이터 바이트 크기
		MemoryTag _tag;		  ///< 메모리 서브시스템 분류 태그 (Graphics, Audio, Physics 등)
		uint32	  _pad;		  ///< 16바이트 경계 정렬 패딩
		uint64	  _hash;	  ///< 할당 시점 콜스택 추적 해시
		uint64	  _magic;	  ///< 유효성 검증용 매직 넘버
		void*	  _pRawPtr;	  ///< OS 원시 할당 시작 주소 (정렬 패딩 이전)
		void*	  _pReserved; ///< 48바이트 (16바이트 배수) 정렬용 패딩
	};

	/** @brief 유효한 엔진 할당 블록임을 식별하기 위한 64비트 매직 상수 */
	static constexpr uint64 kAllocMagic = 0x5C09B10CDA7A0000;
#endif

	/**
	 * @brief 지정한 바이트 경계(Alignment)로 정렬된 메모리 블록을 할당합니다.
	 */
	void* Memory::alignedAlloc( size_t size, size_t alignment )
	{
		const size_t align = MathUtil::max( alignment, sizeof( void* ) );

#if defined( SW_SHIPPING )
	#if defined( SW_PLATFORM_WINDOWS )
		return _aligned_malloc( size, align );
	#else
		void* pRawPtr{ nullptr };
		if ( posix_memalign( &pRawPtr, align, size ) != 0 )
			return nullptr;
		return pRawPtr;
	#endif
#else // SW_SHIPPING

		size_t totalSize = size + sizeof( AllocHeader ) + align;

	#if defined( SW_PLATFORM_WINDOWS )
		void* pRawPtr = _aligned_malloc( totalSize, align );
	#else
		void* pRawPtr{ nullptr };
		if ( posix_memalign( &pRawPtr, align, totalSize ) != 0 )
			pRawPtr = nullptr;
	#endif

		if ( pRawPtr == nullptr )
			return nullptr;

		const uintptr_t rawAddr	 = reinterpret_cast<uintptr_t>( pRawPtr );
		const uintptr_t userAddr = MathUtil::align( rawAddr + sizeof( AllocHeader ), static_cast<uintptr_t>( align ) );
		AllocHeader*	pHeader	 = reinterpret_cast<AllocHeader*>( userAddr - sizeof( AllocHeader ) );
		pHeader->_size			 = size;
		pHeader->_tag			 = MemoryProfiler::getCurrentMemoryTag();
		pHeader->_magic			 = kAllocMagic;
		pHeader->_hash			 = 0;
		pHeader->_pRawPtr		 = pRawPtr;

		void*			userPtr	  = reinterpret_cast<void*>( userAddr );
		MemoryProfiler* pProfiler = MemoryProfiler::getActive();
		if ( pProfiler != nullptr )
			pHeader->_hash = pProfiler->recordAllocation( userPtr, size, pHeader->_tag );

		return userPtr;
#endif // SW_SHIPPING
	}

	/**
	 * @brief 정렬 할당된 메모리 블록을 해제합니다.
	 */
	void Memory::alignedFree( void* pPtr )
	{
		if ( pPtr == nullptr )
			return;

#if defined( SW_SHIPPING )
	#if defined( SW_PLATFORM_WINDOWS )
		_aligned_free( pPtr );
	#else
		free( pPtr );
	#endif
#else // SW_SHIPPING
		AllocHeader* pHeader = reinterpret_cast<AllocHeader*>( static_cast<utf8*>( pPtr ) - sizeof( AllocHeader ) );
		if ( pHeader->_magic != kAllocMagic )
		{
			// 헤더가 손상되었거나 엔진 할당 블록이 아님
			return;
		}

		MemoryProfiler* pProfiler = MemoryProfiler::getActive();
		if ( pProfiler != nullptr )
			pProfiler->recordFree( pPtr, pHeader->_size, pHeader->_tag, pHeader->_hash );

		pHeader->_magic = 0; // 이중 해제 방지
		void* pRawPtr	= pHeader->_pRawPtr;
	#if defined( SW_PLATFORM_WINDOWS )
		_aligned_free( pRawPtr );
	#else
		free( pRawPtr );
	#endif
#endif // SW_SHIPPING
	}

	/**
	 * @brief 일반 동적 메모리를 할당합니다.
	 */
	void* Memory::allocMemory( size_t size )
	{
#if defined( SW_SHIPPING )
		return malloc( size );
#else  // SW_SHIPPING
		size_t totalSize = size + sizeof( AllocHeader );
		void*  pRawPtr	 = malloc( totalSize );
		if ( pRawPtr == nullptr )
			return nullptr;

		void* pUserPtr = static_cast<utf8*>( pRawPtr ) + sizeof( AllocHeader );

		AllocHeader* pHeader = static_cast<AllocHeader*>( pRawPtr );
		pHeader->_size		 = size;
		pHeader->_tag		 = MemoryProfiler::getCurrentMemoryTag();
		pHeader->_magic		 = kAllocMagic;
		pHeader->_hash		 = 0;
		pHeader->_pRawPtr	 = pRawPtr;

		MemoryProfiler* pProfiler = MemoryProfiler::getActive();
		if ( pProfiler != nullptr )
			pHeader->_hash = pProfiler->recordAllocation( pUserPtr, size, pHeader->_tag );

		return pUserPtr;
#endif // SW_SHIPPING
	}

	/**
	 * @brief 동적 메모리를 해제합니다.
	 */
	void Memory::freeMemory( void* pPtr )
	{
		if ( pPtr == nullptr )
			return;

#if defined( SW_SHIPPING )
		free( pPtr );
#else  // SW_SHIPPING
		AllocHeader* pHeader = reinterpret_cast<AllocHeader*>( static_cast<utf8*>( pPtr ) - sizeof( AllocHeader ) );
		if ( pHeader->_magic != kAllocMagic )
		{
			// 헤더가 손상되었거나 엔진 할당 블록이 아님
			return;
		}

		MemoryProfiler* pProfiler = MemoryProfiler::getActive();
		if ( pProfiler != nullptr )
			pProfiler->recordFree( pPtr, pHeader->_size, pHeader->_tag, pHeader->_hash );

		pHeader->_magic = 0;
		free( pHeader->_pRawPtr );
#endif // SW_SHIPPING
	}

	void* Memory::copy( void* pDest, const void* pSrc, size_t size )
	{
		if ( pDest == nullptr || pSrc == nullptr || size == 0 )
			return pDest;
		return std::memcpy( pDest, pSrc, size );
	}

	void* Memory::move( void* pDest, const void* pSrc, size_t size )
	{
		if ( pDest == nullptr || pSrc == nullptr || size == 0 )
			return pDest;
		return std::memmove( pDest, pSrc, size );
	}

	void* Memory::set( void* pDest, uint8 value, size_t size )
	{
		if ( pDest == nullptr || size == 0 )
			return pDest;
		return std::memset( pDest, value, size );
	}

	int32 Memory::compare( const void* pLhs, const void* pRhs, size_t size )
	{
		if ( pLhs == pRhs || size == 0 )
			return 0;
		if ( pLhs == nullptr )
			return -1;
		if ( pRhs == nullptr )
			return 1;
		return std::memcmp( pLhs, pRhs, size );
	}
} // namespace sw
