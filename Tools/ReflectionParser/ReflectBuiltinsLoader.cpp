#include "pch.h"

#include "ReflectionParser/ReflectBuiltinsLoader.h"

#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "ReflectionParser/ContainerTypeMap.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/TypeNameMap.h"

namespace sw
{
	namespace
	{
		/** @brief builtins TYPE 매크로 한 줄: canonical, C++ 타입, 별칭. */
		struct BuiltinTypeRow
		{
			string		   _canonical;
			string		   _cppType;
			vector<string> _listAliases;
		};

		/** @brief 매크로 호출 한 줄에서 인자 목록을 추출합니다. */
		static bool parseMacroLine( const string& line, const utf8* macroName, vector<string>& outArgs )
		{
			const size_t pos = line.find( macroName );
			if ( pos == string::npos )
				return false;
			const size_t open  = line.find( '(', pos );
			const size_t close = line.rfind( ')' );
			if ( open == string::npos || close == string::npos || close <= open )
				return false;
			outArgs = splitCommaRespectingAngles( line.substr( open + 1, close - open - 1 ) );
			return outArgs.empty() == false;
		}

		/** @brief 주석/전처리기를 건너뛰고 매크로 줄을 수집합니다. */
		static void collectMacroLines( const string_view text, vector<string>& outLineList )
		{
			const string_splitter lines( text, { "\r\n", "\n" } );
			for ( const string_view rawLine : lines.getSplitList() )
			{
				const string line = StringUtil::trim( string( rawLine ).c_str() );
				if ( line.empty() || line.front() == '#' || line.front() == '/' )
					continue;
				outLineList.push_back( line );
			}
		}

		static void registerBuiltinTypeLine( const vector<string>& args, uint32& typeCount )
		{
			if ( args.size() < 4 )
				return;

			string		   ns;
			vector<string> aliases;
			if ( args[3] != builtinMacroConstants::kSkipNamespace )
				ns = args[3];
			for ( size_t argIndex = 4; argIndex < args.size(); ++argIndex )
			{
				if ( args[argIndex] == builtinMacroConstants::kSkipAlias )
					continue;
				aliases.push_back( args[argIndex] );
			}
			TypeNameMap::instance().registerEntry( args[0], ns, aliases );
			++typeCount;
		}

		static void registerBuiltinContainerLine( const vector<string>& args, uint32& containerCount )
		{
			if ( args.size() < 3 )
				return;
			ContainerTypeMap::instance().registerRule( args[0], args[1], args[2] );
			++containerCount;
		}

		static void appendBuiltinTypeRow( const vector<string>& args, vector<BuiltinTypeRow>& outRows )
		{
			if ( args.size() < 4 )
				return;

			BuiltinTypeRow row;
			row._canonical = args[0];
			row._cppType   = args[1];
			if ( args[3] != builtinMacroConstants::kSkipNamespace )
			{
				StringBuilder<constant::kMaxBuffer128> qualified;
				qualified.appendFormat( "%#::%#", args[3], args[0] );
				row._listAliases.push_back( string( qualified.view() ) );
			}
			for ( size_t argIndex = 4; argIndex < args.size(); ++argIndex )
			{
				if ( args[argIndex] == builtinMacroConstants::kSkipAlias )
					continue;
				row._listAliases.push_back( args[argIndex] );
				if ( args[3] != builtinMacroConstants::kSkipNamespace && args[argIndex].find( "::" ) == string::npos &&
					 args[argIndex].find( ' ' ) == string::npos )
				{
					StringBuilder<constant::kMaxBuffer128> qualified;
					qualified.appendFormat( "%#::%#", args[3], args[argIndex] );
					row._listAliases.push_back( string( qualified.view() ) );
				}
			}
			outRows.push_back( std::move( row ) );
		}
	} // namespace

	bool loadReflectBuiltins( const string_view absPath )
	{
		string text;
		if ( FileUtil::readTextFile( absPath, text ) == false )
		{
			SW_LOG_WARNING( "[ReflectBuiltins] Failed to read: %#", absPath );
			return false;
		}

		TypeNameMap::instance().clear();
		ContainerTypeMap::instance().clear();

		uint32		   typeCount	  = 0;
		uint32		   containerCount = 0;
		vector<string> lineList;
		collectMacroLines( text, lineList );

		for ( const string& line : lineList )
		{
			vector<string> args;
			if ( line.rfind( builtinMacroConstants::kType, 0 ) == 0 &&
				 parseMacroLine( line, builtinMacroConstants::kType, args ) )
			{
				registerBuiltinTypeLine( args, typeCount );
			}
			else if ( line.rfind( builtinMacroConstants::kContainer, 0 ) == 0 &&
					  parseMacroLine( line, builtinMacroConstants::kContainer, args ) )
			{
				registerBuiltinContainerLine( args, containerCount );
			}
		}

		TypeNameMap::instance().setLoaded( true );
		ContainerTypeMap::instance().setLoaded( true );
		SW_LOG_INFO( "[ReflectBuiltins] types=%# containers=%# (%#)", typeCount, containerCount, absPath );
		return typeCount > 0 || containerCount > 0;
	}

	bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath )
	{
		string text;
		if ( FileUtil::readTextFile( builtinsAbsPath, text ) == false )
		{
			SW_LOG_WARNING( "[ReflectBuiltins] Failed to read: %#", builtinsAbsPath );
			return false;
		}

		vector<string>		   lineList;
		vector<BuiltinTypeRow> rows;
		collectMacroLines( text, lineList );
		for ( const string& line : lineList )
		{
			vector<string> args;
			if ( line.rfind( builtinMacroConstants::kType, 0 ) == 0 &&
				 parseMacroLine( line, builtinMacroConstants::kType, args ) )
			{
				appendBuiltinTypeRow( args, rows );
			}
		}

		if ( rows.empty() )
		{
			SW_LOG_WARNING( "[ReflectBuiltins] emit: no TYPE rows in %#", builtinsAbsPath );
			return false;
		}

		const EmitTemplateStore& tpls = EmitTemplateStore::instance();
		if ( tpls.isLoaded() == false || tpls.has( tplConstants::kBuiltinFileHeader ) == false ||
			 tpls.has( tplConstants::kBuiltinTypeRegistrar ) == false || tpls.has( tplConstants::kBuiltinFileFooter ) == false )
		{
			SW_LOG_ERROR( "[ReflectBuiltins] emit requires %# "
						  "(%# / %# / %#).",
						  cliConstants::kEmitTemplates, tplConstants::kBuiltinFileHeader, tplConstants::kBuiltinTypeRegistrar,
						  tplConstants::kBuiltinFileFooter );
			return false;
		}

		string out = tpls.render( tplConstants::kBuiltinFileHeader, {
																		{ "SourcePath", string( builtinsAbsPath ) }
		} );
		for ( const BuiltinTypeRow& row : rows )
		{
			StringBuilder<constant::kMaxBuffer1024> aliasRegs;
			for ( const string& alias : row._listAliases )
			{
				if ( alias.empty() || alias == row._canonical )
					continue;
				aliasRegs.appendFormat( "\t\t\tregistry.registerTypeAlias( \"%#\", \"%#\" );\n", alias, row._canonical );
			}

			StringBuilder<constant::kMaxBuffer128> id;
			id.appendFormat( "Builtin_%#", row._canonical );
			out += tpls.render( tplConstants::kBuiltinTypeRegistrar,
								{
									{		  "Id",		string( id.view() )},
									{	  "Name",			  row._canonical},
									{  "CppType",				 row._cppType},
									{"AliasRegs", string( aliasRegs.view() )}
			} );
		}
		out += tpls.render( tplConstants::kBuiltinFileFooter, {} );

		if ( FileUtil::writeTextFile( outCppAbsPath, out ) == false )
		{
			SW_LOG_ERROR( "[ReflectBuiltins] Failed to write %#", outCppAbsPath );
			return false;
		}

		SW_LOG_INFO( "[ReflectBuiltins] Emitted %# TYPE registrars → %#", static_cast<uint32>( rows.size() ),
					 outCppAbsPath );
		return true;
	}
} // namespace sw
