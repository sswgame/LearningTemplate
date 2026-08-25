/**
 * @file RHIHandleTable.h
 * @brief index|generation 불투명 핸들 테이블. COM/Vk 포인터를 uint64로 캐스팅하지 않습니다.
 */
#pragma once
#include "Core/Container/HandleTable.h"
#include "Core/Container/ObjectHandle.h"

namespace sw
{
	/**
	 * @brief T 슬롯을 generation과 함께 보관합니다. 핸들 0은 무효입니다.
	 * @details RHI 백엔드는 기존 uint64 핸들 별칭을 유지하므로 packed 값을 주고받습니다.
	 */
	template <typename T>
	class RHIHandleTable
	{
	public:
		/** @brief 값을 넣고 불투명 핸들을 반환합니다. */
		uint64 insert( T value ) { return _table.insert( std::move( value ) ).packed(); }

		/** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. */
		T* get( uint64 handle ) { return _table.get( ObjectHandle::fromPacked( handle ) ); }

		/** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. */
		const T* get( uint64 handle ) const { return _table.get( ObjectHandle::fromPacked( handle ) ); }

		/** @brief 슬롯을 비우고 generation을 올립니다. 꺼낸 값을 반환합니다. */
		bool take( uint64 handle, T& outValue ) { return _table.take( ObjectHandle::fromPacked( handle ), outValue ); }

		/** @brief 핸들을 무효화합니다. */
		void erase( uint64 handle ) { _table.erase( ObjectHandle::fromPacked( handle ) ); }

		/** @brief 모든 점유 슬롯에 fn(T&)를 호출합니다. */
		template <typename Fn>
		void forEach( Fn&& fn ) { _table.forEach( std::forward<Fn>( fn ) ); }

		/** @brief 모든 슬롯을 비웁니다. */
		void clear() { _table.clear(); }

	private:
		HandleTable<T> _table;
	};
} // namespace sw
