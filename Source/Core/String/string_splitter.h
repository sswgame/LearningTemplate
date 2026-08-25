/**
 * @file string_splitter.h
 * @brief 구분자 기반 문자열 분할기
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) basic_string_splitter — 생성 시 분할, getSplitList 로 뷰만 조회
	//    결과는 원본 뷰의 부분 문자열. 원본이 살아 있어야 함
	// ------------------------------------------------------------------------------
	template <typename T>
	/** @brief 구분자가 나타날 때마다 원본 뷰를 잘라 목록에 넣습니다. */
	class basic_string_splitter
	{
		using value_type = T;

	public:
		/**
		 * @brief str 을 delimList 기준으로 잘라 _splitList 에 넣습니다.
		 */
		explicit basic_string_splitter( std::basic_string_view<value_type> str, std::initializer_list<std::basic_string_view<value_type>> delimList );

		/** @brief 잘린 조각 개수입니다. */
		uint32 getCount() const { return static_cast<uint32>( _splitList.size() ); }

		/** @brief 원본을 가리키는 부분 뷰 목록입니다. */
		const vector<std::basic_string_view<value_type>>& getSplitList() const { return _splitList; }

	private:
		/**
		 * @brief 가장 가까운 구분자를 찾아 조각을 푸시합니다.
		 */
		void split( std::initializer_list<std::basic_string_view<value_type>> delimList );

		std::basic_string_view<value_type>		   _str;
		vector<std::basic_string_view<value_type>> _splitList;
	};

	using string_splitter = basic_string_splitter<utf8>;

	using wstring_splitter = basic_string_splitter<utf16>;

#pragma region IMPLEMENTATION
	template <typename TChar>
	basic_string_splitter<TChar>::basic_string_splitter( std::basic_string_view<value_type> str, std::initializer_list<std::basic_string_view<value_type>> delimList )
		: _str{ str }
		, _splitList{}
	{
		_splitList.reserve( 8 );
		split( delimList );
	}

	template <typename TChar>
	void basic_string_splitter<TChar>::split( std::initializer_list<std::basic_string_view<value_type>> delimList )
	{
		if ( _str.empty() )
			return;

		if ( delimList.size() == 0 )
		{
			_splitList.push_back( _str );
			return;
		}

		if ( delimList.size() == 1 )
		{
			const auto& delim = *delimList.begin();
			if ( delim.empty() )
			{
				_splitList.push_back( _str );
				return;
			}

			size_t		 start{ 0 };
			const size_t delim_length = delim.length();
			while ( start < _str.length() )
			{
				size_t pos = _str.find( delim, start );
				if ( pos == std::basic_string_view<value_type>::npos )
				{
					_splitList.push_back( _str.substr( start ) );
					break;
				}

				if ( pos > start )
					_splitList.push_back( _str.substr( start, pos - start ) );

				start = pos + delim_length;
			}
			return;
		}

		size_t start{ 0 };

		while ( start < _str.length() )
		{
			size_t min_pos = std::basic_string_view<value_type>::npos;
			size_t delim_length{ 0 };

			for ( const auto& delim : delimList )
			{
				if ( delim.empty() )
					continue;

				size_t pos = _str.find( delim, start );
				if ( pos != std::basic_string_view<value_type>::npos && pos < min_pos )
				{
					min_pos		 = pos;
					delim_length = delim.length();
				}
			}

			if ( min_pos == std::basic_string_view<value_type>::npos )
			{
				if ( start < _str.length() )
					_splitList.push_back( _str.substr( start ) );
				break;
			}

			if ( min_pos > start )
				_splitList.push_back( _str.substr( start, min_pos - start ) );

			start = min_pos + delim_length;
		}
	}
#pragma endregion

} // namespace sw
