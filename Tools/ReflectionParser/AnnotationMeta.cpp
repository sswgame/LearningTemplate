#include "pch.h"

#include "ReflectionParser/AnnotationMeta.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

namespace sw
{
	AnnotationMeta& AnnotationMeta::instance()
	{
		static AnnotationMeta s_meta;
		return s_meta;
	}

	void AnnotationMeta::clear()
	{
		_mapBare.clear();
		_mapKeys.clear();
		_bLoaded = false;
	}

	uint64 AnnotationMeta::hashScopeAndKey( string_view scope, string_view key ) noexcept
	{
		const uint64 scopeHash = StringUtil::computeHash64( scope, false );
		const uint64 delimHash = ( scopeHash ^ static_cast<uint64>( ':' ) ) * StringUtil::kPrime64;
		return StringUtil::computeHash64( key, false, delimHash );
	}

	void AnnotationMeta::addAlias( const string_view scope, const string_view alias, AnnotationBinding binding )
	{
		if ( alias.empty() )
			return;
		const uint64 hash = hashScopeAndKey( scope, alias );
		if ( binding._kind == AnnotationBinding::Kind::Flag || binding._kind == AnnotationBinding::Kind::NetRole )
			_mapBare.insert_or_assign( hash, std::move( binding ) );
		else
			_mapKeys.insert_or_assign( hash, std::move( binding ) );
	}

	bool AnnotationMeta::loadFile( const string_view absPath )
	{
		clear();

		string text;
		if ( FileUtil::readTextFile( absPath, text ) == false )
		{
			SW_LOG_WARNING( "[AnnotationMeta] Failed to read: %#", absPath );
			return false;
		}

		string currentScope;
		uint32 bindingCount = 0;

		const string_splitter lines( text, { "\r\n", "\n" } );
		for ( const string_view rawLine : lines.getSplitList() )
		{
			const string line = StringUtil::trim( string( rawLine ).c_str() );
			if ( line.empty() || line.front() == '#' || line.front() == ';' )
				continue;

			if ( line.front() == '[' && line.back() == ']' && line.size() >= 3 )
			{
				currentScope = StringUtil::trim( line.substr( 1, line.size() - 2 ).c_str() );
				continue;
			}
			if ( currentScope.empty() )
				continue;

			const size_t eq = line.find( '=' );
			if ( eq == string::npos )
				continue;

			const string left  = StringUtil::trim( line.substr( 0, eq ).c_str() );
			const string right = StringUtil::trim( line.substr( eq + 1 ).c_str() );
			const size_t dot   = left.find( '.' );
			if ( dot == string::npos || dot == 0 || dot + 1 >= left.size() )
			{
				SW_LOG_WARNING( "[AnnotationMeta] Expected kind.Field = aliases: %#", line );
				continue;
			}

			AnnotationBinding binding;
			if ( tryParseAnnotationKind( left.substr( 0, dot ), binding._kind ) == false )
			{
				SW_LOG_WARNING( "[AnnotationMeta] Unknown kind: %#", line );
				continue;
			}

			binding._field = left.substr( dot + 1 );
			const string_splitter aliases( right, { "," } );
			for ( const string_view aliasView : aliases.getSplitList() )
			{
				const string_view alias = StringUtil::trim( aliasView );
				if ( alias.empty() )
					continue;
				addAlias( currentScope, alias, binding );
				++bindingCount;
			}
		}

		_bLoaded = true;
		SW_LOG_INFO( "[AnnotationMeta] %# alias bindings (%#)", bindingCount, absPath );
		return true;
	}

	const AnnotationBinding* AnnotationMeta::findBare( const string_view scope,
													   const string_view token ) const
	{
		const auto it = _mapBare.find( hashScopeAndKey( scope, token ) );
		return ( it != _mapBare.end() ) ? &it->second : nullptr;
	}

	const AnnotationBinding* AnnotationMeta::findKey( const string_view scope,
													  const string_view key ) const
	{
		const auto it = _mapKeys.find( hashScopeAndKey( scope, key ) );
		return ( it != _mapKeys.end() ) ? &it->second : nullptr;
	}
} // namespace sw
