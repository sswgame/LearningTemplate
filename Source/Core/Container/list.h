/**
 * @file list.h
 * @brief std::list 래퍼. 디버그에서 RaceDetectContext 로 동시 접근을 잡습니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/list.h"
#include "Core/Memory/Memory.h"

#include <list>
#include <memory>

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename T, typename Allocator = std::allocator<T>>
	using list = std::list<T, Allocator>;
#else
	/** @brief std::list + 디버그 레이스 탐지. API 는 STL 과 같습니다. */
	template <typename T, typename Allocator = Allocator<T>>
	class list : public std::list<T, Allocator>
	{
		using Base = std::list<T, Allocator>;
		mutable RaceDetectContext _raceCtx{};

	public:
		using value_type			 = typename Base::value_type;
		using allocator_type		 = typename Base::allocator_type;
		using size_type				 = typename Base::size_type;
		using difference_type		 = typename Base::difference_type;
		using reference				 = typename Base::reference;
		using const_reference		 = typename Base::const_reference;
		using pointer				 = typename Base::pointer;
		using const_pointer			 = typename Base::const_pointer;
		using iterator				 = typename Base::iterator;
		using const_iterator		 = typename Base::const_iterator;
		using reverse_iterator		 = typename Base::reverse_iterator;
		using const_reverse_iterator = typename Base::const_reverse_iterator;

		// ------------------------------------------------------------------------------
		// 1) 생성 · 대입 — 내용은 Base 에 두고, 레이스 컨텍스트는 이 인스턴스 것
		// ------------------------------------------------------------------------------
		/** @brief 빈 리스트로 둡니다. */
		list() noexcept( noexcept( Allocator() ) )
			: Base() {}

		/** @brief 지정 할당자로 빈 리스트를 둡니다. */
		explicit list( const Allocator& alloc ) noexcept
			: Base( alloc ) {}

		/** @brief count 개를 value 로 채웁니다. */
		list( size_type count, const T& value, const Allocator& alloc = Allocator() )
			: Base( count, value, alloc ) {}

		/** @brief count 개의 기본 원소를 둡니다. */
		explicit list( size_type count, const Allocator& alloc = Allocator() )
			: Base( count, alloc ) {}

		/** @brief [first, last) 를 복사해 채웁니다. */
		template <class InputIt>
		list( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
			: Base( first, last, alloc ) {}

		/** @brief std::list 내용을 복사합니다. */
		list( const Base& other )
			: Base( other ) {}

		/** @brief 이동 생성합니다. */
		list( Base&& other ) noexcept
			: Base( std::move( other ) ) {}

		/** @brief 복사 생성합니다. */
		list( const list& other )
			: Base( static_cast<const Base&>( other ) ) {}

		/** @brief 복사 생성합니다. */
		list( const list& other, const Allocator& alloc )
			: Base( static_cast<const Base&>( other ), alloc ) {}

		/** @brief 이동 생성합니다. */
		list( list&& other ) noexcept
			: Base( std::move( static_cast<Base&>( other ) ) ) {}

		/** @brief 이동 생성합니다. */
		list( list&& other, const Allocator& alloc )
			: Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

		/** @brief 초기화 리스트로 채웁니다. */
		list( std::initializer_list<T> init, const Allocator& alloc = Allocator() )
			: Base( init, alloc ) {}

		/** @brief 복사 대입합니다. */
		list& operator=( const Base& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( other );
			return *this;
		}

		/** @brief 이동 대입합니다. */
		list& operator=( Base&& other ) noexcept
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( std::move( other ) );
			return *this;
		}

		/** @brief 복사 대입합니다. */
		list& operator=( const list& other )
		{
			if ( this != &other )
			{
				ScopedRaceWrite lockThis( _raceCtx );
				ScopedRaceRead	lockOther( other._raceCtx );
				Base::operator=( static_cast<const Base&>( other ) );
			}
			return *this;
		}

		/** @brief 이동 대입합니다. */
		list& operator=( list&& other ) noexcept( noexcept( std::allocator_traits<Allocator>::is_always_equal::value ) )
		{
			if ( this != &other )
			{
				ScopedRaceWrite lockThis( _raceCtx );
				ScopedRaceWrite lockOther( other._raceCtx );
				Base::operator=( std::move( static_cast<Base&>( other ) ) );
			}
			return *this;
		}

		/** @brief 초기화 리스트로 대입합니다. */
		list& operator=( std::initializer_list<T> ilist )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( ilist );
			return *this;
		}

		/** @brief 내용을 새로 할당합니다. */
		void assign( size_type count, const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::assign( count, value );
		}

		/** @brief 내용을 새로 할당합니다. */
		template <class InputIt>
		void assign( InputIt first, InputIt last )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::assign( first, last );
		}

		/** @brief 내용을 새로 할당합니다. */
		void assign( std::initializer_list<T> ilist )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::assign( ilist );
		}

		// ------------------------------------------------------------------------------
		// 2) 조회 — 원소·이터레이터·크기. 비const 접근도 쓰기 락 (참조 유출)
		// ------------------------------------------------------------------------------
		/** @brief 사용 중인 할당자입니다. */
		allocator_type get_allocator() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::get_allocator();
		}

		// Element access
		/** @brief 첫 원소를 반환합니다. */
		reference front()
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::front();
		}

		/** @brief 첫 원소를 반환합니다. */
		const_reference front() const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::front();
		}

		/** @brief 마지막 원소를 반환합니다. */
		reference back()
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::back();
		}

		/** @brief 마지막 원소를 반환합니다. */
		const_reference back() const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::back();
		}

		// Iterators
		/** @brief 시작 이터레이터를 반환합니다. */
		iterator begin() noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::begin();
		}

		/** @brief 시작 이터레이터를 반환합니다. */
		const_iterator begin() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::begin();
		}

		/** @brief 상수 시작 이터레이터를 반환합니다. */
		const_iterator cbegin() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::cbegin();
		}

		/** @brief 끝 이터레이터를 반환합니다. */
		iterator end() noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::end();
		}

		/** @brief 끝 이터레이터를 반환합니다. */
		const_iterator end() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::end();
		}

		/** @brief 상수 끝 이터레이터를 반환합니다. */
		const_iterator cend() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::cend();
		}

		/** @brief 역방향 시작 이터레이터를 반환합니다. */
		reverse_iterator rbegin() noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::rbegin();
		}

		/** @brief 역방향 시작 이터레이터를 반환합니다. */
		const_reverse_iterator rbegin() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::rbegin();
		}

		/** @brief 상수 역방향 시작 이터레이터를 반환합니다. */
		const_reverse_iterator crbegin() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::crbegin();
		}

		/** @brief 역방향 끝 이터레이터를 반환합니다. */
		reverse_iterator rend() noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::rend();
		}

		/** @brief 역방향 끝 이터레이터를 반환합니다. */
		const_reverse_iterator rend() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::rend();
		}

		/** @brief 상수 역방향 끝 이터레이터를 반환합니다. */
		const_reverse_iterator crend() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::crend();
		}

		// Capacity
		/** @brief 비어 있는지 반환합니다. */
		bool empty() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::empty();
		}

		/** @brief 원소 개수를 반환합니다. */
		size_type size() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::size();
		}

		/** @brief 담을 수 있는 최대 원소 개수를 반환합니다. */
		size_type max_size() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::max_size();
		}

		// Modifiers
		// ------------------------------------------------------------------------------
		// 3) 변경 — insert/erase/splice. 쓰기 락
		// ------------------------------------------------------------------------------
		/** @brief 모든 원소를 제거합니다. */
		void clear() noexcept
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::clear();
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, value );
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, T&& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, std::move( value ) );
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, size_type count, const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, count, value );
		}

		/** @brief 원소를 삽입합니다. */
		template <class InputIt>
		iterator insert( const_iterator pos, InputIt first, InputIt last )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, first, last );
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, std::initializer_list<T> ilist )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, ilist );
		}

		/** @brief 원소를 제자리 생성합니다. */
		template <class... Args>
		iterator emplace( const_iterator pos, Args&&... args )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::emplace( pos, std::forward<Args>( args )... );
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( const_iterator pos )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::erase( pos );
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( const_iterator first, const_iterator last )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::erase( first, last );
		}

		/** @brief 뒤에 원소를 추가합니다. */
		void push_back( const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::push_back( value );
		}

		/** @brief 뒤에 원소를 추가합니다. */
		void push_back( T&& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::push_back( std::move( value ) );
		}

		/** @brief 뒤에 원소를 제자리 생성합니다. */
		template <class... Args>
		reference emplace_back( Args&&... args )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::emplace_back( std::forward<Args>( args )... );
		}

		/** @brief 마지막 원소를 제거합니다. */
		void pop_back()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::pop_back();
		}

		/** @brief 앞에 원소를 추가합니다. */
		void push_front( const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::push_front( value );
		}

		/** @brief 앞에 원소를 추가합니다. */
		void push_front( T&& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::push_front( std::move( value ) );
		}

		/** @brief 앞에 원소를 제자리 생성합니다. */
		template <class... Args>
		reference emplace_front( Args&&... args )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::emplace_front( std::forward<Args>( args )... );
		}

		/** @brief 첫 원소를 제거합니다. */
		void pop_front()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::pop_front();
		}

		/** @brief 크기를 변경합니다. */
		void resize( size_type count )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::resize( count );
		}

		/** @brief 크기를 변경합니다. */
		void resize( size_type count, const value_type& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::resize( count, value );
		}

		/** @brief 내용을 교환합니다. */
		void swap( list& other ) noexcept( noexcept( std::allocator_traits<Allocator>::is_always_equal::value ) )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::swap( static_cast<Base&>( other ) );
		}

		// Operations
		/** @brief 다른 컨테이너의 원소를 병합합니다. */
		void merge( list& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::merge( static_cast<Base&>( other ) );
		}

		/** @brief 다른 컨테이너의 원소를 병합합니다. */
		void merge( list&& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::merge( std::move( static_cast<Base&>( other ) ) );
		}

		/** @brief 다른 컨테이너의 원소를 병합합니다. */
		template <class Compare>
		void merge( list& other, Compare comp )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::merge( static_cast<Base&>( other ), comp );
		}

		/** @brief 다른 컨테이너의 원소를 병합합니다. */
		template <class Compare>
		void merge( list&& other, Compare comp )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::merge( std::move( static_cast<Base&>( other ) ), comp );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, static_cast<Base&>( other ) );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list&& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, std::move( static_cast<Base&>( other ) ) );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list& other, const_iterator it )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, static_cast<Base&>( other ), it );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list&& other, const_iterator it )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, std::move( static_cast<Base&>( other ) ), it );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list& other, const_iterator first, const_iterator last )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, static_cast<Base&>( other ), first, last );
		}

		/** @brief 다른 리스트의 원소를 이어 붙입니다. */
		void splice( const_iterator pos, list&& other, const_iterator first, const_iterator last )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			ScopedRaceWrite lockOther( other._raceCtx );
			Base::splice( pos, std::move( static_cast<Base&>( other ) ), first, last );
		}

		/** @brief 값과 일치하는 원소를 제거합니다. */
		void remove( const T& value )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::remove( value );
		}

		/** @brief 조건에 맞는 원소를 제거합니다. */
		template <class UnaryPredicate>
		void remove_if( UnaryPredicate p )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::remove_if( p );
		}

		/** @brief 순서를 뒤집습니다. */
		void reverse() noexcept
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::reverse();
		}

		/** @brief 연속 중복 원소를 제거합니다. */
		void unique()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::unique();
		}

		/** @brief 연속 중복 원소를 제거합니다. */
		template <class BinaryPredicate>
		void unique( BinaryPredicate p )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::unique( p );
		}

		/** @brief 정렬합니다. */
		void sort()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::sort();
		}

		/** @brief 정렬합니다. */
		template <class Compare>
		void sort( Compare comp )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::sort( comp );
		}
	};
#endif
} // namespace sw
