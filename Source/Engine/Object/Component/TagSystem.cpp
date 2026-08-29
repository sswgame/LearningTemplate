#include "pch.h"

#include "Engine/Object/Component/TagSystem.h"

#include "Core/String/hashed_string.h"

namespace sw
{
	TagID TagID::request( string_view str )
	{
		hashed_string hs{ str };

		uint64 hashValue = StringUtil::kOffset64;
		for ( size_t charIndex = 0; charIndex < str.length(); ++charIndex )
		{
			hashValue = ( hashValue ^ static_cast<uint64>( str[charIndex] ) ) * StringUtil::kPrime64;
		}

		return TagID{ hashValue, hs.c_str() };
	}

	TagContainer::TagContainer( std::initializer_list<TagID> tags )
		: _listTag{}
	{
		_listTag.reserve( tags.size() );
		_listTag.assign( tags.begin(), tags.end() );
		std::sort( _listTag.begin(), _listTag.end() );
		_listTag.erase( std::unique( _listTag.begin(), _listTag.end() ), _listTag.end() );
	}

	void TagContainer::addTag( TagID tag )
	{
		if ( tag.isValid() == false )
			return;

		auto it = std::lower_bound( _listTag.begin(), _listTag.end(), tag );
		if ( it == _listTag.end() || *it != tag )
		{
			const std::ptrdiff_t insertIndex = std::distance( _listTag.begin(), it );
			_listTag.insert( _listTag.begin() + insertIndex, tag );
		}
	}

	void TagContainer::removeTag( TagID tag )
	{
		auto it = std::lower_bound( _listTag.begin(), _listTag.end(), tag );
		if ( it != _listTag.end() && *it == tag )
			_listTag.erase( it );
	}

	bool TagContainer::hasTag( TagID tag, bool bExactMatch ) const
	{
		if ( bExactMatch )
			return std::binary_search( _listTag.begin(), _listTag.end(), tag );
		for ( const TagID& existingTag : _listTag )
		{
			if ( existingTag.isSubtagOf( tag ) || existingTag == tag )
				return true;
		}
		return false;
	}

	bool TagContainer::hasAllTags( const TagContainer& required ) const
	{
		for ( const TagID& reqTag : required._listTag )
		{
			if ( hasTag( reqTag, false ) == false )
				return false;
		}
		return true;
	}

	bool TagContainer::hasAnyTag( const TagContainer& other ) const
	{
		for ( const TagID& otherTag : other._listTag )
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

	TagQuery TagQuery::createAnyMatch( const TagContainer& tags )
	{
		TagQuery query;
		query._rootExpr = TagQueryExpr::anyTagsMatch( tags );
		return query;
	}

	TagQuery TagQuery::createAllMatch( const TagContainer& tags )
	{
		TagQuery query;
		query._rootExpr = TagQueryExpr::allTagsMatch( tags );
		return query;
	}

	TagQuery TagQuery::createNoMatch( const TagContainer& tags )
	{
		TagQuery query;
		query._rootExpr = TagQueryExpr::noTagsMatch( tags );
		return query;
	}

	TagQuery TagQuery::createExpression( const TagQueryExpr& expr )
	{
		TagQuery query;
		query._rootExpr = expr;
		return query;
	}

	bool TagQuery::matches( const TagContainer& container ) const
	{
		if ( _rootExpr._type == TagQueryExprType::Undefined )
			return true;
		return evalExpr( _rootExpr, container );
	}

	bool TagQuery::evalExpr( const TagQueryExpr& expr, const TagContainer& container )
	{
		switch ( expr._type )
		{
			case TagQueryExprType::Undefined:
				return true;
			case TagQueryExprType::AnyTagsMatch:
				return container.hasAnyTag( expr._tags );
			case TagQueryExprType::AllTagsMatch:
				return container.hasAllTags( expr._tags );
			case TagQueryExprType::NoTagsMatch:
				return container.hasAnyTag( expr._tags ) == false;
			case TagQueryExprType::AnyExprMatch:
			{
				if ( expr._listSubExpr.empty() )
					return true;
				for ( const TagQueryExpr& subExpr : expr._listSubExpr )
				{
					if ( evalExpr( subExpr, container ) )
						return true;
				}
				return false;
			}
			case TagQueryExprType::AllExprMatch:
			{
				if ( expr._listSubExpr.empty() )
					return true;
				for ( const TagQueryExpr& subExpr : expr._listSubExpr )
				{
					if ( evalExpr( subExpr, container ) == false )
						return false;
				}
				return true;
			}
			case TagQueryExprType::NotExprMatch:
			{
				if ( expr._listSubExpr.empty() )
					return true;
				return evalExpr( expr._listSubExpr[0], container ) == false;
			}
			default:
				return false;
		}
	}
} // namespace sw
