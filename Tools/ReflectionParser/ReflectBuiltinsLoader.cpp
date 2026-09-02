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

SW_LOG_CALLER( "ReflectBuiltinsLoader" );
namespace sw
{
    namespace
    {
        struct ReflectBuiltinsLoaderInternal
        {
            /** @brief builtins TYPE 매크로 한 줄: canonical, C++ 타입, 별칭. */
            struct BuiltinTypeRow
            {
                string         _canonical;
                string         _cppType;
                vector<string> _listAlias;
            };

            /** @brief 매크로 호출 한 줄에서 인자 목록을 추출합니다. */
            static bool parseMacroLine( const string& line, const utf8* pMacroName, vector<string>& outListMacroArgument )
            {
                const size_t pos = line.find( pMacroName );
                if ( pos == string::npos )
                    return false;
                const size_t open  = line.find( '(', pos );
                const size_t close = line.rfind( ')' );
                if ( open == string::npos || close == string::npos || close <= open )
                    return false;
                outListMacroArgument = ParserUtil::splitCommaRespectingAngles( line.substr( open + 1, close - open - 1 ) );
                return outListMacroArgument.empty() == false;
            }

            /** @brief 주석/전처리기를 건너뛰고 매크로 줄을 수집합니다. */
            static void collectMacroLines( const string_view text, vector<string>& outListLine )
            {
                const string_splitter lines( text, { "\r\n", "\n" } );
                for ( const string_view rawLine : lines.getSplitList() )
                {
                    const string_view line = StringUtil::trim( rawLine );
                    if ( line.empty() || line.front() == '#' || line.front() == '/' )
                        continue;
                    outListLine.emplace_back( line );
                }
            }

            static void registerBuiltinTypeLine( const vector<string>& listMacroArgument, uint32& typeCount )
            {
                if ( listMacroArgument.size() < 4 )
                    return;

                string         ns;
                vector<string> listAlias;
                if ( listMacroArgument[3] != builtinMacroConstants::kSkipNamespace )
                    ns = listMacroArgument[3];
                for ( size_t argIndex = 4; argIndex < listMacroArgument.size(); ++argIndex )
                {
                    if ( listMacroArgument[argIndex] == builtinMacroConstants::kSkipAlias )
                        continue;
                    listAlias.push_back( listMacroArgument[argIndex] );
                }
                TypeNameMap::instance().registerEntry( listMacroArgument[0], ns, listAlias );
                ++typeCount;
            }

            static void registerBuiltinContainerLine( const vector<string>& listMacroArgument, uint32& containerCount )
            {
                if ( listMacroArgument.size() < 3 )
                    return;
                ContainerTypeMap::instance().registerRule( listMacroArgument[0], listMacroArgument[1], listMacroArgument[2] );
                ++containerCount;
            }

            static void appendBuiltinTypeRow( const vector<string>& listMacroArgument, vector<BuiltinTypeRow>& outListRow )
            {
                if ( listMacroArgument.size() < 4 )
                    return;

                BuiltinTypeRow row;
                row._canonical = listMacroArgument[0];
                row._cppType   = listMacroArgument[1];
                if ( listMacroArgument[3] != builtinMacroConstants::kSkipNamespace )
                {
                    StringBuilder<constant::kMaxBuffer128> qualified;
                    qualified.appendFormat( "%#::%#", listMacroArgument[3], listMacroArgument[0] );
                    row._listAlias.push_back( string( qualified.view() ) );
                }
                for ( size_t argIndex = 4; argIndex < listMacroArgument.size(); ++argIndex )
                {
                    if ( listMacroArgument[argIndex] == builtinMacroConstants::kSkipAlias )
                        continue;
                    row._listAlias.push_back( listMacroArgument[argIndex] );
                    if ( listMacroArgument[3] != builtinMacroConstants::kSkipNamespace && listMacroArgument[argIndex].find( "::" ) == string::npos &&
                         listMacroArgument[argIndex].find( ' ' ) == string::npos )
                    {
                        StringBuilder<constant::kMaxBuffer128> qualified;
                        qualified.appendFormat( "%#::%#", listMacroArgument[3], listMacroArgument[argIndex] );
                        row._listAlias.push_back( string( qualified.view() ) );
                    }
                }
                outListRow.push_back( std::move( row ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool loadReflectBuiltins( const string_view absPath )
    {
        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to read builtins: %#", absPath );
            return false;
        }

        TypeNameMap::instance().clear();
        ContainerTypeMap::instance().clear();

        uint32         typeCount      = 0;
        uint32         containerCount = 0;
        vector<string> listLine;
        ReflectBuiltinsLoaderInternal::collectMacroLines( text, listLine );

        for ( const string& line : listLine )
        {
            vector<string> listMacroArgument;
            if ( StringUtil::startsWith( line, builtinMacroConstants::kType ) &&
                 ReflectBuiltinsLoaderInternal::parseMacroLine( line, builtinMacroConstants::kType, listMacroArgument ) )
            {
                ReflectBuiltinsLoaderInternal::registerBuiltinTypeLine( listMacroArgument, typeCount );
            }
            else if ( StringUtil::startsWith( line, builtinMacroConstants::kContainer ) &&
                      ReflectBuiltinsLoaderInternal::parseMacroLine( line, builtinMacroConstants::kContainer, listMacroArgument ) )
            {
                ReflectBuiltinsLoaderInternal::registerBuiltinContainerLine( listMacroArgument, containerCount );
            }
        }

        TypeNameMap::instance().setLoaded( true );
        ContainerTypeMap::instance().setLoaded( true );
        SW_LOG_TRACE( "types=%# containers=%# (%#)", typeCount, containerCount, absPath );
        return typeCount > 0 || containerCount > 0;
    }

    bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath )
    {
        string text;
        if ( FileUtil::readTextFile( builtinsAbsPath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to read: %#", builtinsAbsPath );
            return false;
        }

        vector<string>                                        listLine;
        vector<ReflectBuiltinsLoaderInternal::BuiltinTypeRow> listRow;
        ReflectBuiltinsLoaderInternal::collectMacroLines( text, listLine );
        for ( const string& line : listLine )
        {
            vector<string> listMacroArgument;
            if ( StringUtil::startsWith( line, builtinMacroConstants::kType ) &&
                 ReflectBuiltinsLoaderInternal::parseMacroLine( line, builtinMacroConstants::kType, listMacroArgument ) )
            {
                ReflectBuiltinsLoaderInternal::appendBuiltinTypeRow( listMacroArgument, listRow );
            }
        }

        if ( listRow.empty() )
        {
            SW_LOG_WARNING( "emit: no TYPE rows in %#", builtinsAbsPath );
            return false;
        }

        const EmitTemplateStore& tpls = EmitTemplateStore::instance();
        if ( tpls.isLoaded() == false || tpls.has( tplConstants::kBuiltinFileHeader ) == false ||
             tpls.has( tplConstants::kBuiltinTypeRegistrar ) == false || tpls.has( tplConstants::kBuiltinFileFooter ) == false )
        {
            SW_LOG_ERROR( "emit requires %# "
                          "(%# / %# / %#).",
                          cliConstants::kEmitTemplates, tplConstants::kBuiltinFileHeader, tplConstants::kBuiltinTypeRegistrar,
                          tplConstants::kBuiltinFileFooter );
            return false;
        }

        string out = tpls.render( tplConstants::kBuiltinFileHeader, {
                                                                        { templateKeyConstants::kSourcePath, builtinsAbsPath }
        } );
        for ( const ReflectBuiltinsLoaderInternal::BuiltinTypeRow& row : listRow )
        {
            StringBuilder<constant::kMaxBuffer1024> aliasRegs;
            for ( const string& alias : row._listAlias )
            {
                if ( alias.empty() || alias == row._canonical )
                    continue;
                aliasRegs.appendFormat( "\t\t\tregistry.registerTypeAlias( \"%#\", \"%#\" );\n", alias, row._canonical );
            }

            StringBuilder<constant::kMaxBuffer128> id;
            id.appendFormat( "Builtin_%#", row._canonical );
            out += tpls.render( tplConstants::kBuiltinTypeRegistrar,
                                {
                                    {       templateKeyConstants::kId,        id.view()},
                                    {     templateKeyConstants::kName,   row._canonical},
                                    {  templateKeyConstants::kCppType,     row._cppType},
                                    {templateKeyConstants::kAliasRegs, aliasRegs.view()}
            } );
        }
        out += tpls.render( tplConstants::kBuiltinFileFooter, {} );

        if ( FileUtil::writeTextFile( outCppAbsPath, out ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", outCppAbsPath );
            return false;
        }

        SW_LOG_TRACE( "Emitted %# TYPE registrars → %#", static_cast<uint32>( listRow.size() ),
                      outCppAbsPath );
        return true;
    }
} // namespace sw
