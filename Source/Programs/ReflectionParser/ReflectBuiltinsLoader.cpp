#include "pch.h"

#include "Programs/ReflectionParser/ReflectBuiltinsLoader.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/ContainerTypeMap.h"
#include "Programs/ReflectionParser/EmitTemplateStore.h"
#include "Programs/ReflectionParser/GeneratedFiles.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserUtil.h"
#include "Programs/ReflectionParser/TypeNameMap.h"

namespace sw
{
    namespace
    {
        struct ReflectBuiltinsLoaderInternal
        {
            /** @brief TYPE 줄의 칸 자리입니다(정규 이름 · C++ 타입 · 글 처리기 · 네임스페이스 · 위젯 · 모양 · 역할 · 별칭…). */
            static constexpr size_t kTextConvColumn   = 2;
            static constexpr size_t kNamespaceColumn  = 3;
            static constexpr size_t kWidgetColumn     = 4;
            static constexpr size_t kLayoutColumn     = 5;
            static constexpr size_t kRolesColumn      = 6;
            static constexpr size_t kFirstAliasColumn = 7;
            /** @brief TYPE 줄의 칸 수 하한(별칭 앞 칸 모두)입니다. 별칭이 없으면 `_` 를 둔다. */
            static constexpr size_t kMinTypeArgumentCount = kFirstAliasColumn + 1;
            /** @brief CONTAINER 줄의 칸 수 하한(매칭 이름 · 종류 · 래퍼 · 역할)입니다. */
            static constexpr size_t kMinContainerArgumentCount = 4;

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
                const size_t open                = line.find( '(', pos );
                const size_t close               = line.rfind( ')' );
                const bool   bInvalidMacroParens = open == string_view::npos || close == string_view::npos || close <= open;
                if ( bInvalidMacroParens )
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
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.FailedReadBuiltins", "Failed to read builtins: %#", absPath );
                    return false;
                }

                const string_splitter lines( text, { "\r\n", "\n" } );
                for ( const string_view rawLine : lines.getSplitList() )
                {
                    const string_view line         = StringUtil::trim( rawLine );
                    const bool        bSkippedLine = line.empty() || line.front() == '#' || line.front() == '/';
                    if ( bSkippedLine )
                        continue;

                    // 틀린 줄을 건너뛰면 그 타입 · 컨테이너가 조용히 빠져 직렬화가 엉뚱한 래퍼를 쓴다 - 줄 하나라도 틀리면 멈춘다.
                    BuiltinRow row;
                    size_t     minArgumentCount = 0;
                    bool       bParsed          = false;
                    if ( StringUtil::startsWith( line, builtinmacro::kType ) )
                    {
                        bParsed          = parseMacroLine( line, builtinmacro::kType, row._listArgument );
                        minArgumentCount = kMinTypeArgumentCount;
                    }
                    else if ( StringUtil::startsWith( line, builtinmacro::kContainer ) )
                    {
                        row._bContainer  = SW_TRUE;
                        bParsed          = parseMacroLine( line, builtinmacro::kContainer, row._listArgument );
                        minArgumentCount = kMinContainerArgumentCount;
                    }
                    else
                    {
                        continue;
                    }
                    if ( bParsed == false || row._listArgument.size() < minArgumentCount )
                    {
                        SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.MalformedBuiltinsLineNeeds", "%#: malformed builtins line (needs at least %# arguments): %#", absPath, static_cast<uint32>( minArgumentCount ), line );
                        return false;
                    }
                    outListRow.push_back( std::move( row ) );
                }
                return true;
            }

            /** @brief TYPE 줄의 네임스페이스 칸입니다. `-` 면 비어 있습니다. */
            static string_view getNamespace( const vector<string>& listArgument )
            {
                return listArgument[kNamespaceColumn] == builtinmacro::kSkipNamespace ? string_view{} : string_view( listArgument[kNamespaceColumn] );
            }

            /** @brief TYPE 줄의 별칭들입니다(역할 칸 다음부터). `_` 는 빈 자리 표시라 뺍니다. */
            static vector<string> getAliases( const vector<string>& listArgument )
            {
                vector<string> listAlias;
                for ( size_t argIndex = kFirstAliasColumn; argIndex < listArgument.size(); ++argIndex )
                {
                    if ( listArgument[argIndex] != builtinmacro::kSkipAlias )
                        listAlias.push_back( listArgument[argIndex] );
                }
                return listAlias;
            }

            /** @brief C++ 식별자 철자(영문자 · `_` 로 시작하고 영문자 · 숫자 · `_`)인지 봅니다. */
            static bool isIdentifier( const string_view text )
            {
                if ( text.empty() || StringUtil::isDigitChar( text.front() ) )
                    return false;
                for ( const utf8 character : text )
                {
                    const bool bLetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || character == '_';
                    if ( bLetter == false && StringUtil::isDigitChar( character ) == false )
                        return false;
                }
                return true;
            }

            /** @brief Layout 칸(`none` · `num` · `angle` · `f32(멤버, …)` · `i32(멤버, …)`)을 읽습니다. 모르는 철자 · 빈 멤버 목록 · 식별자가 아닌 멤버는 false 입니다. */
            [[nodiscard]] static bool parseLayout( const string_view text, BuiltinTypeTraits& outTraits )
            {
                if ( text == builtinmacro::kLayoutNone || text == builtinmacro::kLayoutNumber || text == builtinmacro::kLayoutAngle )
                {
                    outTraits._layout = text == builtinmacro::kLayoutNone   ? BuiltinLayoutKind::None
                                      : text == builtinmacro::kLayoutNumber ? BuiltinLayoutKind::Number
                                                                            : BuiltinLayoutKind::Angle;
                    return true;
                }
                const size_t open  = text.find( '(' );
                const bool   bCall = open != string_view::npos && text.back() == ')';
                if ( bCall == false )
                    return false;
                const string_view head = StringUtil::trim( text.substr( 0, open ) );
                if ( head == builtinmacro::kLayoutF32 )
                    outTraits._layout = BuiltinLayoutKind::Float32;
                else if ( head == builtinmacro::kLayoutI32 )
                    outTraits._layout = BuiltinLayoutKind::Int32;
                else
                    return false;
                const string_splitter members( text.substr( open + 1, text.size() - open - 2 ), { "," } );
                for ( const string_view member : members.getSplitList() )
                {
                    const string_view name = StringUtil::trim( member );
                    if ( isIdentifier( name ) == false )
                        return false;
                    outTraits._listMember.emplace_back( name );
                }
                return outTraits._listMember.empty() == false;
            }

            /** @brief Roles 칸(`None` 또는 `Interp` · `State` 를 `|` 로 이은 것)을 읽습니다. 모르는 이름 · 겹친 이름 · 다른 이름과 섞인 `None` 은 false 입니다. */
            [[nodiscard]] static bool parseRoles( const string_view text, BuiltinTypeTraits& outTraits )
            {
                if ( text == builtinmacro::kRoleNone )
                    return true;
                const string_splitter roles( text, builtinmacro::kRoleSeparator );
                for ( const string_view role : roles.getSplitList() )
                {
                    const string_view name = StringUtil::trim( role );
                    if ( name == builtinmacro::kRoleInterp && outTraits._bInterp == SW_FALSE )
                        outTraits._bInterp = SW_TRUE;
                    else if ( name == builtinmacro::kRoleState && outTraits._bState == SW_FALSE )
                        outTraits._bState = SW_TRUE;
                    else
                        return false;
                }
                return true;
            }

            /**
             * @brief TYPE 줄의 위젯 · 모양 · 역할 칸을 읽고 서로 맞는지 봅니다. 어긋나면 그 줄을 알리고 false 입니다(파서가 시작할 때 멈춘다).
             * @details Interp 는 섞는 길이 있는 모양(num · f32 · angle)에만, Widget=Packed 는 글 처리기 packed 에만(`PackedValueTraits` 가 있어야 성분으로 편집한다).
             *          같은 규칙을 엔진이 컴파일 때도 본다(`ReflectedValue.cpp`) — 파서가 먼저 멈춰 생성 코드가 틀린 표로 나가지 않게 한다.
             */
            [[nodiscard]] static bool readTraits( const string_view absPath, const vector<string>& listArgument, BuiltinTypeTraits& outTraits )
            {
                const string& canonical = listArgument[0];
                outTraits._bPacked      = listArgument[kTextConvColumn] == builtinmacro::kTextConvPacked ? SW_TRUE : SW_FALSE;
                if ( parseLayout( listArgument[kLayoutColumn], outTraits ) == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.BadLayout", "%#: '%#' has layout '%#' - use none, num, angle, f32(members) or i32(members)", absPath, canonical,
                                     listArgument[kLayoutColumn] );
                    return false;
                }
                if ( parseRoles( listArgument[kRolesColumn], outTraits ) == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.BadRoles", "%#: '%#' has roles '%#' - use None, or Interp and State joined by |", absPath, canonical,
                                     listArgument[kRolesColumn] );
                    return false;
                }
                const bool bInterpLayout = outTraits._layout == BuiltinLayoutKind::Number || outTraits._layout == BuiltinLayoutKind::Float32 || outTraits._layout == BuiltinLayoutKind::Angle;
                if ( outTraits._bInterp == SW_TRUE && bInterpLayout == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.InterpNeedsLayout", "%#: '%#' has the Interp role but layout '%#' - Interp needs num, f32 or angle", absPath, canonical,
                                     listArgument[kLayoutColumn] );
                    return false;
                }
                if ( listArgument[kWidgetColumn] == builtinmacro::kWidgetPacked && outTraits._bPacked == SW_FALSE )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.PackedWidgetNeedsPacked", "%#: '%#' uses the Packed widget but its text converter is '%#' - the Packed widget needs packed",
                                     absPath, canonical, listArgument[kTextConvColumn] );
                    return false;
                }
                return true;
            }

            /**
             * @brief CONTAINER 줄(매칭 이름 · 종류 · 래퍼 줄기 · 역할)을 읽습니다. 역할은 `None` 또는 `State` · `Fixed` 를 `|` 로 이은 것입니다.
             * @details State 는 시퀀스에만(상태 직렬화 · 해시가 원소 차례대로 싣는다), 종류는 Sequence · Map 만. 어긋나면 그 줄을 알리고 false 입니다.
             */
            [[nodiscard]] static bool readContainerRule( const string_view absPath, const vector<string>& listArgument, ContainerTypeRule& outRule )
            {
                outRule._match        = listArgument[0];
                outRule._type         = listArgument[2];
                const bool bKnownKind = tryParseContainerKind( listArgument[1], outRule._kind ) && outRule._kind != ContainerKind::None;
                if ( bKnownKind == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.BadContainerKind", "%#: container '%#' has kind '%#' - use Sequence or Map", absPath, outRule._match, listArgument[1] );
                    return false;
                }
                bool bRolesOk = true;
                if ( listArgument[3] != builtinmacro::kRoleNone )
                {
                    const string_splitter roles( listArgument[3], builtinmacro::kRoleSeparator );
                    for ( const string_view role : roles.getSplitList() )
                    {
                        const string_view name = StringUtil::trim( role );
                        if ( name == builtinmacro::kRoleState && outRule._bState == SW_FALSE )
                            outRule._bState = SW_TRUE;
                        else if ( name == builtinmacro::kRoleFixed && outRule._bFixed == SW_FALSE )
                            outRule._bFixed = SW_TRUE;
                        else
                            bRolesOk = false;
                    }
                }
                if ( bRolesOk == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.BadContainerRoles", "%#: container '%#' has roles '%#' - use None, or State and Fixed joined by |", absPath, outRule._match,
                                     listArgument[3] );
                    return false;
                }
                if ( outRule._bState == SW_TRUE && outRule._kind != ContainerKind::Sequence )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.ContainerStateNeedsSequence", "%#: container '%#' has the State role but is a %# - State needs a Sequence", absPath,
                                     outRule._match, listArgument[1] );
                    return false;
                }
                return true;
            }

            static string makeQualifiedName( const string_view nameSpace, const string_view name )
            {
                StringBuilder<constant::buffer::kMax128> qualified;
                qualified.appendFormat( "%#::%#", nameSpace, name );
                return string( qualified.view() );
            }

            /**
             * @brief 런타임 레지스트리에 올릴 별칭 목록입니다. 네임스페이스가 있으면 한정 이름도 함께 올립니다.
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
                ContainerTypeRule rule;
                if ( ReflectBuiltinsLoaderInternal::readContainerRule( absPath, listArgument, rule ) == false )
                    return false;
                if ( outSession._containerTypeMap.registerRule( rule ) == false )
                {
                    SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.BadCONTAINERRow", "%#: bad CONTAINER row '%#'", absPath, listArgument[0] );
                    return false;
                }
                ++containerCount;
            }
            else
            {
                BuiltinTypeTraits traits;
                if ( ReflectBuiltinsLoaderInternal::readTraits( absPath, listArgument, traits ) == false )
                    return false;
                outSession._typeNameMap.registerEntry( listArgument[0], string( ReflectBuiltinsLoaderInternal::getNamespace( listArgument ) ),
                                                       ReflectBuiltinsLoaderInternal::getAliases( listArgument ) );
                outSession._typeNameMap.registerTraits( listArgument[0], traits );
                ++typeCount;
            }
        }

        // C 고정 배열(`T _arr[N]`)은 표의 `array` 줄을 탄다(`ContainerTreeBuilder`) — 그 줄이 고정 시퀀스가 아니면 C 배열이 다른 래퍼로 실린다.
        const ContainerTypeRule* pFixedArray    = outSession._containerTypeMap.findByMatch( annotation::kFixedArrayTypeName );
        const bool               bFixedArrayRow = pFixedArray != nullptr && pFixedArray->_kind == ContainerKind::Sequence && pFixedArray->_bFixed == SW_TRUE;
        if ( bFixedArrayRow == false )
        {
            SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.MissingFixedArrayRow", "%#: needs a CONTAINER row '%#' with kind Sequence and the Fixed role (C arrays use it)", absPath,
                             annotation::kFixedArrayTypeName );
            return false;
        }

        outSession._typeNameMap.setLoaded( true );
        outSession._containerTypeMap.setLoaded( true );
        SW_LOG_TRACE_ID( "ReflectBuiltinsLoader.TypesContainers", "types=%# containers=%# (%#)", typeCount, containerCount, absPath );
        return typeCount > 0 || containerCount > 0;
    }

    bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath, const string_view moduleName, const ParserSession& session )
    {
        vector<ReflectBuiltinsLoaderInternal::BuiltinRow> listRow;
        if ( ReflectBuiltinsLoaderInternal::readRows( builtinsAbsPath, listRow ) == false )
            return false;

        // 골격은 시작할 때 모두 읽었다(`EmitTemplateStore::loadDirectory` 가 하나라도 없으면 멈춘다).
        const EmitTemplateStore& tpls = session._emitTemplateStore;

        string out       = tpls.render( EmitTemplate::BuiltinFileHeader, {
                                                                       {templatekey::kSourcePath,                   builtinsAbsPath},
                                                                       {    templatekey::kBanner, sw::config::kTextReflectionBanner}
        } );
        uint32 typeCount = 0;
        for ( const ReflectBuiltinsLoaderInternal::BuiltinRow& row : listRow )
        {
            if ( row._bContainer == SW_TRUE )
                continue;

            const string&                             canonical = row._listArgument[0];
            StringBuilder<constant::buffer::kMax1024> aliasRegs;
            for ( const string& alias : ReflectBuiltinsLoaderInternal::makeRegistryAliases( row._listArgument ) )
            {
                if ( alias.empty() || alias == canonical )
                    continue;
                aliasRegs.appendFormat( "\t\t\tregistry.registerTypeAlias( \"%#\", \"%#\" );\n", alias, canonical );
            }

            StringBuilder<constant::buffer::kMax128> id;
            id.appendFormat( "Builtin_%#", canonical );
            out += tpls.render( EmitTemplate::BuiltinTypeRegistrar,
                                {
                                    {        templatekey::kID,            id.view()},
                                    {      templatekey::kName,            canonical},
                                    {   templatekey::kCppType, row._listArgument[1]},
                                    { templatekey::kAliasRegs,     aliasRegs.view()},
                                    {templatekey::kModuleName,           moduleName}
            } );
            ++typeCount;
        }

        if ( typeCount == 0 )
        {
            SW_LOG_ERROR_ID( "ReflectBuiltinsLoader.EmitNoTYPERows", "emit: no TYPE rows in %#", builtinsAbsPath );
            return false;
        }
        out += tpls.render( EmitTemplate::BuiltinFileFooter, {} );

        if ( GeneratedFileUtil::writeIfChanged( string( outCppAbsPath ), out ) == false )
            return false;
        SW_LOG_TRACE_ID( "ReflectBuiltinsLoader.EmittedTYPERegistrars", "Emitted %# TYPE registrars → %#", typeCount, outCppAbsPath );
        return true;
    }
} // namespace sw
