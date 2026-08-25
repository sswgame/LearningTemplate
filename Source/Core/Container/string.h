/**
 * @file string.h
 * @brief std::basic_string 래퍼. 디버그에서 RaceDetectContext 로 동시 접근을 잡습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Memory/Memory.h"

#include <string>

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
	template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = std::allocator<CharT>>
	using basic_string = std::basic_string<CharT, Traits, Allocator>;
#else
	/** @brief std::basic_string + 디버그 레이스 탐지. API 는 STL 과 같습니다. */
	template <typename CharT, typename Traits = std::char_traits<CharT>, typename Allocator = Allocator<CharT>>
	class basic_string : public std::basic_string<CharT, Traits, Allocator>
	{
		using Base = std::basic_string<CharT, Traits, Allocator>;
		mutable RaceDetectContext _raceCtx{};

	public:
		using traits_type			 = typename Base::traits_type;
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

		static const size_type npos = Base::npos;

		// ------------------------------------------------------------------------------
		// 1) 생성 · 대입 — 내용은 Base 에 두고, 레이스 컨텍스트는 이 인스턴스 것
		// ------------------------------------------------------------------------------
		/** @brief 빈 문자열로 둡니다. */
		basic_string() noexcept( noexcept( Allocator() ) )
			: Base() {}

		/** @brief 지정 할당자로 빈 문자열을 둡니다. */
		explicit basic_string( const Allocator& alloc ) noexcept
			: Base( alloc ) {}

		/** @brief count 개의 ch 로 채웁니다. */
		basic_string( size_type count, CharT ch, const Allocator& alloc = Allocator() )
			: Base( count, ch, alloc ) {}

		/** @brief 복사 생성합니다. */
		basic_string( const basic_string& other, size_type pos, const Allocator& alloc = Allocator() )
			: Base( static_cast<const Base&>( other ), pos, alloc ) {}

		/** @brief 복사 생성합니다. */
		basic_string( const basic_string& other, size_type pos, size_type count, const Allocator& alloc = Allocator() )
			: Base( static_cast<const Base&>( other ), pos, count, alloc ) {}

		/** @brief C 문자열 count 문자를 복사합니다. */
		basic_string( const CharT* s, size_type count, const Allocator& alloc = Allocator() )
			: Base( s, count, alloc ) {}

		/** @brief 널 종료 C 문자열을 복사합니다. */
		basic_string( const CharT* s, const Allocator& alloc = Allocator() )
			: Base( s, alloc ) {}

		/** @brief [first, last) 를 복사해 채웁니다. */
		template <class InputIt>
		basic_string( InputIt first, InputIt last, const Allocator& alloc = Allocator() )
			: Base( first, last, alloc ) {}

		/** @brief std::basic_string 내용을 복사합니다. */
		basic_string( const Base& other )
			: Base( other ) {}

		/** @brief 이동 생성합니다. */
		basic_string( Base&& other ) noexcept
			: Base( std::move( other ) ) {}

		/** @brief 복사 생성합니다. */
		basic_string( const basic_string& other )
			: Base( static_cast<const Base&>( other ) ) {}

		/** @brief 복사 생성합니다. */
		basic_string( const basic_string& other, const Allocator& alloc )
			: Base( static_cast<const Base&>( other ), alloc ) {}

		/** @brief 이동 생성합니다. */
		basic_string( basic_string&& other ) noexcept
			: Base( std::move( static_cast<Base&>( other ) ) ) {}

		/** @brief 이동 생성합니다. */
		basic_string( basic_string&& other, const Allocator& alloc )
			: Base( std::move( static_cast<Base&>( other ) ), alloc ) {}

		/** @brief 초기화 리스트로 채웁니다. */
		basic_string( std::initializer_list<CharT> ilist, const Allocator& alloc = Allocator() )
			: Base( ilist, alloc ) {}

		/** @brief string_view 호환 타입에서 복사합니다. */
		template <class StringViewLike>
		explicit basic_string( const StringViewLike& t, const Allocator& alloc = Allocator() )
			: Base( t, alloc ) {}

		/** @brief string_view 호환 타입의 부분 문자열을 복사합니다. */
		template <class StringViewLike>
		basic_string( const StringViewLike& t, size_type pos, size_type n, const Allocator& alloc = Allocator() )
			: Base( t, pos, n, alloc ) {}

		/** @brief 복사 대입합니다. */
		basic_string& operator=( const Base& other )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( other );
			return *this;
		}

		/** @brief 이동 대입합니다. */
		basic_string& operator=( Base&& other ) noexcept
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( std::move( other ) );
			return *this;
		}

		/** @brief 복사 대입합니다. */
		basic_string& operator=( const basic_string& other )
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
		basic_string& operator=( basic_string&& other ) noexcept
		{
			if ( this != &other )
			{
				ScopedRaceWrite lockThis( _raceCtx );
				ScopedRaceWrite lockOther( other._raceCtx );
				Base::operator=( std::move( static_cast<Base&>( other ) ) );
			}
			return *this;
		}

		/** @brief 복사 대입합니다. */
		basic_string& operator=( const CharT* s )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( s );
			return *this;
		}

		/** @brief 대입합니다. */
		basic_string& operator=( CharT ch )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( ch );
			return *this;
		}

		/** @brief 초기화 리스트로 대입합니다. */
		basic_string& operator=( std::initializer_list<CharT> ilist )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( ilist );
			return *this;
		}

		/** @brief 복사 대입합니다. */
		template <class StringViewLike>
		basic_string& operator=( const StringViewLike& t )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator=( t );
			return *this;
		}

		// ------------------------------------------------------------------------------
		// 2) 조회 — 문자·이터레이터·크기. 비const 접근도 쓰기 락 (참조 유출)
		// ------------------------------------------------------------------------------
		/** @brief 범위 검사와 함께 원소를 반환합니다. */
		reference at( size_type pos )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::at( pos );
		}

		/** @brief 범위 검사와 함께 원소를 반환합니다. */
		const_reference at( size_type pos ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::at( pos );
		}

		/** @brief 지정 위치의 원소를 반환합니다. */
		reference operator[]( size_type pos )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::operator[]( pos );
		}

		/** @brief 지정 위치의 원소를 반환합니다. */
		const_reference operator[]( size_type pos ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::operator[]( pos );
		}

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

		/** @brief 내부 버퍼 포인터를 반환합니다. */
		const CharT* data() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::data();
		}

		/** @brief 내부 버퍼 포인터를 반환합니다. */
		CharT* data() noexcept
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::data();
		}

		/** @brief 널 종료 C 문자열을 반환합니다. */
		const CharT* c_str() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::c_str();
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

		/** @brief 길이를 반환합니다. */
		size_type length() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::length();
		}

		/** @brief 담을 수 있는 최대 원소 개수를 반환합니다. */
		size_type max_size() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::max_size();
		}

		/** @brief 용량을 예약합니다. */
		void reserve( size_type new_cap )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::reserve( new_cap );
		}

		/** @brief 현재 용량을 반환합니다. */
		size_type capacity() const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::capacity();
		}

		/** @brief 용량을 크기에 맞춥니다. */
		void shrink_to_fit()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::shrink_to_fit();
		}

		// Operations
		/** @brief 모든 원소를 제거합니다. */
		// ------------------------------------------------------------------------------
		// 3) 변경 — append/insert/erase. 쓰기 락
		// ------------------------------------------------------------------------------
		void clear() noexcept
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::clear();
		}

		/** @brief 원소를 삽입합니다. */
		basic_string& insert( size_type index, size_type count, CharT ch )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::insert( index, count, ch );
			return *this;
		}

		/** @brief 원소를 삽입합니다. */
		basic_string& insert( size_type index, const CharT* s )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::insert( index, s );
			return *this;
		}

		/** @brief 원소를 삽입합니다. */
		basic_string& insert( size_type index, const CharT* s, size_type count )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::insert( index, s, count );
			return *this;
		}

		/** @brief 원소를 삽입합니다. */
		basic_string& insert( size_type index, const basic_string& str )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::insert( index, static_cast<const Base&>( str ) );
			return *this;
		}

		/** @brief 원소를 삽입합니다. */
		basic_string& insert( size_type index, const basic_string& str, size_type index_str, size_type count = npos )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::insert( index, static_cast<const Base&>( str ), index_str, count );
			return *this;
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, CharT ch )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, ch );
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, size_type count, CharT ch )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, count, ch );
		}

		/** @brief 원소를 삽입합니다. */
		template <class InputIt>
		iterator insert( const_iterator pos, InputIt first, InputIt last )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, first, last );
		}

		/** @brief 원소를 삽입합니다. */
		iterator insert( const_iterator pos, std::initializer_list<CharT> ilist )
		{
			ScopedRaceWrite lock( _raceCtx );
			return Base::insert( pos, ilist );
		}

		/** @brief 원소를 제거합니다. */
		basic_string& erase( size_type index = 0, size_type count = npos )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::erase( index, count );
			return *this;
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
		void push_back( CharT ch )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::push_back( ch );
		}

		/** @brief 마지막 원소를 제거합니다. */
		void pop_back()
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::pop_back();
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( size_type count, CharT ch )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( count, ch );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( const basic_string& str )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( static_cast<const Base&>( str ) );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( const basic_string& str, size_type pos, size_type count = npos )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( static_cast<const Base&>( str ), pos, count );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( const CharT* s, size_type count )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( s, count );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( const CharT* s )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( s );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		template <class InputIt>
		basic_string& append( InputIt first, InputIt last )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( first, last );
			return *this;
		}

		/** @brief 뒤에 이어 붙입니다. */
		basic_string& append( std::initializer_list<CharT> ilist )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( ilist );
			return *this;
		}

		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( const Base& str )
		{
			ScopedRaceWrite lockThis( _raceCtx );
			Base::operator+=( str );
			return *this;
		}

		/** @brief 기본 문자열 참조로 변환합니다. */
		operator const Base&() const noexcept { return *this; }
		/** @brief string_view로 변환합니다. */
		operator std::string_view() const noexcept { return std::string_view( this->data(), this->size() ); }
		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( const basic_string& str ) { return append( str ); }
		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( CharT ch )
		{
			push_back( ch );
			return *this;
		}

		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( const CharT* s ) { return append( s ); }
		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( std::initializer_list<CharT> ilist ) { return append( ilist ); }
		/** @brief 더한 뒤 대입합니다. */
		basic_string& operator+=( std::string_view sv )
		{
			ScopedRaceWrite lock( _raceCtx );
			Base::append( sv.data(), sv.size() );
			return *this;
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( const basic_string& str ) const noexcept
		{
			ScopedRaceRead lockThis( _raceCtx );
			ScopedRaceRead lockOther( str._raceCtx );
			return Base::compare( static_cast<const Base&>( str ) );
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( size_type pos1, size_type count1, const basic_string& str ) const
		{
			ScopedRaceRead lockThis( _raceCtx );
			ScopedRaceRead lockOther( str._raceCtx );
			return Base::compare( pos1, count1, static_cast<const Base&>( str ) );
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( size_type pos1, size_type count1, const basic_string& str, size_type pos2, size_type count2 = npos ) const
		{
			ScopedRaceRead lockThis( _raceCtx );
			ScopedRaceRead lockOther( str._raceCtx );
			return Base::compare( pos1, count1, static_cast<const Base&>( str ), pos2, count2 );
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( const CharT* s ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::compare( s );
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( size_type pos1, size_type count1, const CharT* s ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::compare( pos1, count1, s );
		}

		/** @brief 사전순으로 비교합니다. */
		int32 compare( size_type pos1, size_type count1, const CharT* s, size_type count2 ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::compare( pos1, count1, s, count2 );
		}

		/** @brief 같은지 비교합니다. */
		bool operator==( const basic_string& rhs ) const noexcept { return compare( rhs ) == 0; }
		/** @brief 다른지 비교합니다. */
		bool operator!=( const basic_string& rhs ) const noexcept { return compare( rhs ) != 0; }
		/** @brief 사전순으로 작은지 비교합니다. */
		bool operator<( const basic_string& rhs ) const noexcept { return compare( rhs ) < 0; }
		/** @brief 작거나 같은지 비교합니다. */
		bool operator<=( const basic_string& rhs ) const noexcept { return compare( rhs ) <= 0; }
		/** @brief 사전순으로 큰지 비교합니다. */
		bool operator>( const basic_string& rhs ) const noexcept { return compare( rhs ) > 0; }
		/** @brief 크거나 같은지 비교합니다. */
		bool operator>=( const basic_string& rhs ) const noexcept { return compare( rhs ) >= 0; }

		/** @brief 같은지 비교합니다. */
		bool operator==( const CharT* rhs ) const noexcept { return compare( rhs ) == 0; }
		/** @brief 다른지 비교합니다. */
		bool operator!=( const CharT* rhs ) const noexcept { return compare( rhs ) != 0; }
		/** @brief 사전순으로 작은지 비교합니다. */
		bool operator<( const CharT* rhs ) const noexcept { return compare( rhs ) < 0; }
		/** @brief 작거나 같은지 비교합니다. */
		bool operator<=( const CharT* rhs ) const noexcept { return compare( rhs ) <= 0; }
		/** @brief 사전순으로 큰지 비교합니다. */
		bool operator>( const CharT* rhs ) const noexcept { return compare( rhs ) > 0; }
		/** @brief 크거나 같은지 비교합니다. */
		bool operator>=( const CharT* rhs ) const noexcept { return compare( rhs ) >= 0; }

		// Search
		/** @brief 키를 찾습니다. */
		size_type find( const basic_string& str, size_type pos = 0 ) const noexcept
		{
			ScopedRaceRead lockThis( _raceCtx );
			ScopedRaceRead lockOther( str._raceCtx );
			return Base::find( static_cast<const Base&>( str ), pos );
		}

		/** @brief 키를 찾습니다. */
		size_type find( const CharT* s, size_type pos, size_type count ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::find( s, pos, count );
		}

		/** @brief 키를 찾습니다. */
		size_type find( const CharT* s, size_type pos = 0 ) const
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::find( s, pos );
		}

		/** @brief 키를 찾습니다. */
		size_type find( CharT ch, size_type pos = 0 ) const noexcept
		{
			ScopedRaceRead lock( _raceCtx );
			return Base::find( ch, pos );
		}
	};
#endif

	using string  = basic_string<utf8>;
	using wstring = basic_string<utf16>;
#if defined( __cpp_char8_t )
	using u8string = basic_string<char8_t>;
#endif
	using u16string = basic_string<char16_t>;
	using u32string = basic_string<char32_t>;
} // namespace sw

namespace std
{
	/** @brief sw::string 을 unordered_map 키로 쓸 때 std::string_view 해시를 씁니다. */
	template <>
	struct hash<sw::string>
	{
		using is_transparent = void;
		/** @brief 내용 바이트를 해시합니다. */
		size_t operator()( const sw::string& s ) const noexcept { return hash<std::string_view>{}( std::string_view{ s.data(), s.size() } ); }
		size_t operator()( std::string_view s ) const noexcept { return hash<std::string_view>{}( s ); }
		size_t operator()( const utf8* s ) const noexcept { return hash<std::string_view>{}( std::string_view{ s } ); }
	};

	/** @brief sw::wstring 을 unordered_map 키로 쓸 때 std::wstring_view 해시를 씁니다. */
	template <>
	struct hash<sw::wstring>
	{
		using is_transparent = void;
		/** @brief 내용 바이트를 해시합니다. */
		size_t operator()( const sw::wstring& s ) const noexcept { return hash<std::wstring_view>{}( std::wstring_view{ s.data(), s.size() } ); }
		size_t operator()( std::wstring_view s ) const noexcept { return hash<std::wstring_view>{}( s ); }
		size_t operator()( const utf16* s ) const noexcept { return hash<std::wstring_view>{}( std::wstring_view{ s } ); }
	};
} // namespace std
