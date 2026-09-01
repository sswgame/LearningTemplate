/**
 * @file vector.h
 * @brief 독자적인 sw::vector 구현체 (std::vector 인터페이스 호환).
 * @details 디버그에서 RaceDetectContext 로 동시 접근을 잡으며, InlineAllocator 와 함께 사용 시 SBO 를 지원합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename T, typename Allocator = std::allocator<T>>
	using vector = std::vector<T, Allocator>;
#else

	template <typename Alloc, typename = void>
	struct has_inline_allocator : std::false_type
	{
	};

	template <typename Alloc>
	struct has_inline_allocator<Alloc, std::void_t<decltype( std::declval<Alloc&>().get_inline_buffer() )>> : std::true_type
	{
	};

	template <typename T, typename Allocator = sw::Allocator<T>>
	/** @brief 커스텀 vector. API 는 STL 과 같으며 SBO 및 레이스 탐지를 지원합니다. */
	class vector : private Allocator
	{
	public:
		using value_type			 = T;
		using allocator_type		 = Allocator;
		using size_type				 = size_t;
		using difference_type		 = ptrdiff_t;
		using reference				 = T&;
		using const_reference		 = const T&;
		using pointer				 = T*;
		using const_pointer			 = const T*;
		using iterator				 = T*;
		using const_iterator		 = const T*;
		using reverse_iterator		 = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;

		// ------------------------------------------------------------------------------
		// 1) 생성 · 소멸
		// ------------------------------------------------------------------------------
		vector() noexcept( noexcept( Allocator() ) );
		explicit vector( const Allocator& alloc ) noexcept;
		vector( size_type count, const T& value, const Allocator& alloc = Allocator() );
		explicit vector( size_type count, const Allocator& alloc = Allocator() );
		template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
		vector( InputIt first, InputIt last, const Allocator& alloc = Allocator() );
		vector( const vector& other );
		vector( vector&& other ) noexcept;
		vector( std::initializer_list<T> init, const Allocator& alloc = Allocator() );
		~vector();

		vector& operator=( const vector& other );
		vector& operator=( vector&& other ) noexcept;
		vector& operator=( std::initializer_list<T> ilist );
		void	assign( size_type count, const T& value );
		template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
		void assign( InputIt first, InputIt last );
		void assign( std::initializer_list<T> ilist );

		// ------------------------------------------------------------------------------
		// 2) 조회
		// ------------------------------------------------------------------------------
		allocator_type		   get_allocator() const noexcept;
		reference			   at( size_type pos );
		const_reference		   at( size_type pos ) const;
		reference			   operator[]( size_type pos );
		const_reference		   operator[]( size_type pos ) const;
		reference			   front();
		const_reference		   front() const;
		reference			   back();
		const_reference		   back() const;
		T*					   data() noexcept;
		const T*			   data() const noexcept;
		iterator			   begin() noexcept;
		const_iterator		   begin() const noexcept;
		const_iterator		   cbegin() const noexcept;
		iterator			   end() noexcept;
		const_iterator		   end() const noexcept;
		const_iterator		   cend() const noexcept;
		reverse_iterator	   rbegin() noexcept;
		const_reverse_iterator rbegin() const noexcept;
		const_reverse_iterator crbegin() const noexcept;
		reverse_iterator	   rend() noexcept;
		const_reverse_iterator rend() const noexcept;
		const_reverse_iterator crend() const noexcept;
		bool				   empty() const noexcept;
		size_type			   size() const noexcept;
		size_type			   capacity() const noexcept;
		size_type			   max_size() const noexcept;
		void				   reserve( size_type new_cap );
		void				   shrink_to_fit();

		// ------------------------------------------------------------------------------
		// 3) 변경
		// ------------------------------------------------------------------------------
		void	 clear() noexcept;
		iterator insert( const_iterator pos, const T& value );
		iterator insert( const_iterator pos, T&& value );
		iterator insert( const_iterator pos, size_type count, const T& value );
		template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32> = 0>
		iterator insert( const_iterator pos, InputIt first, InputIt last );
		iterator insert( const_iterator pos, std::initializer_list<T> ilist );
		template <class... Args>
		iterator emplace( const_iterator pos, Args&&... args );
		iterator erase( const_iterator pos );
		iterator erase( const_iterator first, const_iterator last );
		void	 push_back( const T& value );
		void	 push_back( T&& value );
		template <class... Args>
		reference emplace_back( Args&&... args );
		void	  pop_back();
		void	  resize( size_type count );
		void	  resize( size_type count, const value_type& value );
		void	  swap( vector& other ) noexcept;

		// ------------------------------------------------------------------------------
		// 4) 비교 연산자
		// ------------------------------------------------------------------------------
		bool operator==( const vector& other ) const;
		bool operator!=( const vector& other ) const;

	private:
		constexpr bool has_inline_buffer() const;
		T*			   get_inline_ptr();
		const T*	   get_inline_ptr() const;
		size_t		   get_inline_cap() const;
		bool		   is_inline( const T* p ) const;
		T*			   do_allocate( size_t n );
		void		   do_deallocate( T* p, size_t n );
		void		   reserveInternal( size_t new_cap );
		void		   clearInternal() noexcept;

	private:
		SW_RACE_CTX_MEMBER

		T*	   _pData	 = nullptr;
		size_t _size	 = 0;
		size_t _capacity = 0;
	};

	// ------------------------------------------------------------------------------
	// 구현부 (Implementation)
	// ------------------------------------------------------------------------------

	template <typename T, typename Allocator>
	inline constexpr bool vector<T, Allocator>::has_inline_buffer() const
	{
		return has_inline_allocator<Allocator>::value;
	}

	template <typename T, typename Allocator>
	inline T* vector<T, Allocator>::get_inline_ptr()
	{
		if constexpr ( has_inline_allocator<Allocator>::value )
		{
			return this->Allocator::get_inline_buffer();
		}
		return nullptr;
	}

	template <typename T, typename Allocator>
	inline const T* vector<T, Allocator>::get_inline_ptr() const
	{
		if constexpr ( has_inline_allocator<Allocator>::value )
		{
			return const_cast<vector*>( this )->Allocator::get_inline_buffer();
		}
		return nullptr;
	}

	template <typename T, typename Allocator>
	inline size_t vector<T, Allocator>::get_inline_cap() const
	{
		if constexpr ( has_inline_allocator<Allocator>::value )
		{
			return const_cast<vector*>( this )->Allocator::get_inline_capacity();
		}
		return 0;
	}

	template <typename T, typename Allocator>
	inline bool vector<T, Allocator>::is_inline( const T* p ) const
	{
		return has_inline_buffer() && p == get_inline_ptr();
	}

	template <typename T, typename Allocator>
	inline T* vector<T, Allocator>::do_allocate( size_t n )
	{
		return Allocator::allocate( n );
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::do_deallocate( T* p, size_t n )
	{
		if ( p )
		{
			Allocator::deallocate( p, n );
		}
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::reserveInternal( size_t new_cap )
	{
		if ( new_cap <= _capacity )
			return;
		T* pNewData = do_allocate( new_cap );
		for ( size_t index = 0; index < _size; ++index )
		{
			if constexpr ( std::is_nothrow_move_constructible_v<T> )
				sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
			else if constexpr ( std::is_copy_constructible_v<T> )
				sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
			else if constexpr ( std::is_move_constructible_v<T> )
				sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
			_pData[index].~T();
		}
		if ( is_inline( _pData ) == false )
			do_deallocate( _pData, _capacity );
		_pData	  = pNewData;
		_capacity = new_cap;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::clearInternal() noexcept
	{
		for ( size_t index = 0; index < _size; ++index )
			_pData[index].~T();
		_size = 0;
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector() noexcept( noexcept( Allocator() ) )
		: Allocator()
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( const Allocator& alloc ) noexcept
		: Allocator( alloc )
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( size_type count, const T& value, const Allocator& alloc )
		: Allocator( alloc )
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
		assign( count, value );
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( size_type count, const Allocator& alloc )
		: Allocator( alloc )
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
		resize( count );
	}

	template <typename T, typename Allocator>
	template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
	inline vector<T, Allocator>::vector( InputIt first, InputIt last, const Allocator& alloc )
		: Allocator( alloc )
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
		assign( first, last );
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( const vector& other )
		: Allocator( std::allocator_traits<Allocator>::select_on_container_copy_construction( other.get_allocator() ) )
	{
		SW_SCOPED_RACE_READ_OTHER( other );
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
		reserveInternal( other._size );
		for ( size_t index = 0; index < other._size; ++index )
		{
			sw_placement_new( ( _pData + ( index ) ) ) T( other._pData[index] );
		}
		_size = other._size;
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( vector&& other ) noexcept
		: Allocator( std::move( other.get_allocator() ) )
	{
		SW_SCOPED_RACE_WRITE_OTHER( other );
		if ( other.is_inline( other._pData ) )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
			for ( size_t index = 0; index < other._size; ++index )
			{
				sw_placement_new( ( _pData + ( index ) ) ) T( std::move( other._pData[index] ) );
				other._pData[index].~T();
			}
			_size		= other._size;
			other._size = 0;
		}
		else
		{
			_pData	  = other._pData;
			_size	  = other._size;
			_capacity = other._capacity;
			if ( other.has_inline_buffer() )
			{
				other._pData	= other.get_inline_ptr();
				other._capacity = other.get_inline_cap();
			}
			else
			{
				other._pData	= nullptr;
				other._capacity = 0;
			}
			other._size = 0;
		}
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::vector( std::initializer_list<T> init, const Allocator& alloc )
		: Allocator( alloc )
	{
		if ( has_inline_buffer() )
		{
			_pData	  = get_inline_ptr();
			_capacity = get_inline_cap();
		}
		assign( init.begin(), init.end() );
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>::~vector()
	{
		clearInternal();
		if ( is_inline( _pData ) == false )
		{
			do_deallocate( _pData, _capacity );
		}
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>& vector<T, Allocator>::operator=( const vector& other )
	{
		if ( this != &other )
		{
			SW_SCOPED_RACE_WRITE();
			SW_SCOPED_RACE_READ_OTHER( other );
			clearInternal();
			reserveInternal( other._size );
			for ( size_t index = 0; index < other._size; ++index )
			{
				sw_placement_new( ( _pData + ( index ) ) ) T( other._pData[index] );
			}
			_size = other._size;
		}
		return *this;
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>& vector<T, Allocator>::operator=( vector&& other ) noexcept
	{
		if ( this != &other )
		{
			SW_SCOPED_RACE_WRITE();
			SW_SCOPED_RACE_WRITE_OTHER( other );
			clearInternal();
			if ( is_inline( _pData ) == false )
			{
				do_deallocate( _pData, _capacity );
				if ( has_inline_buffer() )
				{
					_pData	  = get_inline_ptr();
					_capacity = get_inline_cap();
				}
				else
				{
					_pData	  = nullptr;
					_capacity = 0;
				}
			}

			if ( other.is_inline( other._pData ) )
			{
				for ( size_t index = 0; index < other._size; ++index )
				{
					sw_placement_new( ( _pData + ( index ) ) ) T( std::move( other._pData[index] ) );
					other._pData[index].~T();
				}
			}
			else
			{
				_pData	  = other._pData;
				_capacity = other._capacity;
				if ( other.has_inline_buffer() )
				{
					other._pData	= other.get_inline_ptr();
					other._capacity = other.get_inline_cap();
				}
				else
				{
					other._pData	= nullptr;
					other._capacity = 0;
				}
			}
			_size		= other._size;
			other._size = 0;
		}
		return *this;
	}

	template <typename T, typename Allocator>
	inline vector<T, Allocator>& vector<T, Allocator>::operator=( std::initializer_list<T> ilist )
	{
		assign( ilist.begin(), ilist.end() );
		return *this;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::assign( size_type count, const T& value )
	{
		SW_SCOPED_RACE_WRITE();
		clearInternal();
		reserveInternal( count );
		for ( size_t index = 0; index < count; ++index )
		{
			sw_placement_new( ( _pData + ( index ) ) ) T( value );
		}
		_size = count;
	}

	template <typename T, typename Allocator>
	template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
	inline void vector<T, Allocator>::assign( InputIt first, InputIt last )
	{
		SW_SCOPED_RACE_WRITE();
		clearInternal();
		size_t count = static_cast<size_t>( std::distance( first, last ) );
		reserveInternal( count );
		for ( auto it = first; it != last; ++it )
		{
			sw_placement_new( ( _pData + ( _size++ ) ) ) T( *it );
		}
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::assign( std::initializer_list<T> ilist )
	{
		assign( ilist.begin(), ilist.end() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::allocator_type vector<T, Allocator>::get_allocator() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return *this;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reference vector<T, Allocator>::at( size_type pos )
	{
		SW_SCOPED_RACE_WRITE();
		SW_ASSERT( pos < _size );
		return _pData[pos];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::at( size_type pos ) const
	{
		SW_SCOPED_RACE_READ();
		SW_ASSERT( pos < _size );
		return _pData[pos];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reference vector<T, Allocator>::operator[]( size_type pos )
	{
		SW_SCOPED_RACE_WRITE();
		SW_ASSERT( pos < _size );
		return _pData[pos];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::operator[]( size_type pos ) const
	{
		SW_SCOPED_RACE_READ();
		SW_ASSERT( pos < _size );
		return _pData[pos];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reference vector<T, Allocator>::front()
	{
		SW_SCOPED_RACE_WRITE();
		SW_ASSERT( _size > 0 );
		return _pData[0];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::front() const
	{
		SW_SCOPED_RACE_READ();
		SW_ASSERT( _size > 0 );
		return _pData[0];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reference vector<T, Allocator>::back()
	{
		SW_SCOPED_RACE_WRITE();
		SW_ASSERT( _size > 0 );
		return _pData[_size - 1];
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reference vector<T, Allocator>::back() const
	{
		SW_SCOPED_RACE_READ();
		SW_ASSERT( _size > 0 );
		return _pData[_size - 1];
	}

	template <typename T, typename Allocator>
	inline T* vector<T, Allocator>::data() noexcept
	{
		SW_SCOPED_RACE_WRITE();
		return _pData;
	}

	template <typename T, typename Allocator>
	inline const T* vector<T, Allocator>::data() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::begin() noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::begin() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::cbegin() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::end() noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData + _size;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::end() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData + _size;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_iterator vector<T, Allocator>::cend() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _pData + _size;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reverse_iterator vector<T, Allocator>::rbegin() noexcept
	{
		SW_SCOPED_RACE_READ();
		return reverse_iterator( end() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::rbegin() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return const_reverse_iterator( end() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::crbegin() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return const_reverse_iterator( cend() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::reverse_iterator vector<T, Allocator>::rend() noexcept
	{
		SW_SCOPED_RACE_READ();
		return reverse_iterator( begin() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::rend() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return const_reverse_iterator( begin() );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::const_reverse_iterator vector<T, Allocator>::crend() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return const_reverse_iterator( cbegin() );
	}

	template <typename T, typename Allocator>
	inline bool vector<T, Allocator>::empty() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _size == 0;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::size_type vector<T, Allocator>::size() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _size;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::size_type vector<T, Allocator>::capacity() const noexcept
	{
		SW_SCOPED_RACE_READ();
		return _capacity;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::size_type vector<T, Allocator>::max_size() const noexcept
	{
		return size_type( -1 ) / sizeof( T );
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::reserve( size_type new_cap )
	{
		SW_SCOPED_RACE_WRITE();
		reserveInternal( new_cap );
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::shrink_to_fit()
	{
		SW_SCOPED_RACE_WRITE();
		if ( _capacity > _size && is_inline( _pData ) == false )
		{
			if ( has_inline_buffer() && _size <= get_inline_cap() )
			{
				T* pNewData = get_inline_ptr();
				for ( size_t index = 0; index < _size; ++index )
				{
					if constexpr ( std::is_nothrow_move_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
					else if constexpr ( std::is_copy_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
					else if constexpr ( std::is_move_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
					_pData[index].~T();
				}
				do_deallocate( _pData, _capacity );
				_pData	  = pNewData;
				_capacity = get_inline_cap();
			}
			else
			{
				if ( _size == 0 )
					return;
				T* pNewData = do_allocate( _size );
				for ( size_t index = 0; index < _size; ++index )
				{
					if constexpr ( std::is_nothrow_move_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
					else if constexpr ( std::is_copy_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( _pData[index] );
					else if constexpr ( std::is_move_constructible_v<T> )
						sw_placement_new( ( pNewData + ( index ) ) ) T( std::move( _pData[index] ) );
					_pData[index].~T();
				}
				do_deallocate( _pData, _capacity );
				_pData	  = pNewData;
				_capacity = _size;
			}
		}
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::clear() noexcept
	{
		SW_SCOPED_RACE_WRITE();
		clearInternal();
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, const T& value )
	{
		return insert( pos, 1, value );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, T&& value )
	{
		SW_SCOPED_RACE_WRITE();
		size_t offset = static_cast<size_t>( pos - _pData );
		SW_ASSERT( offset <= _size );
		if ( _size >= _capacity )
			reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );

		if ( offset < _size )
		{
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( _pData[_size - 1] ) );
			for ( size_t itemIndex = _size - 1; itemIndex > offset; --itemIndex )
			{
				_pData[itemIndex] = std::move( _pData[itemIndex - 1] );
			}
			_pData[offset] = std::move( value );
		}
		else
		{
			sw_placement_new( ( _pData + ( offset ) ) ) T( std::move( value ) );
		}
		++_size;
		return _pData + offset;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, size_type count, const T& value )
	{
		SW_SCOPED_RACE_WRITE();
		size_t offset = static_cast<size_t>( pos - _pData );
		SW_ASSERT( offset <= _size );
		if ( _size + count > _capacity )
			reserveInternal( MathUtil::max( _capacity * 2, _size + count ) );

		if ( offset < _size )
		{
			for ( size_t itemIndex = _size; itemIndex < _size + count; ++itemIndex )
			{
				if ( itemIndex - count >= offset )
					sw_placement_new( ( _pData + ( itemIndex ) ) ) T( std::move( _pData[itemIndex - count] ) );
				else
					sw_placement_new( ( _pData + ( itemIndex ) ) ) T( value );
			}
			for ( size_t itemIndex = _size - 1; itemIndex >= offset + count; --itemIndex )
			{
				_pData[itemIndex] = std::move( _pData[itemIndex - count] );
			}
			for ( size_t itemIndex = offset; itemIndex < MathUtil::min( _size, offset + count ); ++itemIndex )
			{
				_pData[itemIndex] = value;
			}
		}
		else
		{
			for ( size_t index = 0; index < count; ++index )
			{
				sw_placement_new( ( _pData + ( offset + index ) ) ) T( value );
			}
		}
		_size += count;
		return _pData + offset;
	}

	template <typename T, typename Allocator>
	template <class InputIt, typename std::enable_if_t<!std::is_integral_v<InputIt>, int32>>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, InputIt first, InputIt last )
	{
		size_t offset = static_cast<size_t>( pos - _pData );
		for ( auto it = first; it != last; ++it )
		{
			insert( _pData + offset, *it );
			++offset;
		}
		return _pData + ( pos - _pData );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::insert( const_iterator pos, std::initializer_list<T> ilist )
	{
		return insert( pos, ilist.begin(), ilist.end() );
	}

	template <typename T, typename Allocator>
	template <class... Args>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::emplace( const_iterator pos, Args&&... args )
	{
		return insert( pos, T( std::forward<Args>( args )... ) );
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::erase( const_iterator pos )
	{
		SW_SCOPED_RACE_WRITE();
		size_t offset = static_cast<size_t>( pos - _pData );
		SW_ASSERT( offset < _size );
		for ( size_t itemIndex = offset; itemIndex < _size - 1; ++itemIndex )
		{
			_pData[itemIndex] = std::move( _pData[itemIndex + 1] );
		}
		_pData[_size - 1].~T();
		--_size;
		return _pData + offset;
	}

	template <typename T, typename Allocator>
	inline typename vector<T, Allocator>::iterator vector<T, Allocator>::erase( const_iterator first, const_iterator last )
	{
		SW_SCOPED_RACE_WRITE();
		size_t offset = static_cast<size_t>( first - _pData );
		size_t count  = static_cast<size_t>( last - first );
		SW_ASSERT( offset + count <= _size );
		if ( count > 0 )
		{
			for ( size_t itemIndex = offset; itemIndex < _size - count; ++itemIndex )
			{
				_pData[itemIndex] = std::move( _pData[itemIndex + count] );
			}
			for ( size_t itemIndex = _size - count; itemIndex < _size; ++itemIndex )
			{
				_pData[itemIndex].~T();
			}
			_size -= count;
		}
		return _pData + offset;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::push_back( const T& value )
	{
		SW_SCOPED_RACE_WRITE();
		if ( _size >= _capacity )
		{
			T copy = value;
			reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( copy ) );
		}
		else
		{
			sw_placement_new( ( _pData + ( _size ) ) ) T( value );
		}
		++_size;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::push_back( T&& value )
	{
		SW_SCOPED_RACE_WRITE();
		if ( _size >= _capacity )
		{
			T temp( std::move( value ) );
			reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( temp ) );
		}
		else
		{
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( value ) );
		}
		++_size;
	}

	template <typename T, typename Allocator>
	template <class... Args>
	inline typename vector<T, Allocator>::reference vector<T, Allocator>::emplace_back( Args&&... args )
	{
		SW_SCOPED_RACE_WRITE();
		if ( _size >= _capacity )
		{
			T temp( std::forward<Args>( args )... );
			reserveInternal( _capacity == 0 ? 4 : _capacity * 2 );
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::move( temp ) );
		}
		else
		{
			sw_placement_new( ( _pData + ( _size ) ) ) T( std::forward<Args>( args )... );
		}
		++_size;
		return _pData[_size - 1];
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::pop_back()
	{
		SW_SCOPED_RACE_WRITE();
		SW_ASSERT( _size > 0 );
		_pData[_size - 1].~T();
		--_size;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::resize( size_type count )
	{
		SW_SCOPED_RACE_WRITE();
		if ( count < _size )
		{
			for ( size_t itemIndex = count; itemIndex < _size; ++itemIndex )
				_pData[itemIndex].~T();
		}
		else if ( count > _size )
		{
			reserveInternal( count );
			for ( size_t itemIndex = _size; itemIndex < count; ++itemIndex )
				sw_placement_new( ( _pData + ( itemIndex ) ) ) T();
		}
		_size = count;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::resize( size_type count, const value_type& value )
	{
		SW_SCOPED_RACE_WRITE();
		if ( count < _size )
		{
			for ( size_t itemIndex = count; itemIndex < _size; ++itemIndex )
				_pData[itemIndex].~T();
		}
		else if ( count > _size )
		{
			reserveInternal( count );
			for ( size_t itemIndex = _size; itemIndex < count; ++itemIndex )
				sw_placement_new( ( _pData + ( itemIndex ) ) ) T( value );
		}
		_size = count;
	}

	template <typename T, typename Allocator>
	inline void vector<T, Allocator>::swap( vector& other ) noexcept
	{
		SW_SCOPED_RACE_WRITE();
		SW_SCOPED_RACE_WRITE_OTHER( other );

		if ( is_inline( _pData ) || other.is_inline( other._pData ) )
		{
			vector temp = std::move( *this );
			*this		= std::move( other );
			other		= std::move( temp );
		}
		else
		{
			std::swap( _pData, other._pData );
			std::swap( _size, other._size );
			std::swap( _capacity, other._capacity );
		}
	}

	template <typename T, typename Allocator>
	inline bool vector<T, Allocator>::operator==( const vector& other ) const
	{
		SW_SCOPED_RACE_READ();
		SW_SCOPED_RACE_READ_OTHER( other );
		if ( _size != other._size )
			return false;
		for ( size_t index = 0; index < _size; ++index )
		{
			if ( ( _pData[index] == other._pData[index] ) == false )
				return false;
		}
		return true;
	}

	template <typename T, typename Allocator>
	inline bool vector<T, Allocator>::operator!=( const vector& other ) const
	{
		return ( *this == other ) == false;
	}

#endif
	template <typename T, size_t N = 32>
	using small_vector = vector<T, InlineAllocator<T, N>>;
} // namespace sw
