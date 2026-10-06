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
#include "ReflectionParser/GeneratedFiles.h"
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
            /** @brief builtins 표의 매크로 한 줄입니다. */
            struct BuiltinRow
            {
                vector<string>         _listArgument;
                uint8                  _bContainer : 1; ///< SW_REFLECT_BUILTIN_CONTAINER 이면 1, TYPE 이면 0
                [[maybe_unused]] uint8 _reserved   : 7;

                BuiltinRow()
                    : _listArgument{}
                    , _bContainer{ SW_FALSE }
                    , _reserved{ 0 }
                {
                }
            };

            /** @brief 매크로 호출 한 줄에서 인자 목록을 추출합니다. */
            [[nodiscard]] static bool parseMacroLine( const string_view line, const utf8* pMacroName, vector<string>& outListMacroArgument )
            {
                const size_t pos = line.find( pMacroName );
                if ( pos == string_view::npos )
                    return false;
                const size_t open  = line.find( '(', pos );
                const size_t close = line.rfind( ')' );
                if ( open == string_view::npos || close == string_view::npos || close <= open )
                    return false;
                outListMacroArgument = ParserUtil::splitCommaRespectingAngles( line.substr( open + 1, close - open - 1 ) );
                return outListMacroArgument.empty() == false;
            }

            /**
             * @brief builtins 표를 읽어 TYPE · CONTAINER 줄을 적힌 순서대로 돌려줍니다. 주석 · 전처리 줄은 건너뜁니다.
             * @details 맵 채우기와 `ReflectBuiltins.gen.cpp` 쓰기가 함께 씁니다.
             */
            [[nodiscard]] static bool readRows( const string_view absPath, vector<BuiltinRow>& outListRow )
            {
                string text;
                if ( FileUtil::readTextFile( absPath, text ) == false )
                {
                    SW_LOG_WARNING( "Failed to read builtins: %#", absPath );
                    return false;
                }

                const string_splitter lines( text, { "\r\n", "\n" } );
                for ( const string_view rawLine : lines.getSplitList() )
                {
                    const string_view line = StringUtil::trim( rawLine );
                    if ( line.empty() || line.front() == '#' || line.front() == '/' )
                        continue;

                    BuiltinRow row;
                    if ( StringUtil::startsWith( line, builtinmacro::kType ) )
                    {
                        if ( parseMacroLine( line, builtinmacro::kType, row._listArgument ) )
                            outListRow.push_back( std::move( row ) );
                    }
                    else if ( StringUtil::startsWith( line, builtinmacro::kContainer ) )
                    {
                        row._bContainer = SW_TRUE;
                        if ( parseMacroLine( line, builtinmacro::kContainer, row._listArgument ) )
                            outListRow.push_back( std::move( row ) );
                    }
                }
                return true;
            }

            /** @brief TYPE 줄의 네임스페이스 칸입니다. `-` 면 비어 있습니다. */
            static string_view getNamespace( const vector<string>& listArgument )
            {
                return listArgument[3] == builtinmacro::kSkipNamespace ? string_view{} : string_view( listArgument[3] );
            }

            /** @brief TYPE 줄의 별칭들입니다(5번째 칸부터). `_` 는 빈 자리 표시라 뺍니다. */
            static vector<string> getAliases( const vector<string>& listArgument )
            {
                vector<string> listAlias;
                for ( size_t argIndex = 4; argIndex < listArgument.size(); ++argIndex )
                {
                    if ( listArgument[argIndex] != builtinmacro::kSkipAlias )
                        listAlias.push_back( listArgument[argIndex] );
                }
                return listAlias;
            }

            static string makeQualifiedName( const string_view nameSpace, const string_view name )
            {
                StringBuilder<constant::kMaxBuffer128> qualified;
                qualified.appendFormat( "%#::%#", nameSpace, name );
                return string( qualified.view() );
            }

            /**
             * @brief 런타임 등록부에 올릴 별칭 목록입니다. 네임스페이스가 있으면 한정 이름도 함께 올립니다.
             * @details 공백이 든 별칭(`unsigned int`)이나 이미 한정된 별칭(`sw::string`)은 한정하지 않습니다.
             */
            static vector<string> makeRegistryAliases( const vector<string>& listArgument )
            {
                const string_view nameSpace = getNamespace( listArgument );
                vector<string>    listAlias;
                if ( nameSpace.empty() == false )
                    listAlias.push_back( makeQualifiedName( nameSpace, listArgument[0] ) );
                for ( const string& alias : getAliases( listArgument ) )
                {
                    listAlias.push_back( alias );
                    const bool bQualifiable = nameSpace.empty() == false && alias.find( "::" ) == string::npos && alias.find( ' ' ) == string::npos;
                    if ( bQualifiable )
                        listAlias.push_back( makeQualifiedName( nameSpace, alias ) );
                }
                return listAlias;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool loadReflectBuiltins( const string_view absPath, ParserSession& outSession )
    {
        vector<ReflectBuiltinsLoaderInternal::BuiltinRow> listRow;
        if ( ReflectBuiltinsLoaderInternal::readRows( absPath, listRow ) == false )
            return false;

        outSession._typeNameMap.clear();
        outSession._containerTypeMap.clear();

        uint32 typeCount      = 0;
        uint32 containerCount = 0;
        for ( const ReflectBuiltinsLoaderInternal::BuiltinRow& row : listRow )
        {
            const vector<string>& listArgument = row._listArgument;
            if ( row._bContainer == SW_TRUE )
            {
                if ( listArgument.size() < 3 )
                    continue;
                outSession._containerTypeMap.registerRule( listArgument[0], listArgument[1], listArgument[2] );
                ++containerCount;
            }
            else
            {
                if ( listArgument.size() < 4 )
                    continue;
                outSession._typeNameMap.registerEntry( listArgument[0], string( ReflectBuiltinsLoaderInternal::getNamespace( listArgument ) ),
                                                       ReflectBuiltinsLoaderInternal::getAliases( listArgument ) );
                ++typeCount;
            }
        }

        outSession._typeNameMap.setLoaded( true );
        outSession._containerTypeMap.setLoaded( true );
        SW_LOG_TRACE( "types=%# containers=%# (%#)", typeCount, containerCount, absPath );
        return typeCount > 0 || containerCount > 0;
    }

    bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath, const ParserSession& session )
    {
        vector<ReflectBuiltinsLoaderInternal::BuiltinRow> listRow;
        if ( ReflectBuiltinsLoaderInternal::readRows( builtinsAbsPath, listRow ) == false )
            return false;

        const EmitTemplateStore& tpls = session._emitTemplateStore;
        if ( tpls.isLoaded() == false || tpls.has( templatefile::kBuiltinFileHeader ) == false ||
             tpls.has( templatefile::kBuiltinTypeRegistrar ) == false || tpls.has( templatefile::kBuiltinFileFooter ) == false )
        {
            SW_LOG_ERROR( "emit requires %# (%# / %# / %#).", cli::kEmitTemplates, templatefile::kBuiltinFileHeader,
                          templatefile::kBuiltinTypeRegistrar, templatefile::kBuiltinFileFooter );
            return false;
        }

        string out       = tpls.render( templatefile::kBuiltinFileHeader, {
                                                                        { templatekey::kSourcePath, builtinsAbsPath }
        } );
        uint32 typeCount = 0;
        for ( const ReflectBuiltinsLoaderInternal::BuiltinRow& row : listRow )
        {
            if ( row._bContainer == SW_TRUE || row._listArgument.size() < 4 )
                continue;

            const string&                           canonical = row._listArgument[0];
            StringBuilder<constant::kMaxBuffer1024> aliasRegs;
            for ( const string& alias : ReflectBuiltinsLoaderInternal::makeRegistryAliases( row._listArgument ) )
            {
                if ( alias.empty() || alias == canonical )
                    continue;
                aliasRegs.appendFormat( "\t\t\tregistry.registerTypeAlias( \"%#\", \"%#\" );\n", alias, canonical );
            }

            StringBuilder<constant::kMaxBuffer128> id;
            id.appendFormat( "Builtin_%#", canonical );
            out += tpls.render( templatefile::kBuiltinTypeRegistrar,
                                {
                                    {       templatekey::kId,            id.view()},
                                    {     templatekey::kName,            canonical},
                                    {  templatekey::kCppType, row._listArgument[1]},
                                    {templatekey::kAliasRegs,     aliasRegs.view()}
            } );
            ++typeCount;
        }

        if ( typeCount == 0 )
        {
            SW_LOG_WARNING( "emit: no TYPE rows in %#", builtinsAbsPath );
            return false;
        }
        out += tpls.render( templatefile::kBuiltinFileFooter, {} );

        if ( GeneratedFileUtil::writeIfChanged( string( outCppAbsPath ), out ) == false )
            return false;
        SW_LOG_TRACE( "Emitted %# TYPE registrars → %#", typeCount, outCppAbsPath );
        return true;
    }
} // namespace sw
