#include "pch.h"

#include "ReflectionParser/EmitTemplateStore.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"

SW_LOG_CALLER( "EmitTemplateStore" );
namespace sw
{
	namespace
	{
		/** @brief 식별자 시작 문자인지 판별합니다. */
		static bool isIdentStart( const utf8 c )
		{
			return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || c == '_';
		}

		/** @brief 식별자 중간 문자인지 판별합니다. */
		static bool isIdentChar( const utf8 c )
		{
			return isIdentStart( c ) || ( c >= '0' && c <= '9' );
		}

		/** @brief 변수 맵에서 키를 조회하고 없으면 빈 문자열을 반환합니다. */
		static string lookupVar( const unordered_map<string, string>& vars, const string& key )
		{
			const auto it = vars.find( key );
			return ( it != vars.end() ) ? it->second : string{};
		}

	} // namespace

	EmitTemplateStore& EmitTemplateStore::instance()
	{
		static EmitTemplateStore s_store;
		return s_store;
	}

	void EmitTemplateStore::clear()
	{
		_mapTemplate.clear();
		_bLoaded = false;
	}

	bool EmitTemplateStore::loadDirectory( const string_view absDir )
	{
		clear();

		if ( FileUtil::directoryExists( absDir ) == false )
		{
			SW_LOG_WARNING( "Not a directory: %#", absDir );
			return false;
		}

		vector<string> files;
		FileUtil::collectFiles( absDir, ParserContext::getSharedConfig().emitTemplateExtension, files, false, false );

		uint32 count = 0;
		for ( const string& path : files )
		{
			const string stem = FileUtil::removeExtension( FileUtil::getFileNamePart( path ) );
			string		 text;
			FileUtil::readTextFile( path, text );
			if ( text.empty() )
			{
				SW_LOG_WARNING( "Empty or unreadable: %#", path );
				continue;
			}
			_mapTemplate.insert_or_assign( stem, text );
			++count;
		}

		if ( count == 0 )
		{
			SW_LOG_WARNING( "No .tpl files in %#", absDir );
			return false;
		}

		_bLoaded = true;
		SW_LOG_TRACE( "Loaded %# templates from %#", count, absDir );
		return true;
	}

	bool EmitTemplateStore::has( const string_view name ) const
	{
		return _mapTemplate.find( string( name ) ) != _mapTemplate.end();
	}

	string EmitTemplateStore::render( const string_view					   name,
									  const unordered_map<string, string>& vars ) const
	{
		const auto it = _mapTemplate.find( string( name ) );
		if ( it == _mapTemplate.end() )
		{
			SW_LOG_WARNING( "Missing template: %#", name );
			return {};
		}
		return expand( it->second, vars );
	}

	string EmitTemplateStore::render( const string_view											 name,
									  std::initializer_list<std::pair<string_view, string_view>> vars ) const
	{
		const auto it = _mapTemplate.find( string( name ) );
		if ( it == _mapTemplate.end() )
		{
			SW_LOG_WARNING( "Missing template: %#", name );
			return {};
		}
		return expand( it->second, vars );
	}

	string EmitTemplateStore::expand( const string_view					   tpl,
									  const unordered_map<string, string>& vars )
	{
		string out;
		out.reserve( tpl.size() + 64 );

		for ( size_t charIndex = 0; charIndex < tpl.size(); )
		{
			if ( tpl[charIndex] != '$' )
			{
				out.push_back( tpl[charIndex++] );
				continue;
			}

			// 이스케이프: $$ → $
			if ( charIndex + 1 < tpl.size() && tpl[charIndex + 1] == '$' )
			{
				out.push_back( '$' );
				charIndex += 2;
				continue;
			}

			string_view key;
			size_t		keyEnd = charIndex + 1;
			if ( keyEnd < tpl.size() && tpl[keyEnd] == '{' )
			{
				++keyEnd;
				const size_t close = tpl.find( '}', keyEnd );
				if ( close == string_view::npos )
				{
					out.push_back( tpl[charIndex++] );
					continue;
				}
				key	   = tpl.substr( keyEnd, close - keyEnd );
				keyEnd = close + 1;
			}
			else if ( keyEnd < tpl.size() && isIdentStart( tpl[keyEnd] ) )
			{
				const size_t start = keyEnd;
				++keyEnd;
				while ( keyEnd < tpl.size() && isIdentChar( tpl[keyEnd] ) )
					++keyEnd;
				key = tpl.substr( start, keyEnd - start );
			}
			else
			{
				out.push_back( tpl[charIndex++] );
				continue;
			}

			out += lookupVar( vars, string( key ) );
			charIndex = keyEnd;
		}

		return out;
	}

	string EmitTemplateStore::expand( const string_view											 tpl,
									  std::initializer_list<std::pair<string_view, string_view>> vars )
	{
		string out;
		out.reserve( tpl.size() + 64 );

		for ( size_t charIndex = 0; charIndex < tpl.size(); )
		{
			if ( tpl[charIndex] != '$' )
			{
				out.push_back( tpl[charIndex++] );
				continue;
			}

			// 이스케이프: $$ → $
			if ( charIndex + 1 < tpl.size() && tpl[charIndex + 1] == '$' )
			{
				out.push_back( '$' );
				charIndex += 2;
				continue;
			}

			string_view key;
			size_t		keyEnd = charIndex + 1;
			if ( keyEnd < tpl.size() && tpl[keyEnd] == '{' )
			{
				++keyEnd;
				const size_t close = tpl.find( '}', keyEnd );
				if ( close == string_view::npos )
				{
					out.push_back( tpl[charIndex++] );
					continue;
				}
				key	   = tpl.substr( keyEnd, close - keyEnd );
				keyEnd = close + 1;
			}
			else if ( keyEnd < tpl.size() && isIdentStart( tpl[keyEnd] ) )
			{
				const size_t start = keyEnd;
				++keyEnd;
				while ( keyEnd < tpl.size() && isIdentChar( tpl[keyEnd] ) )
					++keyEnd;
				key = tpl.substr( start, keyEnd - start );
			}
			else
			{
				out.push_back( tpl[charIndex++] );
				continue;
			}

			for ( const auto& [k, v] : vars )
			{
				if ( k == key )
				{
					out.append( v.data(), v.size() );
					break;
				}
			}
			charIndex = keyEnd;
		}

		return out;
	}
} // namespace sw
