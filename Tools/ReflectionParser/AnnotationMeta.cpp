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

	void AnnotationMeta::addAlias( const string& scope, const string& alias, AnnotationBinding binding )
	{
		if ( alias.empty() )
			return;
		if ( binding._kind == AnnotationBinding::Kind::Flag || binding._kind == AnnotationBinding::Kind::NetRole )
			_mapBare[scope].insert_or_assign( alias, binding );
		else
			_mapKeys[scope].insert_or_assign( alias, binding );
	}

	bool AnnotationMeta::loadFile( const std::string_view absPath )
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
		for ( const std::string_view rawLine : lines.getSplitList() )
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
			for ( const std::string_view aliasView : aliases.getSplitList() )
			{
				const string alias = StringUtil::trim( string( aliasView ).c_str() );
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

	const AnnotationBinding* AnnotationMeta::findBare( const std::string_view scope,
													   const std::string_view token ) const
	{
		const auto scopeIt = _mapBare.find( string( scope ) );
		if ( scopeIt == _mapBare.end() )
			return nullptr;
		const auto it = scopeIt->second.find( string( token ) );
		return ( it != scopeIt->second.end() ) ? &it->second : nullptr;
	}

	const AnnotationBinding* AnnotationMeta::findKey( const std::string_view scope,
													  const std::string_view key ) const
	{
		const auto scopeIt = _mapKeys.find( string( scope ) );
		if ( scopeIt == _mapKeys.end() )
			return nullptr;
		const auto it = scopeIt->second.find( string( key ) );
		return ( it != scopeIt->second.end() ) ? &it->second : nullptr;
	}
} // namespace sw
