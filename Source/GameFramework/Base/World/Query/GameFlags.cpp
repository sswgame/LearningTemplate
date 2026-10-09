#include "pch.h"

#include "GameFramework/Base/World/Query/GameFlags.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "GameFlags" );

    namespace
    {
        /** @brief 비교 연산자입니다. */
        enum class GameFlagsCompare : uint8
        {
            None = 0,
            Equal,
            NotEqual,
            Less,
            LessEqual,
            Greater,
            GreaterEqual
        };

        /** @brief 조건식 재귀 하강 파서입니다. 읽으면서 바로 평가하고, 오류가 나면 `_bError` 를 세웁니다. */
        struct GameFlagsInternal
        {
            static constexpr int32 kMaxLiteral = 100000000;

            string_view      _text{};
            const GameFlags* _pFlags{ nullptr };
            size_t           _position{ 0 };
            int32            _depth{ 0 };
            bool             _bError{ false };

            static bool isNameChar( utf8 value )
            {
                const bool bLetter = ( 'a' <= value && value <= 'z' ) || ( 'A' <= value && value <= 'Z' );
                const bool bDigit  = '0' <= value && value <= '9';
                return bLetter || bDigit || value == '_' || value == '.' || value == ':';
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

            /** @brief 다음이 @p token 이면 넘기고 true 입니다. */
            bool consume( string_view token )
            {
                skipSpace();
                if ( _text.substr( _position, token.size() ) != token )
                    return false;
                _position += token.size();
                return true;
            }

            GameFlagsCompare consumeCompare()
            {
                if ( consume( ">=" ) )
                    return GameFlagsCompare::GreaterEqual;
                if ( consume( "<=" ) )
                    return GameFlagsCompare::LessEqual;
                if ( consume( "==" ) )
                    return GameFlagsCompare::Equal;
                if ( consume( "!=" ) )
                    return GameFlagsCompare::NotEqual;
                if ( consume( ">" ) )
                    return GameFlagsCompare::Greater;
                if ( consume( "<" ) )
                    return GameFlagsCompare::Less;
                return GameFlagsCompare::None;
            }

            /** @brief 정수(앞에 `-` 가능)나 플래그 이름 하나의 값입니다. */
            int32 readOperand()
            {
                skipSpace();
                const size_t start     = _position;
                const bool   bNegative = _position < _text.size() && _text[_position] == '-';
                if ( bNegative )
                    ++_position;
                while ( _position < _text.size() && isNameChar( _text[_position] ) )
                {
                    ++_position;
                }
                const string_view token = _text.substr( start, _position - start );
                if ( token.empty() || ( bNegative && token.size() == 1 ) )
                {
                    _bError = true;
                    return 0;
                }
                const utf8 first = bNegative ? token[1] : token[0];
                if ( '0' <= first && first <= '9' )
                {
                    int32 value = 0;
                    for ( size_t index = bNegative ? 1 : 0; index < token.size(); ++index )
                    {
                        if ( token[index] < '0' || '9' < token[index] )
                        {
                            _bError = true;
                            return 0;
                        }
                        if ( value > kMaxLiteral / 10 )
                        {
                            _bError = true; // 넘침
                            return 0;
                        }
                        value = value * 10 + ( token[index] - '0' );
                    }
                    return bNegative ? -value : value;
                }
                if ( bNegative )
                {
                    _bError = true;
                    return 0;
                }
                return _pFlags != nullptr ? _pFlags->getFlag( hashed_string( token ) ) : 0;
            }

            /** @brief 피연산자 하나 또는 비교 하나입니다. */
            bool evaluateComparison()
            {
                const int32            lhs     = readOperand();
                const GameFlagsCompare compare = consumeCompare();
                if ( compare == GameFlagsCompare::None )
                    return lhs != 0;
                const int32 rhs = readOperand();
                switch ( compare )
                {
                    case GameFlagsCompare::Equal:
                        return lhs == rhs;
                    case GameFlagsCompare::NotEqual:
                        return lhs != rhs;
                    case GameFlagsCompare::Less:
                        return lhs < rhs;
                    case GameFlagsCompare::LessEqual:
                        return lhs <= rhs;
                    case GameFlagsCompare::Greater:
                        return lhs > rhs;
                    case GameFlagsCompare::GreaterEqual:
                        return lhs >= rhs;
                    case GameFlagsCompare::None:
                        return false;
                }
                return false;
            }

            bool evaluateUnary()
            {
                if ( ++_depth > GameFlags::kMaxConditionDepth )
                {
                    _bError = true;
                    return false;
                }
                bool bResult = false;
                skipSpace();
                const bool bNot = _position < _text.size() && _text[_position] == '!' && _text.substr( _position, 2 ) != "!=";
                if ( bNot )
                {
                    ++_position;
                    bResult = evaluateUnary() == false;
                }
                else if ( consume( "(" ) )
                {
                    bResult = evaluateOr();
                    if ( consume( ")" ) == false )
                        _bError = true;
                }
                else
                {
                    bResult = evaluateComparison();
                }
                --_depth;
                return bResult;
            }

            bool evaluateAnd()
            {
                bool bResult = evaluateUnary();
                while ( _bError == false && consume( "&&" ) )
                {
                    const bool bRight = evaluateUnary(); // 오류를 잡으려 오른쪽도 끝까지 읽는다
                    bResult           = bResult && bRight;
                }
                return bResult;
            }

            bool evaluateOr()
            {
                bool bResult = evaluateAnd();
                while ( _bError == false && consume( "||" ) )
                {
                    const bool bRight = evaluateAnd();
                    bResult           = bResult || bRight;
                }
                return bResult;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GameFlags::GameFlags()
        : _mapFlag{}
        , _revision{ 0 }
    {
    }

    void GameFlags::setFlag( const hashed_string& name, int32 value )
    {
        if ( name.empty() )
            return;
        const auto mapIter = _mapFlag.find( name );
        if ( value == 0 )
        {
            if ( mapIter == _mapFlag.end() )
                return;
            _mapFlag.erase( mapIter );
            ++_revision;
            return;
        }
        if ( mapIter != _mapFlag.end() )
        {
            if ( mapIter->second == value )
                return;
            mapIter->second = value;
        }
        else
        {
            _mapFlag[name] = value;
        }
        ++_revision;
    }

    int32 GameFlags::addFlag( const hashed_string& name, int32 delta )
    {
        const int32 value = getFlag( name ) + delta;
        setFlag( name, value );
        return value;
    }

    int32 GameFlags::getFlag( const hashed_string& name, int32 fallback ) const
    {
        const auto mapIter = _mapFlag.find( name );
        return mapIter != _mapFlag.end() ? mapIter->second : fallback;
    }

    bool GameFlags::hasFlag( const hashed_string& name ) const
    {
        return _mapFlag.find( name ) != _mapFlag.end();
    }

    bool GameFlags::clearFlag( const hashed_string& name )
    {
        const bool bHad = hasFlag( name );
        setFlag( name, 0 );
        return bHad;
    }

    void GameFlags::clear()
    {
        if ( _mapFlag.empty() )
            return;
        _mapFlag.clear();
        ++_revision;
    }

    bool GameFlags::evaluate( string_view expression ) const
    {
        bool bResult = false;
        return parseCondition( expression, *this, bResult ) && bResult;
    }

    bool GameFlags::parseCondition( string_view expression, const GameFlags& flags, bool& outResult )
    {
        outResult = false;
        GameFlagsInternal parser;
        parser._text   = expression;
        parser._pFlags = &flags;
        if ( parser.isAtEnd() )
        {
            outResult = true; // 조건 없음
            return true;
        }
        const bool bResult = parser.evaluateOr();
        if ( parser._bError || parser.isAtEnd() == false )
        {
            SW_LOG_WARNING( "Invalid flag condition '%#' near column %#", expression, static_cast<int32>( parser._position ) );
            return false;
        }
        outResult = bResult;
        return true;
    }

    void GameFlags::fillEntries( vector<GameFlagEntry>& outListEntry ) const
    {
        outListEntry.clear();
        outListEntry.reserve( _mapFlag.size() );
        for ( const auto& [name, value] : _mapFlag )
        {
            outListEntry.push_back( GameFlagEntry{ name, value } );
        }
        const HashedStringLexicalLess lexicalLess;
        std::sort( outListEntry.begin(), outListEntry.end(), [&]( const GameFlagEntry& lhs, const GameFlagEntry& rhs )
        {
            return lexicalLess( lhs._name, rhs._name );
        } );
    }

    void GameFlags::restoreEntries( const vector<GameFlagEntry>& listEntry )
    {
        _mapFlag.clear();
        for ( const GameFlagEntry& entry : listEntry )
        {
            if ( entry._name.empty() == false && entry._value != 0 )
                _mapFlag[entry._name] = entry._value;
        }
        ++_revision;
    }

    void GameFlags::writeState( Archive& outArchive ) const
    {
        vector<GameFlagEntry> listEntry;
        fillEntries( listEntry );
        outArchive << static_cast<uint32>( listEntry.size() );
        for ( const GameFlagEntry& entry : listEntry )
        {
            StateArchiveUtil::writeName( outArchive, entry._name );
            outArchive << entry._value;
        }
    }

    bool GameFlags::readState( Archive& archive )
    {
        uint32 count = 0;
        // 칸마다 이름 길이(4) + 값(4) 이상
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        vector<GameFlagEntry> listEntry;
        listEntry.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            GameFlagEntry entry;
            if ( StateArchiveUtil::readName( archive, entry._name ) == false )
                return false;
            archive >> entry._value;
            if ( archive.isError() )
                return false;
            listEntry.push_back( entry );
        }
        restoreEntries( listEntry );
        return true;
    }
} // namespace sw
