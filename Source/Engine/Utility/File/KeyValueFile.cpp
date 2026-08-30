#include "pch.h"

#include "Engine/Utility/File/KeyValueFile.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Resource/ResourceUtil.h"

namespace sw
{

	bool KeyValueFile::parse( string_view text, KeyValueMap& outMap, KeyValueParseOptions opt )
	{
		outMap.clear();
		forEachContentLine(
			text,
			[&]( string_view line )
		{
			if ( opt._bSkipSemicolonComments != 0 && line.front() == ';' )
				return;
			if ( opt._bSkipBracketSections != 0 && line.front() == '[' )
				return;

			const size_t eq = line.find( '=' );
			if ( eq == string_view::npos || eq == 0 )
				return;

			string_view key = StringUtil::trim( line.substr( 0, eq ) );
			string_view val = StringUtil::trim( line.substr( eq + 1 ) );
			if ( key.empty() )
				return;
			outMap.emplace( string( key ), string( val ) );
		},
			opt._commentChar );

		return outMap.empty() == false;
	}

	bool KeyValueFile::loadFile( string_view absPath, KeyValueMap& outMap, KeyValueParseOptions opt )
	{
		string text;
		if ( FileUtil::readTextFile( absPath, text ) == false )
			return false;
		return parse( text, outMap, opt );
	}

	bool KeyValueFile::loadResource( string_view relativePath, KeyValueMap& outMap, KeyValueParseOptions opt,
									 string* pOutAbsPath )
	{
		string text;
		string absPath;
		if ( ResourceUtil::readTextResource( relativePath, text, &absPath ) == false )
			return false;
		if ( pOutAbsPath != nullptr )
			*pOutAbsPath = std::move( absPath );
		return parse( text, outMap, opt );
	}

	bool KeyValueFile::loadPath( string_view path, KeyValueMap& outMap, KeyValueParseOptions opt, string* pOutAbsPath )
	{
		if ( path.empty() )
			return false;
		if ( FileUtil::fileExists( path ) )
		{
			if ( pOutAbsPath != nullptr )
				*pOutAbsPath = string{ path };
			return loadFile( path, outMap, opt );
		}
		return loadResource( path, outMap, opt, pOutAbsPath );
	}

	const utf8* KeyValueFile::get( const KeyValueMap& mapData, string_view key, const utf8* pFallback )
	{
		if ( key.empty() )
			return pFallback != nullptr ? pFallback : "";
		const KeyValueMap::const_iterator it = mapData.find( string( key ) );
		if ( it == mapData.end() )
			return pFallback;
		return it->second.c_str();
	}

	int32 KeyValueFile::getInt( const KeyValueMap& mapData, string_view key, int32 fallback )
	{
		const utf8* pV = get( mapData, key, nullptr );
		if ( pV == nullptr || pV[0] == '\0' )
			return fallback;
		int32 val{ fallback };
		StringUtil::parseInt( pV, val );
		return val;
	}

	float32 KeyValueFile::getFloat( const KeyValueMap& mapData, string_view key, float32 fallback )
	{
		const utf8* pV = get( mapData, key, nullptr );
		if ( pV == nullptr || pV[0] == '\0' )
			return fallback;
		float32 val{ fallback };
		StringUtil::parseFloat( pV, val );
		return val;
	}

	bool KeyValueFile::getBool( const KeyValueMap& mapData, string_view key, bool fallback )
	{
		const utf8* pV = get( mapData, key, nullptr );
		if ( pV == nullptr || pV[0] == '\0' )
			return fallback;
		return StringUtil::parseBool( pV, fallback );
	}

	string KeyValueFile::dump( const KeyValueMap& mapData, string_view headerComment, string_view sectionName )
	{
		string text;
		if ( headerComment.empty() == false )
		{
			if ( headerComment.front() != '#' )
				text += "# ";
			text.append( headerComment.data(), headerComment.size() );
			if ( text.empty() == false && text.back() != '\n' )
				text += '\n';
		}
		if ( sectionName.empty() == false )
		{
			text += '[';
			text.append( sectionName.data(), sectionName.size() );
			text += "]\n";
		}
		for ( const KeyValueMap::value_type& pair : mapData )
		{
			text += pair.first;
			text += '=';
			text += pair.second;
			text += '\n';
		}
		return text;
	}

	bool KeyValueFile::saveFile( string_view absPath, const KeyValueMap& mapData, string_view headerComment,
								 string_view sectionName )
	{
		if ( absPath.empty() )
			return false;
		return FileUtil::writeTextFile( absPath, dump( mapData, headerComment, sectionName ) );
	}
} // namespace sw
