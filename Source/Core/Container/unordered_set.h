/**
 * @file unordered_set.h
 * @brief 해시셋. SW_USE_DOD_HASHMAP 이면 밀집 버킷, 아니면 std::unordered_set 래퍼.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>, typename Allocator = std::allocator<Key>>
	using unordered_set = std::unordered_set<Key, Hash, KeyEqual, Allocator>;
#else
	/** @brief 밀집 해시셋. erase 시 이터레이터가 무효화될 수 있습니다. */
	template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>, typename Allocator = Allocator<Key>>
	class unordered_set
	{
		SW_RACE_CTX_MEMBER

	public:
		using key_type		  = Key;
		using value_type	  = Key;
		using size_type		  = size_t;
		using difference_type = ptrdiff_t;
		using hasher		  = Hash;
		using key_equal		  = KeyEqual;
		using allocator_type  = Allocator;
		using reference		  = value_type&;
		using const_reference = const value_type&;
		using pointer		  = value_type*;
		using const_pointer	  = const value_type*;

	private:
		/** @brief 키와 버킷 체인 next 인덱스입니다. */
		struct Node
		{
			Key	   _key;
			size_t _next;
		};

		vector<size_t>			_listBucket;
		vector<Node>			_listDenseData;
		pair<hasher, key_equal> _traits;

		const hasher&	 get_hasher() const noexcept { return _traits.first(); }
		const key_equal& get_equal() const noexcept { return _traits.second(); }

		/** @brief 빈 버킷 슬롯 표시 (전비트 1). */
		static constexpr size_t kEmptySlot = static_cast<size_t>( -1 );

		/** @brief 검사합니다. */
		void check_expand()
		{
			if ( _listBucket.empty() || _listDenseData.size() >= _listBucket.size() )
				rehash_internal( _listBucket.empty() ? 16 : _listBucket.size() * 2 );
		}

	public:
		/** @brief 밀집 배열을 순회하는 이터레이터입니다. */
		class iterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type		= value_type;
			using difference_type	= ptrdiff_t;
			using pointer			= const value_type*;
			using reference			= const value_type&;

			/** @brief 생성합니다. */
			iterator( const unordered_set* set, size_t index )
				: _pSet{ set }
				, _index{ index } {}

			/** @brief 증가시킵니다. */
			iterator& operator++()
			{
				++_index;
				return *this;
			}

			/** @brief 같은지 비교합니다. */
			bool operator==( const iterator& other ) const { return _index == other._index; }
			/** @brief 다른지 비교합니다. */
			bool operator!=( const iterator& other ) const { return _index != other._index; }
			/** @brief 역참조합니다. */
			reference operator*() const { return std::as_const( _pSet->_listDenseData ).data()[_index]._key; }
			/** @brief 멤버에 접근합니다. */
			pointer operator->() const { return &std::as_const( _pSet->_listDenseData ).data()[_index]._key; }

			const unordered_set* _pSet;
			size_t				 _index;
		};

		using const_iterator = iterator; // For set, iterator is always const

		// ------------------------------------------------------------------------------
		// 1) 생성 · 대입 — 버킷+밀집 배열. 레이스 컨텍스트는 공유하지 않음
		// ------------------------------------------------------------------------------
		/** @brief 빈 집합으로 둡니다. */
		unordered_set()
			: _listBucket{}
			, _listDenseData{}
			, _traits{} {}

		/** @brief 초기화 리스트를 삽입합니다. */
		unordered_set( std::initializer_list<value_type> init )
			: _listBucket{}
			, _listDenseData{}
			, _traits{}
		{
			for ( const auto& value : init )
			{
				insert( value );
			}
		}

		/** @brief 복사 생성합니다. */
		unordered_set( const unordered_set& other )
			: _listBucket{ other._listBucket }
			, _listDenseData{ other._listDenseData }
			, _traits{ other._traits }
		{
			SW_SCOPED_RACE_READ_OTHER( other );
		}

		/** @brief 이동 생성합니다. */
		unordered_set( unordered_set&& other ) noexcept
			: _listBucket{ std::move( other._listBucket ) }
			, _listDenseData{ std::move( other._listDenseData ) }
			, _traits{ std::move( other._traits ) }
		{
			SW_SCOPED_RACE_WRITE_OTHER( other );
		}

		/** @brief 복사 대입합니다. */
		unordered_set& operator=( const unordered_set& other )
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_READ_OTHER( other );
				_listBucket	   = other._listBucket;
				_listDenseData = other._listDenseData;
				_traits		   = other._traits;
			}
			return *this;
		}

		/** @brief 이동 대입합니다. */
		unordered_set& operator=( unordered_set&& other ) noexcept
		{
			if ( this != &other )
			{
				SW_SCOPED_RACE_WRITE();
				SW_SCOPED_RACE_WRITE_OTHER( other );
				_listBucket	   = std::move( other._listBucket );
				_listDenseData = std::move( other._listDenseData );
				_traits		   = std::move( other._traits );
			}
			return *this;
		}

		/** @brief 시작 이터레이터를 반환합니다. */
		iterator begin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return iterator( this, 0 );
		}

		/** @brief 끝 이터레이터를 반환합니다. */
		iterator end() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return iterator( this, _listDenseData.size() );
		}

		const_iterator cbegin() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return const_iterator( this, 0 );
		}

		const_iterator cend() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return const_iterator( this, _listDenseData.size() );
		}

		// ------------------------------------------------------------------------------
		// 2) 조회 — 밀집 순회 · 버킷 검색
		// ------------------------------------------------------------------------------
		/** @brief 비어 있는지 반환합니다. */
		bool empty() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _listDenseData.empty();
		}

		/** @brief 원소 개수를 반환합니다. */
		size_type size() const noexcept
		{
			SW_SCOPED_RACE_READ();
			return _listDenseData.size();
		}

		// ------------------------------------------------------------------------------
		// 3) 변경 — insert/erase. erase 후 이터레이터 무효화에 주의
		// ------------------------------------------------------------------------------
		/** @brief 모든 원소를 제거합니다. */
		void clear() noexcept
		{
			SW_SCOPED_RACE_WRITE();
			_listDenseData.clear();
			for ( size_t& bucket : _listBucket )
			{
				bucket = kEmptySlot;
			}
		}

		/** @brief 키를 찾습니다. */
		iterator find( const Key& key ) const
		{
			SW_SCOPED_RACE_READ();
			if ( _listBucket.empty() )
				return end();
			size_t		  hash		   = get_hasher()( key );
			const size_t  bucketIndex  = hash % _listBucket.size();
			const size_t* pBucketData  = std::as_const( _listBucket ).data();
			const Node*	  pDenseData   = std::as_const( _listDenseData ).data();
			size_t		  currentIndex = pBucketData[bucketIndex];
			while ( currentIndex != kEmptySlot )
			{
				if ( get_equal()( pDenseData[currentIndex]._key, key ) )
					return iterator( this, currentIndex );
				currentIndex = pDenseData[currentIndex]._next;
			}
			return end();
		}

		/** @brief 키와 일치하는 원소 개수를 반환합니다. */
		size_type count( const Key& key ) const { return find( key ) != end() ? 1 : 0; }

		/** @brief 원소를 삽입합니다. */
		pair<iterator, bool> insert( const value_type& value )
		{
			SW_SCOPED_RACE_WRITE();
			check_expand();
			size_t		 hash		  = get_hasher()( value );
			const size_t bucketIndex  = hash % _listBucket.size();
			size_t		 currentIndex = _listBucket[bucketIndex];
			while ( currentIndex != kEmptySlot )
			{
				if ( get_equal()( _listDenseData[currentIndex]._key, value ) )
					return { iterator( this, currentIndex ), false };
				currentIndex = _listDenseData[currentIndex]._next;
			}

			const size_t newIndex = _listDenseData.size();
			_listDenseData.push_back( { value, _listBucket[bucketIndex] } );
			_listBucket[bucketIndex] = newIndex;
			return { iterator( this, newIndex ), true };
		}

		/** @brief 원소를 제자리 생성합니다. */
		template <typename... Args>
		pair<iterator, bool> emplace( Args&&... args )
		{
			SW_SCOPED_RACE_WRITE();
			check_expand();
			_listDenseData.push_back( { Key( std::forward<Args>( args )... ), kEmptySlot } );
			const Key& key = _listDenseData.back()._key;

			size_t		 hash		  = get_hasher()( key );
			const size_t bucketIndex  = hash % _listBucket.size();
			size_t		 currentIndex = _listBucket[bucketIndex];
			while ( currentIndex != kEmptySlot )
			{
				if ( get_equal()( _listDenseData[currentIndex]._key, key ) )
				{
					_listDenseData.pop_back();
					return { iterator( this, currentIndex ), false };
				}
				currentIndex = _listDenseData[currentIndex]._next;
			}

			const size_t newIndex		   = _listDenseData.size() - 1;
			_listDenseData[newIndex]._next = _listBucket[bucketIndex];
			_listBucket[bucketIndex]	   = newIndex;
			return { iterator( this, newIndex ), true };
		}

		/** @brief 원소를 제거합니다. */
		iterator erase( const_iterator pos )
		{
			if ( pos == end() )
				return end();
			erase( *pos );
			return iterator( this, pos._index );
		}

		/** @brief 원소를 제거합니다. */
		size_type erase( const Key& key )
		{
			SW_SCOPED_RACE_WRITE();
			if ( _listBucket.empty() )
				return 0;

			size_t		 hash		   = get_hasher()( key );
			const size_t bucketIndex   = hash % _listBucket.size();
			size_t		 currentIndex  = _listBucket[bucketIndex];
			size_t		 previousIndex = kEmptySlot;

			while ( currentIndex != kEmptySlot )
			{
				if ( get_equal()( _listDenseData[currentIndex]._key, key ) )
				{
					if ( previousIndex == kEmptySlot )
						_listBucket[bucketIndex] = _listDenseData[currentIndex]._next;
					else
						_listDenseData[previousIndex]._next = _listDenseData[currentIndex]._next;

					const size_t lastIndex = _listDenseData.size() - 1;
					if ( currentIndex != lastIndex )
					{
						_listDenseData[currentIndex] = std::move( _listDenseData[lastIndex] );

						size_t		 lastHash		   = get_hasher()( _listDenseData[currentIndex]._key );
						const size_t lastBucketIndex   = lastHash % _listBucket.size();
						size_t		 lastCurrentIndex  = _listBucket[lastBucketIndex];
						size_t		 lastPreviousIndex = kEmptySlot;
						while ( lastCurrentIndex != kEmptySlot )
						{
							if ( lastCurrentIndex == lastIndex )
							{
								if ( lastPreviousIndex == kEmptySlot )
									_listBucket[lastBucketIndex] = currentIndex;
								else
									_listDenseData[lastPreviousIndex]._next = currentIndex;
								break;
							}
							lastPreviousIndex = lastCurrentIndex;
							lastCurrentIndex  = _listDenseData[lastCurrentIndex]._next;
						}
					}

					_listDenseData.pop_back();
					return 1;
				}
				previousIndex = currentIndex;
				currentIndex  = _listDenseData[currentIndex]._next;
			}
			return 0;
		}

		/** @brief 버킷 수를 재해시합니다. */
		void rehash( size_type count )
		{
			SW_SCOPED_RACE_WRITE();
			rehash_internal( count );
		}

		/** @brief 내부 버킷을 재해시합니다. */
		void rehash_internal( size_type count )
		{
			if ( count <= _listBucket.size() )
				return;
			_listBucket.assign( count, kEmptySlot );
			for ( size_t denseIndex = 0; denseIndex < _listDenseData.size(); ++denseIndex )
			{
				size_t		 hash				 = get_hasher()( _listDenseData[denseIndex]._key );
				const size_t bucketIndex		 = hash % _listBucket.size();
				_listDenseData[denseIndex]._next = _listBucket[bucketIndex];
				_listBucket[bucketIndex]		 = denseIndex;
			}
		}

		/** @brief 용량을 예약합니다. */
		void reserve( size_type count ) { rehash( count ); }
	};
#endif
} // namespace sw
