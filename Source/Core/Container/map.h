/**
 * @file map.h
 * @brief 정렬 맵. 기본은 벡터 이진 검색, SW_ENABLE_STL_CONTAINER 이면 std::map 래퍼.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"

// 이 매크로를 정의하면 표준 std::map 구현으로 되돌립니다.
// #define SW_ENABLE_STL_CONTAINER

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename Key, typename T, typename Compare = std::less<Key>, typename Allocator = std::allocator<pair<const Key, T>>>
	using map = std::map<Key, T, Compare, Allocator>;
#else
	/** @brief 정렬된 벡터 맵. 조회는 이진 검색, 삽입은 정렬 유지. */
	template <typename Key, typename T, typename Compare = std::less<void>, typename Allocator = Allocator<pair<Key, T>>>
	class map
	{
	public:
		using key_type				 = Key;
		using mapped_type			 = T;
		using value_type			 = pair<Key, T>;
		using size_type				 = size_t;
		using difference_type		 = std::ptrdiff_t;
		using key_compare			 = Compare;
		using allocator_type		 = Allocator;
		using reference				 = value_type&;
		using const_reference		 = const value_type&;
		using pointer				 = value_type*;
		using const_pointer			 = const value_type*;
		using Container				 = std::vector<value_type, Allocator>;
		using iterator				 = typename Container::iterator;
		using const_iterator		 = typename Container::const_iterator;
		using reverse_iterator		 = typename Container::reverse_iterator;
		using const_reverse_iterator = typename Container::const_reverse_iterator;

	private:
		/** @brief 키만 비교해 정렬 위치를 찾습니다. */
		struct KeyCompare
		{
			Compare _comp;
			/** @brief 두 엔트리의 키를 비교합니다. */
			bool operator()( const value_type& a, const value_type& b ) const { return _comp( a.first, b.first ); }
			/** @brief 엔트리 키와 이질 키를 비교합니다. */
			template <typename K>
			bool operator()( const value_type& a, const K& b ) const { return _comp( a.first, b ); }

			/** @brief 이질 키와 엔트리 키를 비교합니다. */
			template <typename K>
			bool operator()( const K& a, const value_type& b ) const { return _comp( a.first, b.first ); }
		};

	public:
		// ------------------------------------------------------------------------------
		// 1) 생성 · 대입 — 정렬 벡터 + 비교자. 레이스 컨텍스트는 공유하지 않음
		// ------------------------------------------------------------------------------
		/** @brief 빈 맵으로 둡니다. */
		map()
			: _data{}
			, _comp{} {}

		/** @brief 비교자와 할당자를 지정합니다. */
		explicit map( const Compare& comp, const Allocator& alloc = Allocator() )
			: _data{ alloc }
			, _comp{ comp } {}

		/** @brief 지정 할당자로 빈 맵을 둡니다. */
		explicit map( const Allocator& alloc )
			: _data{ alloc }
			, _comp{} {}

		/** @brief [first, last) 를 삽입해 정렬합니다. */
		template <class InputIt>
		map( InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
			: _data{ alloc }
			, _comp{ comp } { insert( first, last ); }

		/** @brief 복사 생성합니다. */
		map( const map& other )
			: _data{ other._data }
			, _comp{ other._comp } {}

		/** @brief 이동 생성합니다. */
		map( map&& other ) noexcept
			: _data{ std::move( other._data ) }
			, _comp{ std::move( other._comp ) } {}

		/** @brief 초기화 리스트를 삽입해 정렬합니다. */
		map( std::initializer_list<value_type> init, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
			: _data{ alloc }
			, _comp{ comp } { insert( init.begin(), init.end() ); }

		/** @brief 복사 대입합니다. */
		map& operator=( const map& other )
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_READ_OTHER( other );
				_data = other._data;
				_comp = other._comp;
			}
			return *this;
		}

		/** @brief 이동 대입합니다. */
		map& operator=( map&& other ) noexcept
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_WRITE_OTHER( other );
				_data = std::move( other._data );
				_comp = std::move( other._comp );
			}
			return *this;
		}

		/** @brief 초기화 리스트로 대입합니다. */
		map& operator=( std::initializer_list<value_type> ilist )
		{
			SW_SCOPED_RACE_WRITE();
			clear();
			insert( ilist.begin(), ilist.end() );
			return *this;
		}

		// ------------------------------------------------------------------------------
		// 2) 조회 — 이진 검색 · 이터레이터 · 크기
		// ------------------------------------------------------------------------------
		/** @brief 사용 중인 할당자입니다. */
		allocator_type get_allocator() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.get_allocator();
		}

		/** @brief 범위 검사와 함께 원소를 반환합니다. */
		template <typename K>
		T& at( const K& key )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = lower_bound( key );
			if ( it != end() && _comp( key, it->first ) == false )
				return it->second;
			throw std::out_of_range( "map::at" );
		}

		/** @brief 범위 검사와 함께 원소를 반환합니다. */
		template <typename K>
		const T& at( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			auto it = lower_bound( key );
			if ( it != end() && _comp( key, it->first ) == false )
				return it->second;
			throw std::out_of_range( "map::at" );
		}

		/** @brief 지정 위치의 원소를 반환합니다. */
		T& operator[]( const Key& key )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( key, it->first ) == false )
				return it->second;
			it = _data.insert( it, value_type( key, T() ) );
			return it->second;
		}

		/** @brief 지정 위치의 원소를 반환합니다. */
		T& operator[]( Key&& key )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( key, it->first ) == false )
				return it->second;
			it = _data.insert( it, value_type( std::move( key ), T() ) );
			return it->second;
		}

		/** @brief 시작 이터레이터를 반환합니다. */
		iterator begin() noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.begin();
		}

		const_iterator begin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.begin();
		}

		const_iterator cbegin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.cbegin();
		}

		/** @brief 끝 이터레이터를 반환합니다. */
		iterator end() noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.end();
		}

		const_iterator end() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.end();
		}

		const_iterator cend() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.cend();
		}

		/** @brief 역방향 시작 이터레이터를 반환합니다. */
		reverse_iterator rbegin() noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.rbegin();
		}

		const_reverse_iterator rbegin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.rbegin();
		}

		const_reverse_iterator crbegin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.crbegin();
		}

		/** @brief 역방향 끝 이터레이터를 반환합니다. */
		reverse_iterator rend() noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.rend();
		}

		const_reverse_iterator rend() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.rend();
		}

		const_reverse_iterator crend() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.crend();
		}

		/** @brief 비어 있는지 반환합니다. */
		bool empty() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.empty();
		}

		/** @brief 원소 개수를 반환합니다. */
		size_type size() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.size();
		}

		/** @brief 담을 수 있는 최대 원소 개수를 반환합니다. */
		size_type max_size() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.max_size();
		}

		/** @brief 용량을 예약합니다. */
		void reserve( size_type new_cap )
		{
			SW_SCOPED_RACE_WRITE();
			_data.reserve( new_cap );
		}

		/** @brief 현재 용량을 반환합니다. */
		size_type capacity() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _data.capacity();
		}

		// ------------------------------------------------------------------------------
		// 3) 변경 — 정렬을 유지하며 insert/erase
		// ------------------------------------------------------------------------------
		/** @brief 모든 원소를 제거합니다. */
		void clear() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			_data.clear();
		}

		/** @brief 원소를 삽입합니다. */
		pair<iterator, bool> insert( const value_type& value )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), value.first, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( value.first, it->first ) == false )
				return { it, false };
			it = _data.insert( it, value );
			return { it, true };
		}

		/** @brief 원소를 삽입합니다. */
		pair<iterator, bool> insert( value_type&& value )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), value.first, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( value.first, it->first ) == false )
				return { it, false };
			it = _data.insert( it, std::move( value ) );
			return { it, true };
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( [[maybe_unused]] const_iterator hint, const value_type& value ) { return insert( value ).first; }

		/** @brief 원소를 삽입합니다. */
		iterator insert( [[maybe_unused]] const_iterator hint, value_type&& value ) { return insert( std::move( value ) ).first; }

		/** @brief 원소를 삽입합니다. */
		template <class InputIt>
		void insert( InputIt first, InputIt last )
		{
			SW_SCOPED_RACE_WRITE();
			for ( ; first != last; ++first )
			{
				auto it = std::lower_bound( _data.begin(), _data.end(), first->first, KeyCompare{ _comp } );
				if ( it == _data.end() || _comp( first->first, it->first ) )
					_data.insert( it, *first );
			}
		}

		/** @brief 원소를 삽입합니다. */
		void insert( std::initializer_list<value_type> ilist ) { insert( ilist.begin(), ilist.end() ); }

		/** @brief 원소를 제자리 생성합니다. */
		template <class... Args>
		pair<iterator, bool> emplace( Args&&... args )
		{
			value_type val( std::forward<Args>( args )... );
			return insert( std::move( val ) );
		}

		/** @brief 힌트 위치에 원소를 제자리 생성합니다. */
		template <class... Args>
		iterator emplace_hint( [[maybe_unused]] const_iterator hint, Args&&... args ) { return emplace( std::forward<Args>( args )... ).first; }

		// try_emplace
		/** @brief 키가 없을 때만 제자리 생성합니다. */
		template <class... Args>
		pair<iterator, bool> try_emplace( const Key& k, Args&&... args )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), k, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( k, it->first ) == false )
				return { it, false };
			it = _data.emplace( it, std::piecewise_construct, std::forward_as_tuple( k ), std::forward_as_tuple( std::forward<Args>( args )... ) );
			return { it, true };
		}

		/** @brief 키가 없을 때만 제자리 생성합니다. */
		template <class... Args>
		pair<iterator, bool> try_emplace( Key&& k, Args&&... args )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), k, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( k, it->first ) == false )
				return { it, false };
			it = _data.emplace( it, std::piecewise_construct, std::forward_as_tuple( std::move( k ) ), std::forward_as_tuple( std::forward<Args>( args )... ) );
			return { it, true };
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( iterator pos )
		{
			SW_SCOPED_RACE_WRITE();
			return _data.erase( pos );
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( const_iterator pos )
		{
			SW_SCOPED_RACE_WRITE();
			return _data.erase( pos );
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( const_iterator first, const_iterator last )
		{
			SW_SCOPED_RACE_WRITE();
			return _data.erase( first, last );
		}

		/** @brief 원소를 제거합니다. */
		template <typename K>
		size_type erase( const K& key )
		{
			SW_SCOPED_RACE_WRITE();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( key, it->first ) == false )
			{
				_data.erase( it );
				return 1;
			}
			return 0;
		}

		/** @brief 내용을 교환합니다. */
		void swap( map& other ) noexcept
		{
			SW_SCOPED_RACE_WRITE();
			SW_SCOPED_RACE_WRITE_OTHER( other );
			_data.swap( other._data );
			std::swap( _comp, other._comp );
		}

		/** @brief 키와 일치하는 원소 개수를 반환합니다. */
		template <typename K>
		size_type count( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			if ( it != _data.end() && _comp( key, it->first ) == false )
				return 1;
			return 0;
		}

		/** @brief 키를 찾습니다. */
		template <typename K>
		iterator find( const K& key )
		{
			SW_SCOPED_RACE_READ();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			return ( it != _data.end() && _comp( key, it->first ) == false ) ? it : _data.end();
		}

		/** @brief 키를 찾습니다. */
		template <typename K>
		const_iterator find( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			auto it = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			return ( it != _data.end() && _comp( key, it->first ) == false ) ? it : _data.end();
		}

		/** @brief 키 포함 여부를 반환합니다. */
		template <typename K>
		bool contains( const K& key ) const { return count( key ) > 0; }

		/** @brief 동등 범위를 반환합니다. */
		template <typename K>
		pair<iterator, iterator> equal_range( const K& key )
		{
			SW_SCOPED_RACE_READ();
			auto first = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			auto last  = first;
			if ( first != _data.end() && _comp( key, first->first ) == false )
				++last;
			return { first, last };
		}

		/** @brief 동등 범위를 반환합니다. */
		template <typename K>
		pair<const_iterator, const_iterator> equal_range( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			auto first = std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
			auto last  = first;
			if ( first != _data.end() && _comp( key, first->first ) == false )
				++last;
			return { first, last };
		}

		/** @brief 하한 이터레이터를 반환합니다. */
		template <typename K>
		iterator lower_bound( const K& key )
		{
			SW_SCOPED_RACE_READ();
			return std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
		}

		/** @brief 하한 이터레이터를 반환합니다. */
		template <typename K>
		const_iterator lower_bound( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			return std::lower_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
		}

		/** @brief 상한 이터레이터를 반환합니다. */
		template <typename K>
		iterator upper_bound( const K& key )
		{
			SW_SCOPED_RACE_READ();
			return std::upper_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
		}

		/** @brief 상한 이터레이터를 반환합니다. */
		template <typename K>
		const_iterator upper_bound( const K& key ) const
		{
			SW_SCOPED_RACE_READ();
			return std::upper_bound( _data.begin(), _data.end(), key, KeyCompare{ _comp } );
		}

	private:
		Container _data;
		Compare	  _comp;
		SW_RACE_CTX_MEMBER
	};
#endif
} // namespace sw
