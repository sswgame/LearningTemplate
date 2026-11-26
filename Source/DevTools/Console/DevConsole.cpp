#include "pch.h"

#include "DevTools/Console/DevConsole.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Network/DebugReplyLine.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    namespace
    {
        struct DevConsoleInternal
        {
            static constexpr const utf8* kArrBuiltin[] = { "clear", "get", "help", "set" };

            /** @brief 낱말 하나를 이어 붙입니다(공백이 있으면 큰따옴표로 감쌉니다). */
            static void appendToken( string& inoutText, string_view token )
            {
                if ( inoutText.empty() == false )
                    inoutText += ' ';
                const bool bQuote = token.find( ' ' ) != string_view::npos;
                if ( bQuote )
                    inoutText += '"';
                inoutText += token;
                if ( bQuote )
                    inoutText += '"';
            }

            /** @brief @p first 부터 끝까지의 낱말을 공백으로 잇습니다(전역 변수에 쓸 값). */
            static string joinFrom( const vector<string>& listToken, size_t first )
            {
                string joined;
                for ( size_t index = first; index < listToken.size(); ++index )
                {
                    if ( joined.empty() == false )
                        joined += ' ';
                    joined += listToken[index];
                }
                return joined;
            }

            /** @brief 후보들의 공통 접두어(대소문자 무시)의 길이입니다. */
            static size_t computeCommonPrefixLength( const vector<string>& listCandidate )
            {
                if ( listCandidate.empty() )
                    return 0;
                size_t length = listCandidate[0].size();
                for ( const string& candidate : listCandidate )
                {
                    size_t index = 0;
                    while ( true )
                    {
                        const bool bCommonPrefixContinues = index < length && index < candidate.size() &&
                                                            StringUtil::toLowerChar( candidate[index] ) == StringUtil::toLowerChar( listCandidate[0][index] );
                        if ( bCommonPrefixContinues == false )
                            break;
                        ++index;
                    }
                    length = index;
                }
                return length;
            }

            /** @brief 비밀 값 변수(`GlobalSecret`)면 true 입니다. 모르는 이름이면 false 입니다. */
            static bool isSecretVariable( GlobalVariableManager* pVariables, string_view name )
            {
                const GlobalVariableInfo* pInfo = pVariables != nullptr ? pVariables->findVariable( name ) : nullptr;
                return pInfo != nullptr && pInfo->_type == GlobalVariableType::Secret;
            }

            /**
             * @brief 남길(기록 · 출력 · 로그) 줄입니다 — 비밀 인자를 받는 명령이면 그 자리를(`DevCommandRegistry::makeMaskedLine`), 비밀 값 변수에 쓰는 줄이면 값을 가린다.
             */
            static string makeMaskedLine( GlobalVariableManager* pVariables, string_view line )
            {
                const vector<string> listToken = StringUtil::splitQuotedTokens( line );
                if ( listToken.empty() )
                    return string( line );
                // 차례는 `execute` 와 같다 — 내장 set, 명령, 변수.
                const bool                    bSet     = StringUtil::equals( listToken[0], "set", true );
                const DevCommandRegistration* pCommand = bSet ? nullptr : engine::getSubsystem<DevCommandRegistry>().findCommand( listToken[0] );
                if ( pCommand != nullptr )
                    return DevCommandRegistry::makeMaskedLine( *pCommand, line );
                const size_t nameIndex = bSet ? 1 : 0;
                if ( listToken.size() <= nameIndex + 1 || isSecretVariable( pVariables, listToken[nameIndex] ) == false )
                    return string( line );
                StringBuilder<constant::buffer::kMax256> maskedLine;
                maskedLine.appendFormat( "%#%# %#", bSet ? "set " : "", listToken[nameIndex], GlobalSecret::kMask );
                return string( maskedLine.view() );
            }

            static DevConsoleResult readVariable( GlobalVariableManager* pVariables, string_view name, string& outReply )
            {
                GlobalVariableInfo*                      pInfo = pVariables != nullptr ? pVariables->findVariable( name ) : nullptr;
                StringBuilder<constant::buffer::kMax256> reply;
                if ( pInfo == nullptr )
                {
                    reply.appendFormat( "unknown variable: %#", name );
                    outReply = string( reply.view() );
                    return DevConsoleResult::Failed;
                }
                reply.appendFormat( "%# = %#", name, pInfo->getValueAsString() );
                outReply = string( reply.view() );
                return DevConsoleResult::Ok;
            }

            static DevConsoleResult writeVariable( GlobalVariableManager* pVariables, string_view name, string_view value, string& outReply )
            {
                GlobalVariableInfo*                      pInfo = pVariables != nullptr ? pVariables->findVariable( name ) : nullptr;
                StringBuilder<constant::buffer::kMax256> reply;
                if ( pInfo == nullptr )
                {
                    reply.appendFormat( "unknown variable: %#", name );
                    outReply = string( reply.view() );
                    return DevConsoleResult::Failed;
                }
                if ( pInfo->setValueFromString( value ) == false )
                {
                    reply.appendFormat( "could not set %# to '%#'", name, pInfo->_type == GlobalVariableType::Secret ? GlobalSecret::kMask : value );
                    outReply = string( reply.view() );
                    return DevConsoleResult::Failed;
                }
                reply.appendFormat( "%# = %#", name, pInfo->getValueAsString() );
                outReply = string( reply.view() );
                return DevConsoleResult::Ok;
            }

            static void appendHelp( string_view prefix, string& outReply )
            {
                outReply = "clear | get <variable> | set <variable> <value> | <variable> [value] | help [prefix]";
                vector<string> listName;
                engine::getSubsystem<DevCommandRegistry>().collectNames( prefix, listName );
                for ( const string& name : listName )
                {
                    const DevCommandRegistration* pCommand = engine::getSubsystem<DevCommandRegistry>().findCommand( name );
                    outReply += "\n  ";
                    outReply += pCommand->_pUsage;
                    outReply += " - ";
                    outReply += pCommand->_pHelp;
                }
            }
        };
    } // namespace

    DevConsole::DevConsole( GlobalVariableManager* pVariables )
        : _pVariables{ pVariables }
        , _listHistory{}
        , _listOutput{}
        , _historyCursor{ 0 }
    {
    }

    GlobalVariableManager* DevConsole::getVariables() const
    {
        if ( _pVariables != nullptr )
            return _pVariables;
        return engine::areEngineSubsystemsBound() ? &engine::getSubsystem<GlobalVariableManager>() : nullptr;
    }

    DevConsoleResult DevConsole::execute( string_view line, string& outReply ) const
    {
        outReply.clear();
        vector<string> listToken;
        listToken = StringUtil::splitQuotedTokens( line );
        if ( listToken.empty() )
            return DevConsoleResult::Empty;

        const string& head = listToken[0];
        // 출력 줄은 `submit` 이 비운다 — 줄을 들지 않는 길(시나리오 `<DevCommand>` · 원격 채널)에서는 비울 것이 없어 답 없이 성공이다.
        if ( StringUtil::equals( head, "clear", true ) )
            return DevConsoleResult::Ok;
        if ( StringUtil::equals( head, "help", true ) )
        {
            DevConsoleInternal::appendHelp( listToken.size() > 1 ? string_view( listToken[1] ) : string_view{}, outReply );
            return DevConsoleResult::Ok;
        }
        if ( StringUtil::equals( head, "get", true ) )
        {
            if ( listToken.size() != 2 )
            {
                outReply = "usage: get <variable>";
                return DevConsoleResult::Failed;
            }
            return DevConsoleInternal::readVariable( getVariables(), listToken[1], outReply );
        }
        if ( StringUtil::equals( head, "set", true ) )
        {
            if ( listToken.size() < 3 )
            {
                outReply = "usage: set <variable> <value>";
                return DevConsoleResult::Failed;
            }
            return DevConsoleInternal::writeVariable( getVariables(), listToken[1], DevConsoleInternal::joinFrom( listToken, 2 ), outReply );
        }

        if ( const DevCommandRegistration* pCommand = engine::getSubsystem<DevCommandRegistry>().findCommand( head ) )
        {
            const vector<string> listArgument( listToken.begin() + 1, listToken.end() );
            if ( DevCommandRegistry::run( *pCommand, listArgument, outReply ) )
                return DevConsoleResult::Ok;
            if ( outReply.empty() == false )
                outReply += "\n";
            outReply += "usage: ";
            outReply += pCommand->_pUsage;
            return DevConsoleResult::Failed;
        }

        GlobalVariableManager* pVariables = getVariables();
        if ( pVariables != nullptr && pVariables->findVariable( head ) != nullptr )
        {
            if ( listToken.size() == 1 )
                return DevConsoleInternal::readVariable( pVariables, head, outReply );
            return DevConsoleInternal::writeVariable( pVariables, head, DevConsoleInternal::joinFrom( listToken, 1 ), outReply );
        }

        StringBuilder<constant::buffer::kMax256> reply;
        reply.appendFormat( "unknown command: %# (help lists the commands)", head );
        outReply = string( reply.view() );
        return DevConsoleResult::UnknownCommand;
    }

    DevConsoleResult DevConsole::submit( string_view line )
    {
        const string_view trimmed = StringUtil::trim( line );
        if ( trimmed.empty() )
            return DevConsoleResult::Empty;

        // 친 줄은 기록 · 출력 · 로그 세 곳에 남는다 — 모두 비밀 인자를 가린 이 줄만 쓴다(실행은 원래 줄).
        const string maskedLine = DevConsoleInternal::makeMaskedLine( getVariables(), trimmed );
        if ( _listHistory.empty() || _listHistory.back() != maskedLine )
            _listHistory.push_back( maskedLine );
        if ( _listHistory.size() > kMaxHistoryCount )
            _listHistory.erase( _listHistory.begin() );
        resetHistoryCursor();

        // 내장 명령 clear(Quake · Source 콘솔과 같은 이름) — 친 줄과 답을 남기면 비운 화면에 다시 줄이 생기므로 여기서 끝낸다.
        if ( StringUtil::equals( trimmed, "clear", true ) )
        {
            clearOutput();
            return DevConsoleResult::Ok;
        }

        StringBuilder<constant::buffer::kMax256> echo;
        echo.appendFormat( "> %#", maskedLine );
        appendOutput( echo.view(), false );
        string                 reply;
        const DevConsoleResult result = execute( trimmed, reply );
        const bool             bError = result != DevConsoleResult::Ok;
        if ( reply.empty() == false )
            appendOutput( reply, bError );
        // 로그로 나가는 줄은 머리까지 포함해 한 줄 답의 상한을 넘지 않는다 - 개별 명령이 상한을 기억하지 않아도 되고 새 명령도 우회하지 못한다. 오버레이에는 전체가 남는다.
        const string logLine = makeLogLine( maskedLine, reply );
        if ( bError )
            SW_LOG_WARNING_ID( "DevCommand.Failed", "%#", logLine.c_str() ); // 시나리오 `<DevCommand>` 단계와 같은 사건 id 다(개발 명령의 답)
        else
            SW_LOG_INFO_ID( "DevCommand.Reply", "%#", logLine.c_str() );
        return result;
    }

    string DevConsole::makeLogLine( string_view command, string_view reply )
    {
        string head{ "> " };
        head += command;
        head += " : ";
        return DebugReplyLine::makeCapped( std::move( head ), [reply]( string& inoutText, size_t maxBytes )
        {
            DebugReplyLine::appendWithinBytes( inoutText, reply, maxBytes );
        } );
    }

    void DevConsole::appendOutput( string_view text, bool bError )
    {
        // 여러 줄 답은 줄마다 나눈다(오버레이가 한 줄씩 그린다).
        size_t begin = 0;
        while ( begin <= text.size() )
        {
            const size_t   end = text.find( '\n', begin );
            DevConsoleLine line{};
            line._text   = string( text.substr( begin, end == string_view::npos ? string_view::npos : end - begin ) );
            line._bError = bError ? SW_TRUE : SW_FALSE;
            _listOutput.push_back( std::move( line ) );
            if ( end == string_view::npos )
                break;
            begin = end + 1;
        }
        while ( _listOutput.size() > kMaxOutputCount )
        {
            _listOutput.erase( _listOutput.begin() );
        }
    }

    void DevConsole::collectCompletions( string_view line, vector<string>& outListCandidate ) const
    {
        outListCandidate.clear();
        vector<string> listToken;
        listToken            = StringUtil::splitQuotedTokens( line );
        const bool bNewToken = line.empty() == false && ( line.back() == ' ' || line.back() == '\t' );
        if ( bNewToken )
            listToken.emplace_back();
        if ( listToken.empty() )
            listToken.emplace_back();

        const string&          partial      = listToken.back();
        GlobalVariableManager* pVariables   = getVariables();
        const auto             addVariables = [pVariables, &partial, &outListCandidate]()
        {
            if ( pVariables == nullptr )
                return;
            vector<GlobalVariableInfo*> listVariable;
            pVariables->collectVariables( listVariable ); // 이름을 복사해 다시 찾지 않고 등록 항목을 한 번에 받는다
            for ( const GlobalVariableInfo* pInfo : listVariable )
            {
                if ( StringUtil::startsWith( pInfo->_name, partial, true ) )
                    outListCandidate.push_back( pInfo->_name );
            }
        };

        if ( listToken.size() == 1 )
        {
            for ( const utf8* pBuiltin : DevConsoleInternal::kArrBuiltin )
            {
                if ( StringUtil::startsWith( pBuiltin, partial, true ) )
                    outListCandidate.push_back( pBuiltin );
            }
            vector<string> listCommand;
            engine::getSubsystem<DevCommandRegistry>().collectNames( partial, listCommand );
            outListCandidate.insert( outListCandidate.end(), listCommand.begin(), listCommand.end() );
            addVariables();
        }
        else if ( listToken.size() == 2 && ( StringUtil::equals( listToken[0], "set", true ) || StringUtil::equals( listToken[0], "get", true ) ) )
            addVariables();
        else if ( listToken.size() == 2 && StringUtil::equals( listToken[0], "help", true ) )
            engine::getSubsystem<DevCommandRegistry>().collectNames( partial, outListCandidate );
        VectorUtil::sort( outListCandidate );
    }

    bool DevConsole::complete( string& inoutLine, vector<string>& outListCandidate ) const
    {
        collectCompletions( inoutLine, outListCandidate );
        if ( outListCandidate.empty() )
            return false;

        vector<string> listToken;
        listToken            = StringUtil::splitQuotedTokens( inoutLine );
        const bool bNewToken = inoutLine.empty() == false && ( inoutLine.back() == ' ' || inoutLine.back() == '\t' );
        if ( bNewToken || listToken.empty() )
            listToken.emplace_back();

        string replacement;
        if ( outListCandidate.size() == 1 )
            replacement = outListCandidate[0];
        else
            replacement = outListCandidate[0].substr( 0, DevConsoleInternal::computeCommonPrefixLength( outListCandidate ) );
        if ( replacement.size() < listToken.back().size() )
            return false;
        listToken.back() = replacement;

        string rebuilt;
        for ( const string& token : listToken )
        {
            DevConsoleInternal::appendToken( rebuilt, token );
        }
        if ( outListCandidate.size() == 1 )
            rebuilt += ' ';
        const bool bChanged = rebuilt != inoutLine;
        inoutLine           = std::move( rebuilt );
        return bChanged;
    }

    bool DevConsole::moveHistoryBack( string& outLine )
    {
        if ( _historyCursor == 0 || _listHistory.empty() )
            return false;
        --_historyCursor;
        outLine = _listHistory[_historyCursor];
        return true;
    }

    bool DevConsole::moveHistoryForward( string& outLine )
    {
        if ( _historyCursor >= _listHistory.size() )
            return false;
        ++_historyCursor;
        if ( _historyCursor == _listHistory.size() )
            outLine.clear();
        else
            outLine = _listHistory[_historyCursor];
        return true;
    }
} // namespace sw
