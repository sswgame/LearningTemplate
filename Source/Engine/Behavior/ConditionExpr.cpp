#include "pch.h"

#include "Engine/Behavior/ConditionExpr.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/Reflection.h"

#include <cmath>

namespace sw
{
    /** @brief 식 파서입니다. 읽으면서 후위 배열을 짓고, 틀리면 `_error` 에 첫 까닭을 적습니다. */
    struct ConditionExprInternal
    {
        /** @brief 등록 목록입니다(함수 정적 — 다른 TU · DLL 의 정적 등록자가 먼저 쓴다). 워커가 읽으므로 고치고 읽을 때 목록의 주인 잠금(`getOwnerMutex`)을 잡는다. */
        static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const ConditionEntry, "ConditionRegistry", RegistrationOrder::Insertion, true )

            static constexpr float64 kMaxLiteral = 100000000.0;

        string_view    _text{};
        const utf8*    _pBareCondition{ nullptr };
        ConditionExpr* _pExpr{ nullptr };
        string         _error{};
        size_t         _position{ 0 };
        int32          _depth{ 0 };

        static bool isNameChar( utf8 value )
        {
            const bool bLetter = ( 'a' <= value && value <= 'z' ) || ( 'A' <= value && value <= 'Z' );
            const bool bDigit  = '0' <= value && value <= '9';
            return bLetter || bDigit || value == '_' || value == '.' || value == ':';
        }

        bool hasError() const { return _error.empty() == false; }

        void fail( string_view reason )
        {
            if ( hasError() == false )
            {
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "%# near column %#", reason, static_cast<int32>( _position ) );
                _error = string( errorBuilder.view() );
            }
        }

        void skipSpace()
        {
            while ( _position < _text.size() && ( _text[_position] == ' ' || _text[_position] == '\t' || _text[_position] == '\r' || _text[_position] == '\n' ) )
            {
                ++_position;
            }
        }

        bool isAtEnd()
        {
            skipSpace();
            return _position >= _text.size();
        }

        bool consume( string_view token )
        {
            skipSpace();
            if ( _text.substr( _position, token.size() ) != token )
                return false;
            _position += token.size();
            return true;
        }

        void emit( ConditionOpCode opCode )
        {
            ConditionInstruction instruction;
            instruction._opCode = opCode;
            _pExpr->_listInstruction.push_back( instruction );
        }

        void emitNumber( float64 number )
        {
            ConditionInstruction instruction;
            instruction._opCode = ConditionOpCode::PushNumber;
            instruction._number = number;
            _pExpr->_listInstruction.push_back( instruction );
        }

        /** @brief 등록 줄 @p entry 를 부르는 명령을 더합니다. 인자는 지금 읽어 들고 있습니다. */
        void emitCall( const ConditionEntry& entry, const vector<string_view>& listToken )
        {
            InstancedStruct      args( *entry._pArgsOps );
            const expected<void> read = InstancedStructText::read( args.getInstance(), *args.getType(), listToken );
            if ( read.has_value() == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "'%#': %# (%#)", entry._pName, read.error().message(), read.error().detail() );
                fail( textBuilder.view() );
                return;
            }
            ConditionInstruction instruction;
            instruction._opCode    = ConditionOpCode::Call;
            instruction._function  = entry._function;
            instruction._pArgs     = args.getInstance();
            instruction._callIndex = static_cast<uint32>( _pExpr->_listArgs.size() ); // 호출마다 인자 하나를 쥐므로 쥔 수가 곧 글 순서다
            _pExpr->_listArgs.push_back( args.share() );
            _pExpr->_listInstruction.push_back( instruction );
        }

        /** @brief 수 글자를 읽습니다. 숫자 뒤에 글자가 붙으면(`3a`) · 너무 크면 오류입니다. */
        void readNumber( string_view token, bool bNegative )
        {
            float64 value    = 0.0;
            float64 fraction = 0.0;
            for ( const utf8 letter : token )
            {
                if ( letter == '.' && fraction == 0.0 )
                {
                    fraction = 1.0;
                    continue;
                }
                if ( letter < '0' || '9' < letter )
                {
                    fail( "a number has a stray letter" );
                    return;
                }
                const float64 digit = static_cast<float64>( letter - '0' );
                if ( fraction > 0.0 )
                {
                    fraction *= 0.1;
                    value += digit * fraction;
                }
                else
                {
                    value = value * 10.0 + digit;
                }
                if ( value > kMaxLiteral )
                {
                    fail( "a number is too large" );
                    return;
                }
            }
            emitNumber( bNegative ? -value : value );
        }

        /** @brief 호출 `이름( … )` 의 인자를 읽어 명령을 더합니다. 여는 괄호는 이미 넘겼습니다. */
        void readCall( string_view name )
        {
            const size_t closePos = _text.find( ')', _position );
            if ( closePos == string_view::npos )
            {
                fail( "a call has no closing parenthesis" );
                return;
            }
            const string_view argumentText = _text.substr( _position, closePos - _position );
            if ( argumentText.find( '(' ) != string_view::npos )
            {
                fail( "call arguments cannot nest" );
                return;
            }
            _position                    = closePos + 1;
            const ConditionEntry* pEntry = ConditionRegistry::find( name );
            if ( pEntry == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "unknown condition '%#'", name );
                fail( textBuilder.view() );
                return;
            }
            vector<string_view> listToken;
            InstancedStructText::splitArguments( argumentText, listToken );
            emitCall( *pEntry, listToken );
        }

        /** @brief 그냥 이름입니다. 인자 없는 등록 이름이면 그 조건이고, 아니면 지정한 조건에 이름을 넘깁니다. */
        void readBareName( string_view name )
        {
            const ConditionEntry* pEntry = ConditionRegistry::find( name );
            if ( pEntry != nullptr && pEntry->_pArgsOps->_pfnGetType()->getPropertiesWithBase().empty() )
            {
                emitCall( *pEntry, {} );
                return;
            }
            const ConditionEntry* pBare = _pBareCondition != nullptr ? ConditionRegistry::find( _pBareCondition ) : nullptr;
            if ( pBare == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "unknown name '%#'", name );
                fail( textBuilder.view() );
                return;
            }
            const vector<string_view> listToken{ name };
            emitCall( *pBare, listToken );
        }

        void parseOperand()
        {
            skipSpace();
            const size_t start     = _position;
            const bool   bNegative = _position < _text.size() && _text[_position] == '-';
            if ( bNegative )
                ++_position;
            const size_t tokenStart = _position;
            while ( _position < _text.size() && isNameChar( _text[_position] ) )
            {
                ++_position;
            }
            const string_view token = _text.substr( tokenStart, _position - tokenStart );
            if ( token.empty() )
            {
                _position = start;
                fail( "an operand is missing" );
                return;
            }
            if ( '0' <= token[0] && token[0] <= '9' )
            {
                readNumber( token, bNegative );
                return;
            }
            if ( bNegative )
            {
                fail( "'-' needs a number" );
                return;
            }
            if ( consume( "(" ) )
                readCall( token );
            else
                readBareName( token );
        }

        /** @brief 비교 연산자를 읽습니다. 긴 것부터 봅니다. 없으면 false 입니다. */
        bool consumeCompare( ConditionOpCode& outOp )
        {
            if ( consume( ">=" ) )
                outOp = ConditionOpCode::GreaterEqual;
            else if ( consume( "<=" ) )
                outOp = ConditionOpCode::LessEqual;
            else if ( consume( "==" ) )
                outOp = ConditionOpCode::Equal;
            else if ( consume( "!=" ) )
                outOp = ConditionOpCode::NotEqual;
            else if ( consume( ">" ) )
                outOp = ConditionOpCode::Greater;
            else if ( consume( "<" ) )
                outOp = ConditionOpCode::Less;
            else
                return false;
            return true;
        }

        /** @brief 괄호 하나 또는 피연산자 하나입니다. 괄호 안은 식 전체(`||` 부터)입니다. */
        void parsePrimary()
        {
            if ( consume( "(" ) )
            {
                if ( ++_depth > ConditionExpr::kMaxDepth )
                {
                    fail( "the expression nests too deep" );
                    return;
                }
                parseOr();
                if ( hasError() == false && consume( ")" ) == false )
                    fail( "a parenthesis is not closed" );
                --_depth;
                return;
            }
            parseOperand();
        }

        void parseTerm()
        {
            parsePrimary();
            while ( hasError() == false )
            {
                if ( consume( "*" ) )
                {
                    parsePrimary();
                    emit( ConditionOpCode::Multiply );
                }
                else if ( consume( "/" ) )
                {
                    parsePrimary();
                    emit( ConditionOpCode::Divide );
                }
                else
                {
                    return;
                }
            }
        }

        void parseAdditive()
        {
            parseTerm();
            while ( hasError() == false )
            {
                if ( consume( "+" ) )
                {
                    parseTerm();
                    emit( ConditionOpCode::Add );
                }
                else if ( consume( "-" ) )
                {
                    parseTerm();
                    emit( ConditionOpCode::Subtract );
                }
                else
                {
                    return;
                }
            }
        }

        void parseComparison()
        {
            parseAdditive();
            ConditionOpCode op{ ConditionOpCode::Equal };
            if ( hasError() == false && consumeCompare( op ) )
            {
                parseAdditive();
                emit( op );
            }
        }

        void parseUnary()
        {
            if ( ++_depth > ConditionExpr::kMaxDepth )
            {
                fail( "the expression nests too deep" );
                return;
            }
            skipSpace();
            const bool bNot = _position < _text.size() && _text[_position] == '!' && _text.substr( _position, 2 ) != "!=";
            if ( bNot )
            {
                ++_position;
                parseUnary();
                emit( ConditionOpCode::Not );
            }
            else
            {
                parseComparison(); // 괄호는 피연산자 자리(`parsePrimary`)에서 읽는다 — `(a || b) && c` 도 `20 * (1 - x)` 도 같은 길이다
            }
            --_depth;
        }

        void parseAnd()
        {
            parseUnary();
            while ( hasError() == false && consume( "&&" ) )
            {
                parseUnary();
                emit( ConditionOpCode::And );
            }
        }

        void parseOr()
        {
            parseAnd();
            while ( hasError() == false && consume( "||" ) )
            {
                parseAnd();
                emit( ConditionOpCode::Or );
            }
        }

        /** @brief 평가에 필요한 스택 깊이를 잽니다. */
        static bool canFitStack( const ConditionExpr& expr )
        {
            int32 depth = 0;
            for ( const ConditionInstruction& instruction : expr._listInstruction )
            {
                const bool bPush = instruction._opCode == ConditionOpCode::PushNumber || instruction._opCode == ConditionOpCode::Call;
                const bool bNot  = instruction._opCode == ConditionOpCode::Not;
                depth += bPush ? 1 : ( bNot ? 0 : -1 );
                if ( depth > ConditionExpr::kMaxStack )
                    return false;
            }
            return true;
        }

        static bool isTrue( float64 value )
        {
            return value != 0.0 && std::isnan( value ) == false;
        }

        static float64 toNumber( bool bValue )
        {
            return bValue ? 1.0 : 0.0;
        }

        static bool compareValues( ConditionOpCode op, float64 lhs, float64 rhs )
        {
            if ( std::isnan( lhs ) || std::isnan( rhs ) )
                return false; // 값이 없으면 어떤 비교도 참이 아니다(있는지는 따로 묻는다)
            const float64 distance = lhs > rhs ? lhs - rhs : rhs - lhs;
            switch ( op )
            {
                case ConditionOpCode::Equal:
                    return distance < ConditionExpr::kEqualTolerance;
                case ConditionOpCode::NotEqual:
                    return distance >= ConditionExpr::kEqualTolerance;
                case ConditionOpCode::Less:
                    return lhs < rhs;
                case ConditionOpCode::LessEqual:
                    return lhs <= rhs;
                case ConditionOpCode::Greater:
                    return lhs > rhs;
                case ConditionOpCode::GreaterEqual:
                    return lhs >= rhs;
                case ConditionOpCode::PushNumber:
                case ConditionOpCode::Call:
                case ConditionOpCode::Not:
                case ConditionOpCode::And:
                case ConditionOpCode::Or:
                case ConditionOpCode::Add:
                case ConditionOpCode::Subtract:
                case ConditionOpCode::Multiply:
                case ConditionOpCode::Divide:
                    return false;
            }
            return false;
        }

        /** @brief 산술 하나입니다. 값이 없음(NaN)은 그대로 퍼지고, 0 으로 나누면 값이 없음입니다. */
        static float64 computeArithmetic( ConditionOpCode op, float64 lhs, float64 rhs )
        {
            switch ( op )
            {
                case ConditionOpCode::Add:
                    return lhs + rhs;
                case ConditionOpCode::Subtract:
                    return lhs - rhs;
                case ConditionOpCode::Multiply:
                    return lhs * rhs;
                case ConditionOpCode::Divide:
                    return rhs == 0.0 ? std::nan( "" ) : lhs / rhs;
                case ConditionOpCode::PushNumber:
                case ConditionOpCode::Call:
                case ConditionOpCode::Not:
                case ConditionOpCode::And:
                case ConditionOpCode::Or:
                case ConditionOpCode::Equal:
                case ConditionOpCode::NotEqual:
                case ConditionOpCode::Less:
                case ConditionOpCode::LessEqual:
                case ConditionOpCode::Greater:
                case ConditionOpCode::GreaterEqual:
                    return std::nan( "" );
            }
            return std::nan( "" );
        }
    };
} // namespace sw

namespace sw
{
    void ConditionRegistry::registerCondition( const ConditionEntry* pEntry )
    {
        RegistrationList<const ConditionEntry>& registrations = ConditionExprInternal::getRegistrations();
        ScopedLock<mutex>                       lock{ registrations.getOwnerMutex() };
        (void)registrations.addLogged( pEntry, "Condition", pEntry->_pName ); // 거절은 공용 오류 로그가 알린다
    }

    void ConditionRegistry::unregisterCondition( const ConditionEntry* pEntry )
    {
        RegistrationList<const ConditionEntry>& registrations = ConditionExprInternal::getRegistrations();
        ScopedLock<mutex>                       lock{ registrations.getOwnerMutex() };
        (void)registrations.remove( pEntry ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    const ConditionEntry* ConditionRegistry::find( string_view name )
    {
        RegistrationList<const ConditionEntry>& registrations = ConditionExprInternal::getRegistrations();
        ScopedLock<mutex>                       lock{ registrations.getOwnerMutex() };
        return registrations.findByName( name );
    }

    void ConditionRegistry::collectNames( vector<string>& outListName )
    {
        RegistrationList<const ConditionEntry>& registrations = ConditionExprInternal::getRegistrations();
        ScopedLock<mutex>                       lock{ registrations.getOwnerMutex() };
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            outListName.push_back( registrations.getNameAt( index ) );
        }
    }

    ConditionRegistrar::ConditionRegistrar( const ConditionEntry* pEntry )
        : _pEntry{ pEntry }
    {
        ConditionRegistry::registerCondition( pEntry );
    }

    ConditionRegistrar::~ConditionRegistrar()
    {
        ConditionRegistry::unregisterCondition( _pEntry );
    }
} // namespace sw

namespace sw
{
    ConditionExpr::ConditionExpr()
        : _listInstruction{}
        , _listArgs{}
        , _text{}
    {
    }

    expected<void> ConditionExpr::tryCompile( string_view text, ConditionExpr& outExpr, const utf8* pBareCondition )
    {
        outExpr = ConditionExpr{};
        ConditionExpr         built;
        ConditionExprInternal parser;
        parser._text           = text;
        parser._pBareCondition = pBareCondition;
        parser._pExpr          = &built;
        if ( parser.isAtEnd() )
            return {}; // 조건 없음
        parser.parseOr();
        if ( parser.hasError() == false && parser.isAtEnd() == false )
            parser.fail( "unexpected text after the expression" );
        if ( parser.hasError() == false && ConditionExprInternal::canFitStack( built ) == false )
            parser.fail( "the expression is too large" );
        if ( parser.hasError() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "invalid condition expression", parser._error } );
        built._text = string( text );
        outExpr     = std::move( built );
        return {};
    }

    bool ConditionExpr::compile( string_view text, string_view sourceName, ConditionExpr& outExpr, const utf8* pBareCondition )
    {
        const expected<void> compiled = tryCompile( text, outExpr, pBareCondition );
        if ( compiled.has_value() )
            return true;
        SW_LOG_ERROR_ID( "ConditionExpr.Invalid", "%#: invalid condition '%#': %#", sourceName, text, compiled.error().detail() );
        return false;
    }

    float64 ConditionExpr::evaluateNumber( const GameplayContext& context ) const
    {
        using Internal = ConditionExprInternal;
        if ( _listInstruction.empty() )
            return 1.0;
        float64 arrStack[kMaxStack];
        int32   top = 0;
        for ( const ConditionInstruction& instruction : _listInstruction )
        {
            switch ( instruction._opCode )
            {
                case ConditionOpCode::PushNumber:
                {
                    arrStack[top++] = instruction._number;
                    break;
                }
                case ConditionOpCode::Call:
                {
                    // 첫 호출은 문맥 그대로, 그다음은 자리 번호를 섞은 씨앗이다 — 한 식의 확률 호출이 한 굴림을 나누지 않는다.
                    if ( instruction._callIndex == 0 )
                    {
                        arrStack[top++] = instruction._function( context, instruction._pArgs );
                    }
                    else
                    {
                        GameplayContext callContext = context;
                        callContext.mixCallSeed( instruction._callIndex );
                        arrStack[top++] = instruction._function( callContext, instruction._pArgs );
                    }
                    break;
                }
                case ConditionOpCode::Not:
                {
                    arrStack[top - 1] = Internal::toNumber( Internal::isTrue( arrStack[top - 1] ) == false );
                    break;
                }
                case ConditionOpCode::And:
                {
                    --top;
                    arrStack[top - 1] = Internal::toNumber( Internal::isTrue( arrStack[top - 1] ) && Internal::isTrue( arrStack[top] ) );
                    break;
                }
                case ConditionOpCode::Or:
                {
                    --top;
                    arrStack[top - 1] = Internal::toNumber( Internal::isTrue( arrStack[top - 1] ) || Internal::isTrue( arrStack[top] ) );
                    break;
                }
                case ConditionOpCode::Equal:
                case ConditionOpCode::NotEqual:
                case ConditionOpCode::Less:
                case ConditionOpCode::LessEqual:
                case ConditionOpCode::Greater:
                case ConditionOpCode::GreaterEqual:
                {
                    --top;
                    arrStack[top - 1] = Internal::toNumber( Internal::compareValues( instruction._opCode, arrStack[top - 1], arrStack[top] ) );
                    break;
                }
                case ConditionOpCode::Add:
                case ConditionOpCode::Subtract:
                case ConditionOpCode::Multiply:
                case ConditionOpCode::Divide:
                {
                    --top;
                    arrStack[top - 1] = Internal::computeArithmetic( instruction._opCode, arrStack[top - 1], arrStack[top] );
                    break;
                }
            }
        }
        return arrStack[top - 1];
    }

    bool ConditionExpr::evaluate( const GameplayContext& context ) const
    {
        return ConditionExprInternal::isTrue( evaluateNumber( context ) );
    }

    void ConditionExpr::appendCompare( ConditionOpCode op, float64 number )
    {
        ConditionInstruction push;
        push._opCode = ConditionOpCode::PushNumber;
        push._number = number;
        _listInstruction.push_back( push );
        ConditionInstruction compare;
        compare._opCode = op;
        _listInstruction.push_back( compare );
    }
} // namespace sw
