#pragma once
/**
 * @file hashed_string.h
 * @brief 해시 기반 문자열(basic_hashed_string)과 사전 정의 이름 열거형
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw
{

	/**
	 * @enum PredefinedNameType
	 * @brief PredefinedNameType.xxx 의 REGISTER_NAME으로 생성되는 사전 정의 이름 열거
	 */
	enum class PredefinedNameType : uint8
	{
#define REGISTER_NAME( index, name ) NameType_##name = index,
#include "Core/Utility/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
		Count
	};
}

namespace sw
{

	template <typename T, typename N = uint32>
	class basic_hashed_string
	{
		using value_type = T;
		using size_type	 = N;
		using hash_type	 = N;

	public:

		struct HashFunc
		{
			std::size_t operator()( const basic_hashed_string& key ) const
			{
				return key.getHash();
			}
		};

	public:

		basic_hashed_string() noexcept
			: _stringKeyIndex{ static_cast<uint32>( PredefinedNameType::NameType_None ) }
		{
		}

		basic_hashed_string( const value_type* str, const size_type length ) noexcept
			: _stringKeyIndex{ helper( str, length ) }
		{
		}

		template <std::size_t U>
		explicit basic_hashed_string( const std::array<value_type, U>& scopedString ) noexcept
			: _stringKeyIndex{ helper( scopedString.data() ) }
		{
		}

		template <size_type U>
		explicit basic_hashed_string( const value_type ( &str )[U] ) noexcept
			: _stringKeyIndex{ helper( str ) }
		{
		}

		explicit basic_hashed_string( const T* str ) noexcept
			: _stringKeyIndex{ helper( str ) }
		{
		}

	public:

		/**
		 * @brief 크기를 반환합니다
		 */
		size_type size() const noexcept;

		const value_type* c_str() const noexcept;

		/**
		 * @brief 해시 값을 반환합니다
		 */
		hash_type getHash() const noexcept;

		bool empty() const noexcept
		{
			return _stringKeyIndex == static_cast<uint32>( PredefinedNameType::NameType_None ) || size() == 0;
		}

		uint32 getIndex() const noexcept { return _stringKeyIndex; }

	private:
		static uint32 helper( const T* str ) noexcept
		{
			return helper( str, StringUtil::strlen( str ) );
		}

		/**
		 * @brief 헬퍼를 반환합니다
		 */
		static uint32 helper( const T* str, size_type length ) noexcept;

	public:
		struct AllocationInfo;
	private:
		struct StringKey;

		basic_hashed_string( AllocationInfo& info, const T* str ) noexcept
			: _stringKeyIndex{ helper_internal( info, str, StringUtil::strlen( str ) ) }
		{
		}

		/**
		 * @brief 내부 헬퍼를 반환합니다
		 */
		static uint32 helper_internal( AllocationInfo& info, const T* str, size_type length ) noexcept;

		/**
		 * @brief AllocationInfo을(를) 반환합니다
		 */
		static AllocationInfo& getAllocationInfo() noexcept;

		uint32 _stringKeyIndex;
	};

	template <typename T>
	bool operator==( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() == rhs.getIndex(); }

	template <typename T>
	bool operator!=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() != rhs.getIndex(); }

	template <typename T>
	bool operator<( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() < rhs.getIndex(); }

	template <typename T>
	bool operator<=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() <= rhs.getIndex(); }

	template <typename T>
	bool operator>( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() > rhs.getIndex(); }

	template <typename T>
	bool operator>=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() >= rhs.getIndex(); }

	using hashed_string = basic_hashed_string<char>;
	using hashed_wstring = basic_hashed_string<wchar_t>;

	/** @brief Core.dll 단독 소유 인턴 테이블 (모든 모듈이 이 export만 사용) */
	SW_API hashed_string::AllocationInfo*  getCoreHashedStringAllocationInfo() noexcept;
	SW_API hashed_wstring::AllocationInfo* getCoreHashedWStringAllocationInfo() noexcept;

}

namespace sw
{
	template <typename T, typename N>
	struct basic_hashed_string<T, N>::StringKey final
	{
		friend class basic_hashed_string<T>;

		StringKey( const bool bHasOwnership, hash_type hashValue, const value_type* str, const uint32 stringLength )
			: _bHasOwnership{ bHasOwnership }
			, _str{ str }
			, _stringLength{ stringLength }
			, _hash{ hashValue }
		{
		}

		~StringKey()
		{
			destroy();
		}

		StringKey( const StringKey& rhs )
		{
			*this = rhs;
		}

		StringKey& operator=( const StringKey& rhs )
		{
			if ( this == &rhs )
				return *this;

			SW_LOG_ASSERT( rhs._bHasOwnership == false, "ownership을 가진 경우 복사되면 안됩니다" );
			destroy();

			_str		  = rhs._str;
			_stringLength = rhs._stringLength;
			_hash		  = rhs._hash;

			return *this;
		}

		StringKey( StringKey&& rhs ) noexcept
		{
			*this = std::move( rhs );
		}

		StringKey& operator=( StringKey&& rhs ) noexcept
		{
			if ( this == &rhs )
				return *this;

			destroy();

			_bHasOwnership = true;
			_str		   = rhs._str;
			_stringLength  = rhs._stringLength;
			_hash		   = rhs._hash;

			rhs._str		   = nullptr;
			rhs._stringLength  = 0;
			rhs._bHasOwnership = false;
			rhs._hash		   = 0;

			return *this;
		}

		struct HashFunc
		{
			std::size_t operator()( const StringKey& key ) const
			{
				return key._hash;
			}
		};

	public:
		bool operator==( const StringKey& rhs ) const
		{
			const bool bHasEqualHash  = ( _hash == rhs._hash );
			const bool bHasSameLength = ( _stringLength == rhs._stringLength );
			const bool bHasSameChar	  = ( StringUtil::strnicmp( _str, rhs._str, _stringLength ) == 0 );
			const bool bSatisfyAll	  = ( bHasEqualHash == true && bHasSameLength == true && bHasSameChar == true );
			return bSatisfyAll;
		}

	private:
		void destroy()
		{
			if ( _bHasOwnership == true )
			{
				delete[] _str;
				_str = nullptr;
			}
			_stringLength  = 0;
			_hash		   = 0;
			_bHasOwnership = false;
		}

	private:
		bool			  _bHasOwnership = false;
		const value_type* _str			 = nullptr;
		size_type		  _stringLength	 = 0;
		hash_type		  _hash			 = 0;
	};

	template <typename T, typename N>
	struct basic_hashed_string<T, N>::AllocationInfo
	{
		friend class basic_hashed_string<T, N>;

		std::unordered_map<StringKey, uint32, typename StringKey::HashFunc> _mapKeyToIndex;
		std::vector<StringKey>												_keyList;
		std::shared_mutex													_mutex;

		AllocationInfo()
		{
			_keyList.reserve( std::numeric_limits<uint16>::max() );
			createPredefinedNameTypes();
		}

	private:
		void createPredefinedNameTypes()
		{

/** @brief REGISTER_NAME 매크로 정의입니다. */
#define REGISTER_NAME( index, name ) basic_hashed_string<T, N> predefined_##name{ *this, reinterpret_cast<const T*>( #name ) };
#include "Core/Utility/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
		}
	};

	template <typename T, typename N>
	uint32 basic_hashed_string<T, N>::helper_internal( AllocationInfo& info, const T* str, size_type length ) noexcept
	{
		hash_type hash{};
		if constexpr ( std::is_same_v<hash_type, uint32> )
			hash = StringUtil::computeHash32( str, length );
		else if constexpr ( std::is_same_v<hash_type, uint64> )
			hash = StringUtil::computeHash64( str, length );

		StringKey findStringKey{ false, hash, str, length };

		{
			/**
			 * @brief 읽기 잠금을 획득합니다
			 */
			std::shared_lock<std::shared_mutex> readLock{  info._mutex  };
			const auto							iter = info._mapKeyToIndex.find( findStringKey );
			if ( iter != info._mapKeyToIndex.end() )
				return iter->second;
		}

		/**
		 * @brief 쓰기 잠금을 획득합니다
		 */
		std::unique_lock<std::shared_mutex> writeLock{  info._mutex  };

		const auto iter = info._mapKeyToIndex.find( findStringKey );
		if ( iter != info._mapKeyToIndex.end() )
		{
			return iter->second;
		}

		const uint32 stringIndex = static_cast<uint32>( info._keyList.size() );

		value_type* pStr = new value_type[length + 1]{};

		std::char_traits<value_type>::copy( pStr, str, length );
		pStr[length] = static_cast<value_type>( 0 );

		StringKey newStringKey{ true, hash, pStr, length };
		StringKey weakStringKey{ false, hash, pStr, length };

		info._keyList.push_back( std::move( newStringKey ) );
		info._mapKeyToIndex.insert( { weakStringKey, stringIndex } );

		return stringIndex;
	}

	template <typename T, typename N>
	typename basic_hashed_string<T, N>::AllocationInfo& basic_hashed_string<T, N>::getAllocationInfo() noexcept
	{
		if constexpr ( std::is_same_v<T, char> )
			return *getCoreHashedStringAllocationInfo();
		else
			return *getCoreHashedWStringAllocationInfo();
	}

	template <typename T, typename N>
	typename basic_hashed_string<T, N>::size_type basic_hashed_string<T, N>::size() const noexcept
	{
		auto&								info = getAllocationInfo();
		/**
		 * @brief 내부 뮤텍스를 잠급니다
		 */
		std::shared_lock<std::shared_mutex> lock{  info._mutex  };
		return info._keyList[_stringKeyIndex]._stringLength;
	}

	template <typename T, typename N>
	const typename basic_hashed_string<T, N>::value_type* basic_hashed_string<T, N>::c_str() const noexcept
	{
		auto&								info = getAllocationInfo();
		/**
		 * @brief 내부 뮤텍스를 잠급니다
		 */
		std::shared_lock<std::shared_mutex> lock{  info._mutex  };
		return info._keyList[_stringKeyIndex]._str;
	}

	template <typename T, typename N>
	typename basic_hashed_string<T, N>::hash_type basic_hashed_string<T, N>::getHash() const noexcept
	{
		auto&								info = getAllocationInfo();
		/**
		 * @brief 내부 뮤텍스를 잠급니다
		 */
		std::shared_lock<std::shared_mutex> lock{  info._mutex  };
		return info._keyList[_stringKeyIndex]._hash;
	}

	template <typename T, typename N>
	uint32 basic_hashed_string<T, N>::helper( const T* str, const size_type length ) noexcept
	{
		auto& info = getAllocationInfo();
		return helper_internal( info, str, length );
	}
}

namespace std
{
	template <typename T, typename N>
	struct hash<sw::basic_hashed_string<T, N>>
	{
		std::size_t operator()( const sw::basic_hashed_string<T, N>& key ) const noexcept
		{
			return key.getHash();
		}
	};

	template <typename T, typename N>
	struct equal_to<sw::basic_hashed_string<T, N>>
	{
		bool operator()( const sw::basic_hashed_string<T, N>& lhs, const sw::basic_hashed_string<T, N>& rhs ) const noexcept
		{

			return lhs.getIndex() == rhs.getIndex();
		}
	};
}
