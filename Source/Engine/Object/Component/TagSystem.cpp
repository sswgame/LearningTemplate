#include "pch.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	class TagRegistryImpl
	{
	public:
		const utf8* intern( string_view str )
		{
			string								strKey( str );
			std::shared_lock<std::shared_mutex> readLock{ _mutex };
			auto								it = _uniquePool.find( strKey );
			if ( it != _uniquePool.end() )
				return it->data();

			readLock.unlock();
			std::unique_lock<std::shared_mutex> writeLock{ _mutex };
			auto [insertedIt, success] = _uniquePool.emplace( std::move( strKey ) );
			return insertedIt->data();
		}

	private:
		std::shared_mutex	  _mutex;
		unordered_set<string> _uniquePool;
	};

	TagID requestTag( string_view str )
	{
		static TagRegistryImpl s_impl;
		const utf8*			   pInterned = s_impl.intern( str );

		uint64 hashValue = StringUtil::kOffset64;
		for ( size_t charIndex = 0; charIndex < str.length(); ++charIndex )
		{
			hashValue = ( hashValue ^ static_cast<uint64>( str[charIndex] ) ) * StringUtil::kPrime64;
		}

		return TagID{ hashValue, pInterned };
	}

	TagContainer::TagContainer( std::initializer_list<TagID> tags )
		: _listTags{}
	{
		_listTags.reserve( tags.size() );
		_listTags.assign( tags.begin(), tags.end() );
		std::sort( _listTags.begin(), _listTags.end() );
		_listTags.erase( std::unique( _listTags.begin(), _listTags.end() ), _listTags.end() );
	}

	void TagContainer::addTag( TagID tag )
	{
		if ( tag.isValid() == false )
			return;

		auto it = std::lower_bound( _listTags.begin(), _listTags.end(), tag );
		if ( it == _listTags.end() || *it != tag )
		{
			const std::ptrdiff_t insertIndex = std::distance( _listTags.begin(), it );
			_listTags.insert( _listTags.begin() + insertIndex, tag );
		}
	}

	void TagContainer::removeTag( TagID tag )
	{
		auto it = std::lower_bound( _listTags.begin(), _listTags.end(), tag );
		if ( it != _listTags.end() && *it == tag )
			_listTags.erase( it );
	}

	bool TagContainer::hasTag( TagID tag, bool bExactMatch ) const
	{
		if ( bExactMatch )
			return std::binary_search( _listTags.begin(), _listTags.end(), tag );
		for ( const TagID& existingTag : _listTags )
		{
			if ( existingTag.isSubtagOf( tag ) || existingTag == tag )
				return true;
		}
		return false;
	}

	bool TagContainer::hasAllTags( const TagContainer& required ) const
	{
		for ( const TagID& reqTag : required._listTags )
		{
			if ( hasTag( reqTag, false ) == false )
				return false;
		}
		return true;
	}

	bool TagContainer::hasAnyTag( const TagContainer& other ) const
	{
		for ( const TagID& otherTag : other._listTags )
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
				if ( expr._listSubExprs.empty() )
					return true;
				for ( const TagQueryExpr& subExpr : expr._listSubExprs )
				{
					if ( evalExpr( subExpr, container ) )
						return true;
				}
				return false;
			}
			case TagQueryExprType::AllExprMatch:
			{
				if ( expr._listSubExprs.empty() )
					return true;
				for ( const TagQueryExpr& subExpr : expr._listSubExprs )
				{
					if ( evalExpr( subExpr, container ) == false )
						return false;
				}
				return true;
			}
			case TagQueryExprType::NotExprMatch:
			{
				if ( expr._listSubExprs.empty() )
					return true;
				return evalExpr( expr._listSubExprs[0], container ) == false;
			}
			default:
				return false;
		}
	}
} // namespace sw
