#include "pch.h"

#include "Engine/Localization/TextFormatter.h"

#include "Core/Container/set.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 패턴을 한 번 훑는 동안의 일입니다. 포맷 · 검사 · 이름 모으기 · 글자 조각 바꾸기가 같은 파서를 씁니다. */
        enum class TextWalkMode : uint8
        {
            Format = 0, ///< 고른 갈래만 풀어 쓴다
            Validate,   ///< 쓰지 않고 구문만 본다
            Rebuild     ///< 구문은 그대로, 글자 조각만 바꿔 모든 갈래를 다시 쓴다
        };

        /**
         * @class TextFormatterWalker
         * @brief ICU MessageFormat 부분 집합의 재귀 하강 파서입니다.
         */
        class TextFormatterWalker
        {
        public:
            static constexpr uint32 kMaxDepth = 16;

            TextFormatterWalker( string_view pattern, TextWalkMode mode )
                : _pattern{ pattern }
                , _error{}
                , _listPluralNumber{}
                , _uniqueName{}
                , _pArguments{ nullptr }
                , _pCulture{ nullptr }
                , _pfnMap{ nullptr }
                , _position{ 0 }
                , _mode{ mode }
            {
            }

            void bindFormat( const TextArgumentList* pArguments, const CultureInfo* pCulture )
            {
                _pArguments = pArguments;
                _pCulture   = pCulture;
            }
            void bindMap( TextLiteralMapFunc pfnMap ) { _pfnMap = pfnMap; }

            /** @brief 최상위 메시지를 훑습니다. 구문 오류가 없으면 true 입니다(인자 오류는 `_error` 에만 남긴다). */
            bool walk( string& outText )
            {
                const bool bParsed = walkMessage( 0, false, true, outText );
                if ( bParsed && _position < _pattern.size() )
                    return fail( "unmatched '}'" );
                return bParsed;
            }

            const string&      getError() const { return _error; }
            const set<string>& getNames() const { return _uniqueName; }

        private:
            bool fail( string_view reason )
            {
                if ( _error.empty() )
                    _error = string( reason ) + " at offset " + to_string( static_cast<uint64>( _position ) );
                return false;
            }

            /** @brief 인자 오류 — 구문은 맞으니 계속 훑는다. */
            void noteArgumentError( string_view reason )
            {
                if ( _error.empty() )
                    _error = string( reason );
            }

            static bool isSpace( utf8 character ) { return character == ' ' || character == '\t' || character == '\n' || character == '\r'; }
            static bool isNameCharacter( utf8 character )
            {
                return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) ||
                       character == '_' || character == '-' || character == ':' || character == '.';
            }

            void skipSpace()
            {
                while ( _position < _pattern.size() && isSpace( _pattern[_position] ) )
                {
                    ++_position;
                }
            }

            string_view readWord()
            {
                const size_t start = _position;
                while ( _position < _pattern.size() && isNameCharacter( _pattern[_position] ) )
                {
                    ++_position;
                }
                return _pattern.substr( start, _position - start );
            }

            /** @brief 글자 조각을 내보냅니다 — Rebuild 면 바꾼 원문, Format 이면 풀린 글자입니다. */
            void emitLiteral( bool bEmit, string_view rawText, string_view resolvedText, string& outText ) const
            {
                if ( bEmit == false || rawText.empty() )
                    return;
                if ( _mode == TextWalkMode::Rebuild )
                    outText.append( _pfnMap != nullptr ? _pfnMap( rawText ) : string( rawText ) );
                else if ( _mode == TextWalkMode::Format )
                    outText.append( resolvedText );
            }

            /**
             * @brief 메시지 하나(최상위 또는 갈래 몸통)를 `}` 또는 끝까지 훑습니다.
             * @param bInPlural `#` 이 숫자 자리인지(복수형 갈래 안)
             */
            bool walkMessage( uint32 depth, bool bInPlural, bool bEmit, string& outText )
            {
                if ( depth > kMaxDepth )
                    return fail( "message nesting is too deep" );
                string rawRun;
                string resolvedRun;
                while ( _position < _pattern.size() )
                {
                    const utf8 character = _pattern[_position];
                    if ( character == '}' )
                    {
                        if ( depth == 0 )
                            return fail( "unmatched '}'" );
                        break;
                    }
                    if ( character == '{' )
                    {
                        emitLiteral( bEmit, rawRun, resolvedRun, outText );
                        rawRun.clear();
                        resolvedRun.clear();
                        if ( walkArgument( depth, bEmit, outText ) == false )
                            return false;
                        continue;
                    }
                    if ( character == '#' && bInPlural )
                    {
                        emitLiteral( bEmit, rawRun, resolvedRun, outText );
                        rawRun.clear();
                        resolvedRun.clear();
                        if ( bEmit && _mode == TextWalkMode::Rebuild )
                            outText.push_back( '#' );
                        else if ( bEmit && _mode == TextWalkMode::Format && _listPluralNumber.empty() == false )
                            outText.append( _listPluralNumber.back() );
                        ++_position;
                        continue;
                    }
                    if ( character == '\'' )
                    {
                        const size_t start = _position;
                        walkQuote( bInPlural, resolvedRun );
                        rawRun.append( _pattern.substr( start, _position - start ) );
                        continue;
                    }
                    rawRun.push_back( character );
                    resolvedRun.push_back( character );
                    ++_position;
                }
                emitLiteral( bEmit, rawRun, resolvedRun, outText );
                return true;
            }

            /** @brief 작은따옴표 하나를 처리합니다. 풀린 글자는 @p inoutResolved 에 붙습니다. */
            void walkQuote( bool bInPlural, string& inoutResolved )
            {
                const size_t next = _position + 1;
                if ( next < _pattern.size() && _pattern[next] == '\'' )
                {
                    inoutResolved.push_back( '\'' );
                    _position += 2;
                    return;
                }
                const bool bStartsQuote = next < _pattern.size() && ( _pattern[next] == '{' || _pattern[next] == '}' || ( bInPlural && _pattern[next] == '#' ) );
                if ( bStartsQuote == false )
                {
                    inoutResolved.push_back( '\'' );
                    ++_position;
                    return;
                }
                _position = next;
                while ( _position < _pattern.size() )
                {
                    if ( _pattern[_position] == '\'' )
                    {
                        if ( _position + 1 < _pattern.size() && _pattern[_position + 1] == '\'' )
                        {
                            inoutResolved.push_back( '\'' );
                            _position += 2;
                            continue;
                        }
                        ++_position;
                        return;
                    }
                    inoutResolved.push_back( _pattern[_position] );
                    ++_position;
                }
            }

            /** @brief `{` 에서 시작하는 인자 하나를 짝 `}` 뒤까지 훑습니다. */
            bool walkArgument( uint32 depth, bool bEmit, string& outText )
            {
                const size_t openPos = _position;
                ++_position; // '{'
                skipSpace();
                const string_view name = readWord();
                if ( name.empty() )
                    return fail( "argument name expected" );
                _uniqueName.insert( string( name ) );
                skipSpace();
                if ( _position >= _pattern.size() )
                    return fail( "unterminated argument" );

                if ( _pattern[_position] == '}' )
                {
                    ++_position;
                    if ( bEmit && _mode == TextWalkMode::Rebuild )
                        outText.append( _pattern.substr( openPos, _position - openPos ) );
                    else if ( bEmit && _mode == TextWalkMode::Format )
                        appendSimpleArgument( name, outText );
                    return true;
                }
                if ( _pattern[_position] != ',' )
                    return fail( "',' or '}' expected after argument name" );
                ++_position;
                skipSpace();
                const string_view type = readWord();
                skipSpace();
                if ( type == "plural" || type == "select" )
                {
                    if ( _position >= _pattern.size() || _pattern[_position] != ',' )
                        return fail( "',' expected after plural/select" );
                    ++_position;
                    return walkOptions( depth, name, type == "plural", openPos, bEmit, outText );
                }

                string_view style;
                if ( _position < _pattern.size() && _pattern[_position] == ',' )
                {
                    ++_position;
                    skipSpace();
                    style = readWord();
                    skipSpace();
                }
                if ( _position >= _pattern.size() || _pattern[_position] != '}' )
                    return fail( "'}' expected after argument style" );
                ++_position;
                const bool bKnownType = type == "number" || type == "date" || type == "time";
                if ( bKnownType == false )
                    return fail( "unknown argument type '" + string( type ) + "'" );
                if ( bEmit && _mode == TextWalkMode::Rebuild )
                    outText.append( _pattern.substr( openPos, _position - openPos ) );
                else if ( bEmit && _mode == TextWalkMode::Format )
                    appendTypedArgument( name, type, style, outText );
                return true;
            }

            /** @brief plural · select 의 갈래들을 훑고, Format 이면 맞는 갈래 하나만 씁니다. */
            bool walkOptions( uint32 depth, string_view name, bool bPlural, size_t openPos, bool bEmit, string& outText )
            {
                const bool          bFormat   = _mode == TextWalkMode::Format && bEmit;
                const bool          bRebuild  = _mode == TextWalkMode::Rebuild && bEmit;
                const TextArgument* pArgument = ( bFormat && _pArguments != nullptr ) ? _pArguments->findArgument( name ) : nullptr;
                if ( bFormat && pArgument == nullptr )
                    noteArgumentError( "missing argument '" + string( name ) + "'" );

                skipSpace();
                if ( bRebuild )
                    outText.append( _pattern.substr( openPos, _position - openPos ) );

                float64 offset{ 0.0 };
                if ( bPlural && _pattern.substr( _position ).substr( 0, 7 ) == "offset:" )
                {
                    _position += 7;
                    skipSpace();
                    const string_view offsetText = readWord();
                    if ( StringUtil::parseDouble( offsetText, offset ) == false )
                        return fail( "offset number expected" );
                    if ( bRebuild )
                        outText.append( "offset:" ).append( offsetText ).append( " " );
                }

                vector<string> listSelector;
                vector<size_t> listBodyStart;
                while ( true )
                {
                    skipSpace();
                    if ( _position >= _pattern.size() )
                        return fail( "unterminated plural/select" );
                    if ( _pattern[_position] == '}' )
                    {
                        ++_position;
                        break;
                    }
                    const size_t selectorStart = _position;
                    if ( _pattern[_position] == '=' )
                        ++_position;
                    (void)readWord(); // 이름 글자를 건너뛸 뿐이다 — 고른 말은 아래 selector 로 다시 잘라 본다
                    const string_view selector = _pattern.substr( selectorStart, _position - selectorStart );
                    if ( selector.empty() || selector == "=" )
                        return fail( "plural/select keyword expected" );
                    const bool bExact = selector[0] == '=';
                    if ( bExact && bPlural == false )
                        return fail( "'=N' is only for plural" );
                    PluralCategory category{ PluralCategory::Other };
                    if ( bPlural && bExact == false && PluralRuleUtil::tryParseCategory( selector, category ) == false )
                        return fail( "unknown plural keyword '" + string( selector ) + "'" );
                    skipSpace();
                    if ( _position >= _pattern.size() || _pattern[_position] != '{' )
                        return fail( "'{' expected after keyword '" + string( selector ) + "'" );
                    ++_position;
                    listSelector.push_back( string( selector ) );
                    listBodyStart.push_back( _position );
                    if ( bRebuild )
                        outText.append( selector ).append( " {" );
                    if ( walkMessage( depth + 1, bPlural, bRebuild, outText ) == false )
                        return false;
                    if ( _position >= _pattern.size() || _pattern[_position] != '}' )
                        return fail( "'}' expected to close a branch" );
                    ++_position;
                    if ( bRebuild )
                        outText.append( "} " );
                }
                bool bHasOther{ false };
                for ( const string& selector : listSelector )
                {
                    bHasOther = bHasOther || selector == "other";
                }
                if ( bHasOther == false )
                    return fail( "plural/select needs an 'other' branch" );
                if ( bRebuild )
                {
                    while ( outText.empty() == false && outText.back() == ' ' )
                    {
                        outText.pop_back();
                    }
                    outText.push_back( '}' );
                }
                if ( bFormat == false || pArgument == nullptr )
                    return true;

                float64      value{ 0.0 };
                const size_t chosen = chooseBranch( *pArgument, bPlural, offset, listSelector, value );
                if ( chosen >= listSelector.size() )
                    return true;
                const size_t endPosition = _position;
                _position                = listBodyStart[chosen];
                if ( bPlural )
                    _listPluralNumber.push_back( formatPluralNumber( *pArgument, value - offset ) );
                const bool bWritten = walkMessage( depth + 1, bPlural, true, outText );
                if ( bPlural )
                    _listPluralNumber.pop_back();
                _position = endPosition;
                return bWritten;
            }

            /** @brief 쓸 갈래의 번호입니다 — 복수형은 `=N` → 범주 → other, select 는 값 → other. 인자가 맞지 않으면 other 입니다. */
            size_t chooseBranch( const TextArgument& argument, bool bPlural, float64 offset, const vector<string>& listSelector, float64& outValue )
            {
                string wanted;
                if ( bPlural )
                {
                    if ( argument._kind == TextArgumentKind::Integer )
                        outValue = static_cast<float64>( argument._integer );
                    else if ( argument._kind == TextArgumentKind::Decimal )
                        outValue = argument._decimal;
                    else
                        noteArgumentError( "plural argument '" + argument._name + "' must be a number" );
                    for ( size_t index = 0; index < listSelector.size(); ++index )
                    {
                        float64    exactValue{ 0.0 };
                        const bool bExact = listSelector[index][0] == '=' && StringUtil::parseDouble( string_view( listSelector[index] ).substr( 1 ), exactValue );
                        if ( bExact && exactValue == outValue )
                            return index;
                    }
                    wanted = categorySelector( argument, offset, outValue );
                }
                else if ( argument._kind == TextArgumentKind::Text || argument._kind == TextArgumentKind::Integer )
                    wanted = argument._text;
                else
                    noteArgumentError( "select argument '" + argument._name + "' must be text" );

                size_t otherIndex = listSelector.size();
                for ( size_t index = 0; index < listSelector.size(); ++index )
                {
                    if ( listSelector[index] == wanted )
                        return index;
                    if ( listSelector[index] == "other" )
                        otherIndex = index;
                }
                return otherIndex;
            }

            /** @brief 복수형 범주 키워드입니다(offset 을 뺀 값으로 고른다). */
            string categorySelector( const TextArgument& argument, float64 offset, float64 value ) const
            {
                if ( _pCulture == nullptr )
                    return "other";
                const float64        shown    = value - offset;
                const PluralOperands operands = argument._kind == TextArgumentKind::Integer
                                                  ? PluralOperands::makeInteger( static_cast<int64>( shown ) )
                                                  : PluralOperands::makeDecimal( shown, CultureInfo::countVisibleFraction( shown, 0, 3 ) );
                return PluralRuleUtil::getCategoryName( _pCulture->selectPlural( operands ) );
            }

            string formatPluralNumber( const TextArgument& argument, float64 shownValue ) const
            {
                if ( _pCulture == nullptr )
                    return {};
                if ( argument._kind == TextArgumentKind::Integer )
                    return _pCulture->formatInteger( static_cast<int64>( shownValue ) );
                return _pCulture->formatDecimal( shownValue, 0, 3 );
            }

            void appendSimpleArgument( string_view name, string& outText )
            {
                const TextArgument* pArgument = _pArguments != nullptr ? _pArguments->findArgument( name ) : nullptr;
                if ( pArgument == nullptr )
                {
                    noteArgumentError( "missing argument '" + string( name ) + "'" );
                    outText.append( "{" ).append( name ).append( "}" );
                    return;
                }
                switch ( pArgument->_kind )
                {
                    case TextArgumentKind::Text:
                    {
                        outText.append( pArgument->_text );
                        break;
                    }
                    case TextArgumentKind::Integer:
                    {
                        outText.append( _pCulture->formatInteger( pArgument->_integer ) );
                        break;
                    }
                    case TextArgumentKind::Decimal:
                    {
                        outText.append( _pCulture->formatDecimal( pArgument->_decimal, 0, 3 ) );
                        break;
                    }
                    case TextArgumentKind::DateTime:
                    {
                        outText.append( _pCulture->formatDateTime( pArgument->_dateTime, _pCulture->findDatePattern( "medium" ) ) );
                        break;
                    }
                }
            }

            void appendTypedArgument( string_view name, string_view type, string_view style, string& outText )
            {
                const TextArgument* pArgument = _pArguments != nullptr ? _pArguments->findArgument( name ) : nullptr;
                if ( pArgument == nullptr )
                {
                    noteArgumentError( "missing argument '" + string( name ) + "'" );
                    outText.append( "{" ).append( name ).append( "}" );
                    return;
                }
                if ( type == "number" )
                {
                    const bool bNumber = pArgument->_kind == TextArgumentKind::Integer || pArgument->_kind == TextArgumentKind::Decimal;
                    if ( bNumber == false )
                    {
                        noteArgumentError( "number argument '" + string( name ) + "' is not a number" );
                        outText.append( pArgument->_text );
                        return;
                    }
                    const float64 value = pArgument->_kind == TextArgumentKind::Integer ? static_cast<float64>( pArgument->_integer ) : pArgument->_decimal;
                    if ( style == "percent" )
                        outText.append( _pCulture->formatPercent( value ) );
                    else if ( style == "integer" )
                        outText.append( _pCulture->formatDecimal( value, 0, 0 ) );
                    else if ( style.empty() )
                        outText.append( pArgument->_kind == TextArgumentKind::Integer ? _pCulture->formatInteger( pArgument->_integer ) : _pCulture->formatDecimal( value, 0, 3 ) );
                    else
                        noteArgumentError( "unknown number style '" + string( style ) + "'" );
                    return;
                }
                if ( pArgument->_kind != TextArgumentKind::DateTime )
                {
                    noteArgumentError( "date/time argument '" + string( name ) + "' is not a date" );
                    return;
                }
                const string& pattern = type == "date" ? _pCulture->findDatePattern( style ) : _pCulture->findTimePattern( style );
                if ( pattern.empty() )
                {
                    noteArgumentError( "unknown date/time style '" + string( style ) + "'" );
                    return;
                }
                outText.append( _pCulture->formatDateTime( pArgument->_dateTime, pattern ) );
            }

        private:
            string_view             _pattern;
            string                  _error;
            vector<string>          _listPluralNumber; ///< 바깥에서 안쪽으로 — `#` 은 가장 안쪽 복수형의 수
            set<string>             _uniqueName;
            const TextArgumentList* _pArguments;
            const CultureInfo*      _pCulture;
            TextLiteralMapFunc      _pfnMap;
            size_t                  _position;
            TextWalkMode            _mode;
        };
    } // namespace
} // namespace sw

namespace sw
{
    TextArgument& TextArgumentList::getOrAddArgument( string_view name )
    {
        for ( TextArgument& argument : _listArgument )
        {
            if ( argument._name == name )
                return argument;
        }
        TextArgument& added = _listArgument.emplace_back();
        added._name         = string( name );
        return added;
    }

    TextArgumentList& TextArgumentList::addText( string_view name, string_view value )
    {
        TextArgument& argument = getOrAddArgument( name );
        argument._kind         = TextArgumentKind::Text;
        argument._text         = string( value );
        return *this;
    }

    TextArgumentList& TextArgumentList::addInteger( string_view name, int64 value )
    {
        TextArgument& argument = getOrAddArgument( name );
        argument._kind         = TextArgumentKind::Integer;
        argument._integer      = value;
        argument._text         = to_string( value );
        return *this;
    }

    TextArgumentList& TextArgumentList::addDecimal( string_view name, float64 value )
    {
        TextArgument& argument = getOrAddArgument( name );
        argument._kind         = TextArgumentKind::Decimal;
        argument._decimal      = value;
        return *this;
    }

    TextArgumentList& TextArgumentList::addDateTime( string_view name, const TextDateTime& value )
    {
        TextArgument& argument = getOrAddArgument( name );
        argument._kind         = TextArgumentKind::DateTime;
        argument._dateTime     = value;
        return *this;
    }

    const TextArgument* TextArgumentList::findArgument( string_view name ) const
    {
        for ( const TextArgument& argument : _listArgument )
        {
            if ( argument._name == name )
                return &argument;
        }
        return nullptr;
    }

    bool TextFormatter::format( string_view pattern, const TextArgumentList& arguments, const CultureInfo& culture, string& outText, string* pOutError )
    {
        outText.clear();
        TextFormatterWalker walker( pattern, TextWalkMode::Format );
        walker.bindFormat( &arguments, &culture );
        const bool bParsed = walker.walk( outText );
        if ( bParsed == false )
            outText = string( pattern ); // 구문이 틀린 번역은 원문 그대로 — 반쯤 풀린 글보다 낫다
        if ( pOutError != nullptr )
            *pOutError = walker.getError();
        return bParsed && walker.getError().empty();
    }

    bool TextFormatter::validatePattern( string_view pattern, string* pOutError )
    {
        string              ignored;
        TextFormatterWalker walker( pattern, TextWalkMode::Validate );
        const bool          bParsed = walker.walk( ignored );
        if ( pOutError != nullptr )
            *pOutError = walker.getError();
        return bParsed;
    }

    void TextFormatter::collectArgumentNames( string_view pattern, vector<string>& outListName )
    {
        string              ignored;
        TextFormatterWalker walker( pattern, TextWalkMode::Validate );
        (void)walker.walk( ignored );
        outListName.assign( walker.getNames().begin(), walker.getNames().end() );
    }

    string TextFormatter::mapLiteralText( string_view pattern, TextLiteralMapFunc pfnMap )
    {
        string              rebuilt;
        TextFormatterWalker walker( pattern, TextWalkMode::Rebuild );
        walker.bindMap( pfnMap );
        if ( walker.walk( rebuilt ) == false )
            return string( pattern );
        return rebuilt;
    }
} // namespace sw
