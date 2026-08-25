
#include "TypeNameMap.h"
#include "ParserDefines.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	namespace
	{
		/** @brief clang 수식어(const/class 등)와 참조를 제거합니다. */
		static string stripClangDecorations( std::string_view tView )
		{
			tView = StringUtil::trim( tView );
			for ( const utf8* prefix : kClangTypePrefixes )
			{
				const size_t prefixLen = StringUtil::strlen( prefix );
				while ( tView.size() >= prefixLen && tView.substr( 0, prefixLen ) == prefix )
				{
					tView.remove_prefix( prefixLen );
				}
			}

			tView = StringUtil::trim( tView );
			while ( tView.empty() == false && tView.back() == '&' )
			{
				tView.remove_suffix( 1 );
				tView = StringUtil::trim( tView );
			}
			return string( tView );
		}

	} // namespace

	TypeNameMap& TypeNameMap::instance()
	{
		static TypeNameMap s_map;
		return s_map;
	}

	void TypeNameMap::clear()
	{
		_aliasToCanonical.clear();
		_bLoaded = false;
	}

	void TypeNameMap::addKey( const string& key, const string& canonical )
	{
		if ( key.empty() )
			return;
		_aliasToCanonical.insert_or_assign( key, canonical );
	}

	void TypeNameMap::registerEntry( const string& canonical, const string& nameSpace,
									 const vector<string>& aliases )
	{
		if ( canonical.empty() )
			return;

		addKey( canonical, canonical );
		if ( nameSpace.empty() == false )
		{
			StringBuilder<constant::kMaxBuffer128> qualified;
			qualified.appendFormat( "%#::%#", nameSpace, canonical );
			addKey( string( qualified.view() ), canonical );
		}

		for ( const string& alias : aliases )
		{
			addKey( alias, canonical );
			if ( nameSpace.empty() == false )
			{
				StringBuilder<constant::kMaxBuffer128> qualified;
				qualified.appendFormat( "%#::%#", nameSpace, alias );
				addKey( string( qualified.view() ), canonical );
			}
		}
	}

	string TypeNameMap::normalize( const string& clangSpelling ) const
	{
		string t = stripClangDecorations( clangSpelling );
		if ( t.empty() )
			return t;

		const auto it = _aliasToCanonical.find( t );
		if ( it != _aliasToCanonical.end() )
			t = it->second;
		else
		{
			const size_t sep = t.rfind( "::" );
			if ( sep != string::npos && sep + 2 < t.size() )
			{
				const string bare	= t.substr( sep + 2 );
				const auto	 itBare = _aliasToCanonical.find( bare );
				if ( itBare != _aliasToCanonical.end() )
					t = itBare->second;
			}
		}

		return t;
	}

	string normalizeTypeName( const string& clangSpelling )
	{
		return TypeNameMap::instance().normalize( clangSpelling );
	}
} // namespace sw
