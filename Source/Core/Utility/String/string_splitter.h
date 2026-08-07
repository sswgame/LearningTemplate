#pragma once
/**
 * @file string_splitter.h
 * @brief 구분자 기반 문자열 분할기
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	template <typename T>
	class basic_string_splitter
	{
		using value_type = T;

	public:
		/**
		 * @brief 구분자 기반 분할기를 생성합니다
		 */
		explicit basic_string_splitter( std::basic_string_view<value_type> str, std::initializer_list<std::basic_string_view<value_type>> delimList );

	public:
		uint32 getCount() const { return static_cast<uint32>( _splitList.size() ); }

		const std::vector<std::basic_string_view<value_type>>& getSplitList() const { return _splitList; }

	private:
		/**
		 * @brief 구분자로 분할합니다
		 */
		void split( std::initializer_list<std::basic_string_view<value_type>> delimList );

	private:
		std::basic_string_view<value_type>				_str;
		std::vector<std::basic_string_view<value_type>> _splitList;
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

		size_t start = 0;

		while ( start < _str.length() )
		{
			size_t min_pos		= std::basic_string_view<value_type>::npos;
			size_t delim_length = 0;

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
			{
				_splitList.push_back( _str.substr( start, min_pos - start ) );
			}

			start = min_pos + delim_length;
		}
	}
#pragma endregion

} // namespace sw
