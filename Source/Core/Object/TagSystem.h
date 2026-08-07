#pragma once
/**
 * @file TagSystem.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/String/hashed_string.h"

namespace sw
{

	struct SW_API TagID
	{
		uint64 _id		   = 0;
		uint64 _parentHash = 0;

		constexpr TagID() = default;
		constexpr TagID( uint64 id, uint64 parentHash = 0 )
			: _id( id )
			, _parentHash( parentHash )
		{
		}

		constexpr bool operator==( const TagID& other ) const { return _id == other._id; }
		constexpr bool operator!=( const TagID& other ) const { return _id != other._id; }
		constexpr bool operator<( const TagID& other ) const { return _id < other._id; }

		constexpr bool isValid() const { return _id != 0; }

		constexpr bool isSubtagOf( const TagID& parentTag ) const
		{
			if ( _id == parentTag._id || ( _parentHash != 0 && _parentHash == parentTag._id ) )
				return true;
			return false;
		}
	};

	constexpr TagID operator""_tag( const utf8* str, size_t len )
	{
		uint64 hash		  = StringUtil::kOffset64;
		uint64 parentHash = 0;

		for ( size_t i = 0; i < len; ++i )
		{
			if ( str[i] == '.' )
			{
				parentHash = hash;
			}
			hash = ( hash ^ static_cast<uint64>( str[i] ) ) * StringUtil::kPrime64;
		}

		return TagID( hash, parentHash );
	}

	class SW_API TagContainer
	{
	public:
		TagContainer() = default;
		TagContainer( std::initializer_list<TagID> tags );

		void reserve( uint32 capacity ) { _tags.reserve( capacity ); }

		/**
		 * @brief addTag 처리를 수행합니다.
		 */
		void addTag( TagID tag );

		/**
		 * @brief removeTag 처리를 수행합니다.
		 */
		void removeTag( TagID tag );

		/**
		 * @brief hasTag 처리를 수행합니다.
		 */
		bool hasTag( TagID tag, bool bExactMatch = false ) const;

		/**
		 * @brief hasAllTags 처리를 수행합니다.
		 */
		bool hasAllTags( const TagContainer& required ) const;

		/**
		 * @brief hasAnyTag 처리를 수행합니다.
		 */
		bool hasAnyTag( const TagContainer& other ) const;

		/**
		 * @brief matchTags 처리를 수행합니다.
		 */
		bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const;

		uint32 getTagCount() const { return static_cast<uint32>( _tags.size() ); }

		void clear() { _tags.clear(); }

		const std::vector<TagID>& getTags() const { return _tags; }

	private:
		std::vector<TagID> _tags;
	};
}
