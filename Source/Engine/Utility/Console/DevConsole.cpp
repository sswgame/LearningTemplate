#include "pch.h"

#include "Engine/Utility/Console/DevConsole.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Log/Logger.h"
    #include "Core/Container/StringUtil.h"

    #include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );

    namespace
    {
        struct DevConsoleInternal
        {
            static constexpr const utf8* kArrBuiltin[] = { "get", "help", "set" };

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
                    while ( index < length && index < candidate.size() &&
                            StringUtil::toLowerChar( candidate[index] ) == StringUtil::toLowerChar( listCandidate[0][index] ) )
                    {
                        ++index;
                    }
                    length = index;
                }
                return length;
            }

            static DevConsoleResult readVariable( GlobalVariableManager* pVariables, string_view name, string& outReply )
            {
                GlobalVariableInfo* pInfo = pVariables != nullptr ? pVariables->findVariable( name ) : nullptr;
                if ( pInfo == nullptr )
                {
                    outReply = "unknown variable: " + string( name );
                    return DevConsoleResult::Failed;
                }
                outReply = string( name ) + " = " + pInfo->getValueAsString();
                return DevConsoleResult::Ok;
            }

            static DevConsoleResult writeVariable( GlobalVariableManager* pVariables, string_view name, string_view value, string& outReply )
            {
                GlobalVariableInfo* pInfo = pVariables != nullptr ? pVariables->findVariable( name ) : nullptr;
                if ( pInfo == nullptr )
                {
                    outReply = "unknown variable: " + string( name );
                    return DevConsoleResult::Failed;
                }
                if ( pInfo->setValueFromString( value ) == false )
                {
                    outReply = "could not set " + string( name ) + " to '" + string( value ) + "'";
                    return DevConsoleResult::Failed;
                }
                outReply = string( name ) + " = " + pInfo->getValueAsString();
                return DevConsoleResult::Ok;
            }

            static void appendHelp( string_view prefix, string& outReply )
            {
                outReply = "get <variable> | set <variable> <value> | <variable> [value] | help [prefix]";
                vector<string> listName;
                DevCommandRegistry::get().collectNames( prefix, listName );
                for ( const string& name : listName )
                {
                    const DevCommandRegistration* pCommand = DevCommandRegistry::get().findCommand( name );
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
        return engine::areEngineServicesBound() ? &engine::getGlobalVariableManager() : nullptr;
    }

    void DevConsole::tokenize( string_view line, vector<string>& outListToken )
    {
        outListToken.clear();
        string current;
        bool   bInQuote{ false };
        bool   bHasToken{ false };
        for ( const utf8 ch : line )
        {
            if ( ch == '"' )
            {
                bInQuote  = bInQuote == false;
                bHasToken = true;
                continue;
            }
            if ( ( ch == ' ' || ch == '\t' ) && bInQuote == false )
            {
                if ( bHasToken )
                    outListToken.push_back( std::move( current ) );
                current.clear();
                bHasToken = false;
                continue;
            }
            current += ch;
            bHasToken = true;
        }
        if ( bHasToken )
            outListToken.push_back( std::move( current ) );
    }

    DevConsoleResult DevConsole::execute( string_view line, string& outReply ) const
    {
        outReply.clear();
        vector<string> listToken;
        tokenize( line, listToken );
        if ( listToken.empty() )
            return DevConsoleResult::Empty;

        const string& head = listToken[0];
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

        if ( const DevCommandRegistration* pCommand = DevCommandRegistry::get().findCommand( head ) )
        {
            const vector<string> listArgument( listToken.begin() + 1, listToken.end() );
            if ( pCommand->_pFunc( listArgument, outReply ) )
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

        outReply = "unknown command: " + head + " (help lists the commands)";
        return DevConsoleResult::UnknownCommand;
    }

    DevConsoleResult DevConsole::submit( string_view line )
    {
        const string_view trimmed = StringUtil::trim( line );
        if ( trimmed.empty() )
            return DevConsoleResult::Empty;

        if ( _listHistory.empty() || _listHistory.back() != trimmed )
            _listHistory.push_back( string( trimmed ) );
        if ( _listHistory.size() > kMaxHistoryCount )
            _listHistory.erase( _listHistory.begin() );
        resetHistoryCursor();

        appendOutput( string( "> " ) + string( trimmed ), false );
        string                 reply;
        const DevConsoleResult result = execute( trimmed, reply );
        const bool             bError = result != DevConsoleResult::Ok;
        if ( reply.empty() == false )
            appendOutput( reply, bError );
        if ( bError )
            SW_LOG_WARNING( "> %# : %#", string( trimmed ).c_str(), reply.c_str() );
        else
            SW_LOG_INFO( "> %# : %#", string( trimmed ).c_str(), reply.c_str() );
        return result;
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
        tokenize( line, listToken );
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
            for ( const string& name : pVariables->collectVariableNames() )
            {
                if ( StringUtil::startsWith( name, partial, true ) )
                    outListCandidate.push_back( name );
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
            DevCommandRegistry::get().collectNames( partial, listCommand );
            outListCandidate.insert( outListCandidate.end(), listCommand.begin(), listCommand.end() );
            addVariables();
        }
        else if ( listToken.size() == 2 && ( StringUtil::equals( listToken[0], "set", true ) || StringUtil::equals( listToken[0], "get", true ) ) )
            addVariables();
        else if ( listToken.size() == 2 && StringUtil::equals( listToken[0], "help", true ) )
            DevCommandRegistry::get().collectNames( partial, outListCandidate );
        std::sort( outListCandidate.begin(), outListCandidate.end() );
    }

    bool DevConsole::complete( string& inoutLine, vector<string>& outListCandidate ) const
    {
        collectCompletions( inoutLine, outListCandidate );
        if ( outListCandidate.empty() )
            return false;

        vector<string> listToken;
        tokenize( inoutLine, listToken );
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

    const string* DevConsole::moveHistoryBack()
    {
        if ( _historyCursor == 0 || _listHistory.empty() )
            return nullptr;
        --_historyCursor;
        return &_listHistory[_historyCursor];
    }

    const string* DevConsole::moveHistoryForward()
    {
        static const string s_emptyLine{};
        if ( _historyCursor >= _listHistory.size() )
            return nullptr;
        ++_historyCursor;
        if ( _historyCursor == _listHistory.size() )
            return &s_emptyLine;
        return &_listHistory[_historyCursor];
    }
} // namespace sw

#endif
