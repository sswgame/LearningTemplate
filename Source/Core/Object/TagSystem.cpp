/**
 * @file TagSystem.cpp
 * @brief TagSystem 구현
 */
#include "pch.h"
#include "TagSystem.h"

namespace sw
{
	TagContainer::TagContainer( std::initializer_list<TagID> tags )
	{
		_tags.reserve( tags.size() );
		_tags.assign( tags.begin(), tags.end() );
		std::sort( _tags.begin(), _tags.end() );
		_tags.erase( std::unique( _tags.begin(), _tags.end() ), _tags.end() );
	}

	void TagContainer::addTag( TagID tag )
	{
		if ( tag.isValid() == false )
			return;

		auto it = std::lower_bound( _tags.begin(), _tags.end(), tag );
		if ( it == _tags.end() || *it != tag )
		{
			const std::ptrdiff_t index = std::distance( _tags.begin(), it );
			_tags.insert( _tags.begin() + index, tag );
		}
	}

	void TagContainer::removeTag( TagID tag )
	{
		auto it = std::lower_bound( _tags.begin(), _tags.end(), tag );
		if ( it != _tags.end() && *it == tag )
		{
			_tags.erase( it );
		}
	}

	bool TagContainer::hasTag( TagID tag, bool bExactMatch ) const
	{
		if ( bExactMatch )
		{
			return std::binary_search( _tags.begin(), _tags.end(), tag );
		}
		for ( const TagID& existing : _tags )
		{
			if ( existing.isSubtagOf( tag ) || existing == tag )
				return true;
		}
		return false;
	}

	bool TagContainer::hasAllTags( const TagContainer& required ) const
	{
		for ( const TagID& reqTag : required._tags )
		{
			if ( hasTag( reqTag, false ) == false )
				return false;
		}
		return true;
	}

	bool TagContainer::hasAnyTag( const TagContainer& other ) const
	{
		for ( const TagID& otherTag : other._tags )
		{
			if ( hasTag( otherTag, false ) )
				return true;
		}
		return false;
	}

	bool TagContainer::matchTags( const TagContainer& required, const TagContainer& forbidden ) const
	{
		if ( hasAllTags( required ) == false )
			return false;

		if ( hasAnyTag( forbidden ) )
			return false;

		return true;
	}
}
