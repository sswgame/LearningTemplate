#include "pch.h"

#include "Editor/Common/Localization/TextGatherer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/set.h"
#include "Core/Foundation/HexDigitUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        /** @brief 열거형 본문에서 읽은 열거자 하나입니다(`ENUMERATOR( … )` 의 보일 글 · 툴팁 · 숨김). */
        struct GatheredEnumerator
        {
            string_view            _name;
            string                 _displayName;
            string                 _tooltip;
            uint32                 _line{ 0 };
            uint8                  _bHasDisplayName : 1;
            uint8                  _bHidden         : 1;
            [[maybe_unused]] uint8 _reserved        : 6;

            GatheredEnumerator()
                : _bHasDisplayName{ SW_FALSE }
                , _bHidden{ SW_FALSE }
                , _reserved{ 0 }
            {
            }
        };

        /**
         * @class TextGathererCodeScanner
         * @brief C++ 텍스트를 한 번 훑으며 주석, 문자열, 전처리 줄을 건너뛰고 매크로 호출의 문자열 리터럴 인자를 읽습니다.
         * @details `ENUM()` 열거형 본문의 `ENUMERATOR( DisplayName = "…" )` 도 읽어 키 `Enum.<열거형>.<열거자>` 로 모은다(`LocalizedText::makeEnumKey`).
         */
        class TextGathererCodeScanner
        {
        public:
            static constexpr utf8 kDoubleQuote = 0x22; ///< `"` 입니다. 문자 리터럴로 적으면 린트의 텍스트 토큰이 어긋납니다.
            static constexpr utf8 kSingleQuote = 0x27;

            TextGathererCodeScanner( string_view text, string_view originName, TextGatherer& gatherer )
                : _text{ text }
                , _originName{ originName }
                , _pGatherer{ &gatherer }
                , _position{ 0 }
                , _line{ 1 }
                , _bLineStart{ SW_TRUE }
                , _bPreprocessorLine{ SW_FALSE }
                , _bEnumMacroPending{ SW_FALSE }
            {
            }

            void scan()
            {
                while ( _position < _text.size() )
                {
                    const utf8 character = _text[_position];
                    if ( character == '\n' )
                    {
                        advanceLine();
                        continue;
                    }
                    if ( isPair( '/', '/' ) )
                    {
                        while ( _position < _text.size() && _text[_position] != '\n' )
                        {
                            ++_position;
                        }
                        continue;
                    }
                    if ( isPair( '/', '*' ) )
                    {
                        skipBlockComment();
                        continue;
                    }
                    if ( character == '#' && _bLineStart )
                    {
                        _bPreprocessorLine = SW_TRUE;
                        ++_position;
                        continue;
                    }
                    const bool bWhitespaceCharacter = character == ' ' || character == '\t' || character == '\r';
                    if ( bWhitespaceCharacter )
                    {
                        ++_position;
                        continue;
                    }
                    _bLineStart              = SW_FALSE;
                    const bool bLiteralStart = character == kDoubleQuote || character == kSingleQuote || isRawStringStart();
                    if ( bLiteralStart )
                    {
                        string ignored;
                        (void)readLiteral( ignored ); // 건너뛰는 리터럴이다. 닫히지 않았으면 그 줄 끝에서 멈추고 훑기를 이어 간다
                        continue;
                    }
                    if ( isIdentifierStart( character ) )
                    {
                        const size_t start = _position;
                        while ( _position < _text.size() && isIdentifierCharacter( _text[_position] ) )
                        {
                            ++_position;
                        }
                        const string_view identifier = _text.substr( start, _position - start );
                        if ( _bPreprocessorLine != SW_FALSE )
                            continue;
                        if ( isCodeMacro( identifier ) )
                            readMacroCall( identifier );
                        else if ( identifier == kEnumMacro )
                            _bEnumMacroPending = SW_TRUE;
                        else if ( identifier == kEnumKeyword )
                            readEnumDeclaration();
                        continue;
                    }
                    ++_position;
                }
            }

        private:
            static constexpr string_view kEnumMacro       = "ENUM";
            static constexpr string_view kEnumKeyword     = "enum";
            static constexpr string_view kEnumeratorMacro = "ENUMERATOR";

            static bool isIdentifierStart( utf8 character ) { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || character == '_'; }
            static bool isIdentifierCharacter( utf8 character ) { return isIdentifierStart( character ) || ( '0' <= character && character <= '9' ); }

            static bool isCodeMacro( string_view identifier )
            {
                for ( const utf8* pMacro : TextGatherer::kArrCodeMacro )
                {
                    if ( identifier == pMacro )
                        return true;
                }
                return false;
            }

            bool startsWith( string_view prefix ) const { return _text.substr( _position, prefix.size() ) == prefix; }
            /** @brief 지금 위치의 두 글자가 @p first 와 @p second 인지 확인합니다(주석 표시이며, 텍스트로 적으면 린트가 그것을 주석으로 읽습니다). */
            bool isPair( utf8 first, utf8 second ) const { return _position + 1 < _text.size() && _text[_position] == first && _text[_position + 1] == second; }

            void advanceLine()
            {
                // 줄 끝의 `\` 는 전처리 줄을 잇는다.
                const bool bContinued = _position > 0 && _text[_position - 1] == '\\';
                ++_position;
                ++_line;
                _bLineStart = SW_TRUE;
                if ( bContinued == false )
                    _bPreprocessorLine = SW_FALSE;
            }

            void skipBlockComment()
            {
                _position += 2;
                while ( _position < _text.size() && isPair( '*', '/' ) == false )
                {
                    if ( _text[_position] == '\n' )
                        ++_line;
                    ++_position;
                }
                _position = _position + 2 <= _text.size() ? _position + 2 : _text.size();
            }

            void skipSpaceAndComments()
            {
                while ( _position < _text.size() )
                {
                    const utf8 character    = _text[_position];
                    const bool bInlineSpace = character == ' ' || character == '\t' || character == '\r';
                    if ( character == '\n' )
                    {
                        ++_line;
                        ++_position;
                    }
                    else if ( bInlineSpace )
                        ++_position;
                    else if ( isPair( '/', '/' ) )
                    {
                        while ( _position < _text.size() && _text[_position] != '\n' )
                        {
                            ++_position;
                        }
                    }
                    else if ( isPair( '/', '*' ) )
                        skipBlockComment();
                    else
                        break;
                }
            }

            bool isRawStringStart() const
            {
                return startsWith( "R\"" ) || startsWith( "u8R\"" ) || startsWith( "LR\"" );
            }

            bool isStringLiteralStart() const
            {
                return _position < _text.size() && ( _text[_position] == kDoubleQuote || startsWith( "u8\"" ) || startsWith( "L\"" ) || isRawStringStart() );
            }

            /** @brief 지금 위치의 문자열이나 문자 리터럴 하나를 읽어 해석한 텍스트를 @p outValue 에 붙입니다. */
            [[nodiscard]] bool readLiteral( string& outValue )
            {
                if ( startsWith( "u8" ) )
                    _position += 2;
                else if ( startsWith( "L" ) )
                    _position += 1;
                if ( startsWith( "R\"" ) )
                    return readRawLiteral( outValue );
                const utf8 quote = _text[_position];
                ++_position;
                while ( _position < _text.size() && _text[_position] != quote )
                {
                    const utf8 character = _text[_position];
                    if ( character == '\n' )
                        return false; // 닫히지 않은 리터럴이다
                    if ( character != '\\' )
                    {
                        outValue.push_back( character );
                        ++_position;
                        continue;
                    }
                    readEscape( outValue );
                }
                if ( _position >= _text.size() )
                    return false;
                ++_position;
                return true;
            }

            void readEscape( string& outValue )
            {
                ++_position; // '\'
                if ( _position >= _text.size() )
                    return;
                const utf8 escaped = _text[_position];
                ++_position;
                switch ( escaped )
                {
                    case 'n':
                    {
                        outValue.push_back( '\n' );
                        break;
                    }
                    case 't':
                    {
                        outValue.push_back( '\t' );
                        break;
                    }
                    case 'r':
                    {
                        outValue.push_back( '\r' );
                        break;
                    }
                    case '0':
                    {
                        outValue.push_back( '\0' );
                        break;
                    }
                    case 'x':
                    {
                        appendHexEscape( outValue, 2, false );
                        break;
                    }
                    case 'u':
                    {
                        appendHexEscape( outValue, 4, true );
                        break;
                    }
                    case 'U':
                    {
                        appendHexEscape( outValue, 8, true );
                        break;
                    }
                    default:
                    {
                        outValue.push_back( escaped );
                        break;
                    }
                }
            }

            void appendHexEscape( string& outValue, uint32 maxDigits, bool bCodepoint )
            {
                uint32 value{ 0 };
                uint32 digitCount{ 0 };
                while ( digitCount < maxDigits && _position < _text.size() )
                {
                    const utf8 character = _text[_position];
                    uint32     digit{ 0 };
                    if ( HexDigitUtil::tryParseDigit( character, digit ) == false )
                        break;
                    value = value * 16 + digit;
                    ++digitCount;
                    ++_position;
                }
                if ( bCodepoint )
                    StringUtil::appendUTF8( outValue, value );
                else
                    outValue.push_back( static_cast<utf8>( value ) );
            }

            [[nodiscard]] bool readRawLiteral( string& outValue )
            {
                _position += 2; // R"
                const size_t delimiterEnd = _text.find( '(', _position );
                if ( delimiterEnd == string_view::npos )
                    return false;
                fixed_string<constant::buffer::kMax32> closing;
                formatstring( closing.data(), closing.capacity(), ")%#\"", _text.substr( _position, delimiterEnd - _position ) );
                const size_t bodyStart = delimiterEnd + 1;
                const size_t bodyEnd   = _text.find( closing.view(), bodyStart );
                if ( bodyEnd == string_view::npos )
                    return false;
                const string_view body = _text.substr( bodyStart, bodyEnd - bodyStart );
                for ( const utf8 character : body )
                {
                    _line += character == '\n' ? 1u : 0u;
                }
                outValue.append( body );
                _position = bodyEnd + closing.size();
                return true;
            }

            /** @brief 이어 붙은 문자열 리터럴(`"a" "b"`)을 하나로 읽습니다. 리터럴이 아니면 false 입니다. */
            [[nodiscard]] bool readConcatenatedLiteral( string& outValue )
            {
                skipSpaceAndComments();
                if ( isStringLiteralStart() == false )
                    return false;
                while ( isStringLiteralStart() )
                {
                    if ( readLiteral( outValue ) == false )
                        return false;
                    skipSpaceAndComments();
                }
                return true;
            }

            string makeLocation( uint32 line ) const
            {
                StringBuilder<constant::buffer::kMax256> location;
                location.appendFormat( "%#:%#", string( _originName ), line );
                return string( location.view() );
            }

            void readMacroCall( string_view macroName )
            {
                const uint32 callLine = _line;
                skipSpaceAndComments();
                if ( _position >= _text.size() || _text[_position] != '(' )
                    return; // 매크로 이름만 쓴 경우다(문서나 다른 매크로의 인자)
                ++_position;
                // `SW_LOCKEY` 와 `SW_LOCKEYFORMAT` 은 원문을 코드에 두지 않는다. 키만 읽어 "테이블에 있어야 하는 참조" 로 모은다.
                const bool   bKeyOnly     = macroName == "SW_LOCKEY" || macroName == "SW_LOCKEYFORMAT" || macroName == "SW_LOCKEYREF" || macroName == "SW_LOCALIZED_MESSAGE";
                const uint32 literalCount = bKeyOnly ? 2u : 3u;
                string       arrArgument[3];
                for ( uint32 argumentIndex = 0; argumentIndex < literalCount; ++argumentIndex )
                {
                    if ( readConcatenatedLiteral( arrArgument[argumentIndex] ) == false )
                    {
                        fixed_string<constant::buffer::kMax256> issueText;
                        formatstring( issueText.data(), issueText.capacity(), "%# arguments must be string literals - the text is not gathered", macroName );
                        _pGatherer->addIssue( makeLocation( callLine ), issueText, true );
                        return;
                    }
                    const bool bLast = argumentIndex + 1 == literalCount;
                    if ( _position >= _text.size() || ( bLast == false && _text[_position] != ',' ) )
                    {
                        fixed_string<constant::buffer::kMax256> issueText;
                        formatstring( issueText.data(), issueText.capacity(), "%#%#", macroName,
                                      bKeyOnly ? " needs ( \"Namespace\", \"Key\" )" : " needs ( \"Namespace\", \"Key\", \"Source\" )" );
                        _pGatherer->addIssue( makeLocation( callLine ), issueText, true );
                        return;
                    }
                    if ( bLast == false )
                        ++_position;
                }
                if ( arrArgument[1].empty() )
                {
                    fixed_string<constant::buffer::kMax256> issueText;
                    formatstring( issueText.data(), issueText.capacity(), "%# has an empty key", macroName );
                    _pGatherer->addIssue( makeLocation( callLine ), issueText, true );
                    return;
                }
                if ( bKeyOnly )
                {
                    _pGatherer->addKeyReference( SourceTextEntry::makeFullKey( arrArgument[0], arrArgument[1] ), _originName );
                    return;
                }
                if ( const expected<void> valid = TextFormatter::validatePattern( arrArgument[2] ); valid.has_value() == false )
                {
                    fixed_string<constant::buffer::kMax512> issueText;
                    formatstring( issueText.data(), issueText.capacity(), "source text is not a valid message pattern: %#", valid.error().detail() );
                    _pGatherer->addIssue( makeLocation( callLine ), issueText, true );
                }
                _pGatherer->addKeyedText( SourceTextEntry::makeFullKey( arrArgument[0], arrArgument[1] ), arrArgument[2], {}, _originName );
            }

            string_view readIdentifier()
            {
                const size_t start = _position;
                while ( _position < _text.size() && isIdentifierCharacter( _text[_position] ) )
                {
                    ++_position;
                }
                return _text.substr( start, _position - start );
            }

            /** @brief 전처리 줄 하나를 건너뜁니다(X 매크로로 열거자를 펼치는 본문). 줄 끝의 `\` 는 줄을 잇는다. */
            void skipPreprocessorLine()
            {
                while ( _position < _text.size() )
                {
                    if ( _text[_position] != '\n' )
                    {
                        ++_position;
                        continue;
                    }
                    const bool bContinued = _position > 0 && _text[_position - 1] == '\\';
                    ++_line;
                    ++_position;
                    if ( bContinued == false )
                        return;
                }
            }

            /**
             * @brief `enum [class] 이름 [: 형] { … }` 을 읽습니다. 앞 선언(`;`)이면 아무것도 하지 않습니다.
             * @details 바로 앞에 `ENUM()` 이 있었는지는 `_bEnumMacroPending` 이 든다 — 리플렉션이 없는 열거형은 실행 중에 키를 만들 이름이 없다.
             */
            void readEnumDeclaration()
            {
                const bool   bReflected = _bEnumMacroPending != SW_FALSE;
                const uint32 line       = _line;
                _bEnumMacroPending      = SW_FALSE;
                string_view enumName;
                bool        bAfterColon{ false };
                while ( true )
                {
                    skipSpaceAndComments();
                    if ( _position >= _text.size() )
                        return;
                    const utf8 character = _text[_position];
                    if ( character == '{' )
                        break;
                    const bool bNotDefinition = character == ';' || character == ')' || character == ',' || character == '>' || character == '=';
                    if ( bNotDefinition )
                        return;
                    if ( isIdentifierStart( character ) )
                    {
                        const string_view word = readIdentifier();
                        if ( bAfterColon == false && enumName.empty() && word != "class" && word != "struct" )
                            enumName = word;
                        continue;
                    }
                    bAfterColon = bAfterColon || character == ':';
                    ++_position;
                }
                ++_position; // '{'
                vector<GatheredEnumerator> listEnumerator;
                readEnumBody( listEnumerator );
                addEnumTexts( enumName, bReflected, line, listEnumerator );
            }

            /** @brief 열거형 본문을 닫는 `}` 까지 읽어 열거자를 모읍니다. 값 식 · 매크로 인자는 괄호 깊이를 세며 건너뜁니다. */
            void readEnumBody( vector<GatheredEnumerator>& outListEnumerator )
            {
                while ( true )
                {
                    skipSpaceAndComments();
                    if ( _position >= _text.size() )
                        return;
                    const utf8 character = _text[_position];
                    if ( character == '}' )
                    {
                        ++_position;
                        return;
                    }
                    if ( character == '#' )
                    {
                        skipPreprocessorLine();
                        continue;
                    }
                    if ( isIdentifierStart( character ) == false )
                    {
                        ++_position; // 쉼표
                        continue;
                    }
                    GatheredEnumerator& enumerator = outListEnumerator.emplace_back();
                    enumerator._line               = _line;
                    enumerator._name               = readIdentifier();
                    skipSpaceAndComments();
                    if ( startsWith( kEnumeratorMacro ) )
                    {
                        (void)readIdentifier();
                        readEnumeratorArguments( enumerator );
                    }
                    skipEnumeratorValue();
                }
            }

            /** @brief `ENUMERATOR( DisplayName = "…", Tooltip = "…", Hidden, Meta = "…" )` 의 인자를 읽습니다. 글이 아닌 값은 건너뜁니다. */
            void readEnumeratorArguments( GatheredEnumerator& outEnumerator )
            {
                skipSpaceAndComments();
                if ( _position >= _text.size() || _text[_position] != '(' )
                    return;
                ++_position;
                while ( true )
                {
                    skipSpaceAndComments();
                    if ( _position >= _text.size() )
                        return;
                    const utf8 character = _text[_position];
                    if ( character == ')' )
                    {
                        ++_position;
                        return;
                    }
                    if ( isIdentifierStart( character ) == false )
                    {
                        ++_position; // 쉼표
                        continue;
                    }
                    const string_view key = readIdentifier();
                    skipSpaceAndComments();
                    string value;
                    bool   bText{ false };
                    if ( _position < _text.size() && _text[_position] == '=' )
                    {
                        ++_position;
                        bText = readConcatenatedLiteral( value );
                        if ( bText == false )
                            value = string( readIdentifier() ); // `Hidden = false` 처럼 글이 아닌 값
                    }
                    if ( key == "DisplayName" && bText )
                    {
                        outEnumerator._displayName     = std::move( value );
                        outEnumerator._bHasDisplayName = SW_TRUE;
                    }
                    else if ( key == "Tooltip" && bText )
                        outEnumerator._tooltip = std::move( value );
                    else if ( key == "Hidden" )
                        outEnumerator._bHidden = value != "false" ? SW_TRUE : SW_FALSE;
                }
            }

            /** @brief 열거자의 나머지(`= 값`, 매크로 인자)를 다음 쉼표나 본문을 닫는 `}` 앞까지 건너뜁니다. */
            void skipEnumeratorValue()
            {
                uint32 depth{ 0 };
                while ( true )
                {
                    skipSpaceAndComments();
                    if ( _position >= _text.size() )
                        return;
                    const utf8 character = _text[_position];
                    if ( depth == 0 && ( character == ',' || character == '}' ) )
                        return;
                    if ( character == kDoubleQuote || character == kSingleQuote )
                    {
                        string ignored;
                        (void)readLiteral( ignored ); // 닫히지 않은 리터럴은 그 줄 끝에서 멈춘다
                        continue;
                    }
                    if ( character == '(' )
                        ++depth;
                    else if ( character == ')' && depth > 0 )
                        --depth;
                    ++_position;
                }
            }

            /**
             * @brief 열거형 하나의 보일 글을 모읍니다. 한 열거자라도 `DisplayName` 을 적었으면 숨기지 않은 열거자 모두가 적어야 한다(`Count` 표식 제외).
             * @details 실행 중 조회(`LocalizedText::findEnum`)는 값마다 키를 찾는다. 빠진 열거자는 배포에서 이름 글자가 그대로 보이므로 수집 오류로 막는다.
             */
            void addEnumTexts( string_view enumName, bool bReflected, uint32 line, const vector<GatheredEnumerator>& listEnumerator )
            {
                bool bAnyDisplayName{ false };
                for ( const GatheredEnumerator& enumerator : listEnumerator )
                {
                    bAnyDisplayName = bAnyDisplayName || enumerator._bHasDisplayName != SW_FALSE;
                }
                if ( bAnyDisplayName == false )
                    return;
                if ( bReflected == false || enumName.empty() )
                {
                    fixed_string<constant::buffer::kMax256> issueText;
                    formatstring( issueText.data(), issueText.capacity(), "enum '%#' has ENUMERATOR( DisplayName ) but no ENUM() - the runtime has no name to build the key from", enumName );
                    _pGatherer->addIssue( makeLocation( line ), issueText, true );
                    return;
                }
                for ( const GatheredEnumerator& enumerator : listEnumerator )
                {
                    const string key = LocalizedText::makeEnumKey( enumName, enumerator._name );
                    if ( enumerator._bHasDisplayName == SW_FALSE )
                    {
                        if ( enumerator._bHidden != SW_FALSE || enumerator._name == "Count" )
                            continue;
                        fixed_string<constant::buffer::kMax512> issueText;
                        formatstring( issueText.data(), issueText.capacity(), "enumerator '%#::%#' has no DisplayName while others in the enum do - add one (key %#) or mark it Hidden", enumName,
                                      enumerator._name, key );
                        _pGatherer->addIssue( makeLocation( enumerator._line ), issueText, true );
                        continue;
                    }
                    if ( const expected<void> valid = TextFormatter::validatePattern( enumerator._displayName ); valid.has_value() == false )
                    {
                        fixed_string<constant::buffer::kMax512> issueText;
                        formatstring( issueText.data(), issueText.capacity(), "DisplayName of '%#' is not a valid message pattern: %#", key, valid.error().detail() );
                        _pGatherer->addIssue( makeLocation( enumerator._line ), issueText, true );
                    }
                    _pGatherer->addKeyedText( key, enumerator._displayName, {}, _originName );
                    if ( enumerator._tooltip.empty() == false )
                    {
                        StringBuilder<constant::buffer::kMax256> tooltipKey;
                        tooltipKey.appendFormat( "%#.%#", key, LocalizedText::kEnumTooltipSuffix );
                        _pGatherer->addKeyedText( tooltipKey.view(), enumerator._tooltip, {}, _originName );
                    }
                }
            }

        private:
            string_view   _text;
            string_view   _originName;
            TextGatherer* _pGatherer;
            size_t        _position;
            uint32        _line;
            uint8         _bLineStart        : 1;
            uint8         _bPreprocessorLine : 1;
            uint8         _bEnumMacroPending : 1; ///< 바로 앞에 `ENUM(` 이 있었다 — 다음 `enum` 이 리플렉션 열거형이다
        };

        struct TextGathererInternal
        {
            /** @brief 사람이 읽는 텍스트로 보이는지 판정합니다. 글자 둘 이상인 단어가 둘 이상이고, 경로, 식별자, 숫자 목록이 아니어야 합니다. 싼 추정이라 경고만 합니다. */
            static bool isLikelyUserFacingText( string_view value )
            {
                const bool bPathLike = value.find( '/' ) != string_view::npos || value.find( '\\' ) != string_view::npos || value.find( '_' ) != string_view::npos;
                if ( bPathLike || value.find( ' ' ) == string_view::npos )
                    return false;
                uint32 wordCount{ 0 };
                uint32 letterRun{ 0 };
                for ( const utf8 character : value )
                {
                    const bool bLetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || static_cast<uint8>( character ) >= 0x80;
                    if ( bLetter )
                    {
                        ++letterRun;
                        continue;
                    }
                    wordCount += letterRun >= 2 ? 1u : 0u;
                    letterRun = 0;
                }
                wordCount += letterRun >= 2 ? 1u : 0u;
                return wordCount >= 2;
            }

            static bool isStringProperty( const PropertyInfo& property )
            {
                // 글 타입 판정은 내장 타입 표의 자리로 한다(별칭도 같은 자리) — 이름 리터럴을 두지 않는다. 키를 드는 칸은 hashed_string 이기도 하다(오디오 자막 키).
                if ( property._containerKind != ContainerKind::None )
                    return false;
                const auto builtinIndex = property.resolveValueFacts( editor::getSubsystem<TypeRegistry>() )._builtinIndex;
                return builtinIndex == ReflectBuiltinOf<string>::kIndex || builtinIndex == ReflectBuiltinOf<hashed_string>::kIndex;
            }

            static string_view readPropertyValue( const XMLNode& node, const PropertyInfo& property )
            {
                string_view value = node.getAttributeText( property._name.c_str() );
                if ( value.empty() )
                {
                    const utf8* pChildText = node.findChildText( property._name.c_str() );
                    if ( pChildText != nullptr )
                        value = string_view( pChildText );
                }
                return value;
            }

            /** @brief `LocalizableIf` 가 가리키는 불리언 칸이 @p node 에 거짓으로 적혔는지 봅니다. 적지 않았거나 바인딩 식이면 번역한다. */
            static bool isTurnedOff( const XMLNode& node, const TypeInfo& type, const PropertyInfo& property )
            {
                const string* pSwitchName = property._metadata->getEditor().findCustomMeta( hashed_string( TextGatherer::kMetaLocalizableIf ) );
                if ( pSwitchName == nullptr )
                    return false;
                const PropertyInfo* pSwitch = type.findPropertyInHierarchy( hashed_string( pSwitchName->c_str() ) );
                SW_ASSERT( pSwitch != nullptr );
                bool bLocalized{ true };
                return pSwitch != nullptr && StringUtil::tryParseBool( readPropertyValue( node, *pSwitch ), bLocalized ) && bLocalized == false;
            }

            static void gatherNode( const XMLNode& node, string_view originName, TextGatherer& gatherer, uint32 depth )
            {
                if ( depth > 64 )
                    return;
                const utf8*     pName = node.getName();
                const TypeInfo* pType = StringUtil::isNullOrEmpty( pName ) ? nullptr : editor::getSubsystem<TypeRegistry>().findType( hashed_string( pName ) );
                if ( pType != nullptr )
                {
                    pType->forEachProperty( [&node, originName, &gatherer, pType]( const PropertyInfo& property )
                    {
                        if ( isStringProperty( property ) == false )
                            return;
                        const string_view value = readPropertyValue( node, property );
                        // `{` 로 시작하는 값은 UI 문서의 바인딩 식이다(`{bind:_health}`). 텍스트가 아니다.
                        if ( value.empty() || value.front() == '{' )
                            return;
                        fixed_string<constant::buffer::kMax256> context;
                        formatstring( context.data(), context.capacity(), "%#.%#", pType->_name.c_str(), property._name.c_str() );
                        if ( property._metadata->getEditor().findCustomMeta( hashed_string( TextGatherer::kMetaLocalizable ) ) != nullptr )
                        {
                            if ( isTurnedOff( node, *pType, property ) )
                                return;
                            const string* pMaxLength = property._metadata->getEditor().findCustomMeta( hashed_string( TextGatherer::kMetaMaxLength ) );
                            int32         maxLength{ 0 };
                            if ( pMaxLength != nullptr && StringUtil::parseInt( *pMaxLength, maxLength ) == false )
                                maxLength = 0;
                            gatherer.addTextOrKey( value, context, originName, static_cast<uint32>( maxLength > 0 ? maxLength : 0 ) );
                            return;
                        }
                        const bool bExempt = property._metadata->getEditor().findCustomMeta( hashed_string( TextGatherer::kMetaNotLocalizable ) ) != nullptr;
                        if ( bExempt == false && isLikelyUserFacingText( value ) )
                        {
                            fixed_string<constant::buffer::kMax512> issueText;
                            formatstring( issueText.data(), issueText.capacity(), "possible hardcoded user-facing text in %#: '%#' - mark the property Meta = \"Localizable\" (or \"NotLocalizable\")", context,
                                          value );
                            gatherer.addIssue( originName, issueText, false );
                        }
                    }, true );
                }
                for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    gatherNode( child, originName, gatherer, depth + 1 );
                }
            }

            static void appendOrigin( vector<string>& inoutListOrigin, string_view origin )
            {
                if ( origin.empty() )
                    return;
                for ( const string& existing : inoutListOrigin )
                {
                    if ( existing == origin )
                        return;
                }
                inoutListOrigin.push_back( string( origin ) );
            }

            static bool hasKey( const vector<const SourceStringTable*>& listTable, string_view key )
            {
                for ( const SourceStringTable* pTable : listTable )
                {
                    if ( pTable != nullptr && pTable->findEntry( key ) != nullptr )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool TextGatherReport::hasErrors() const
    {
        return VectorUtil::containsIf( _listIssue, [&]( const TextGatherIssue& issue )
        { return issue._bError; } );
    }

    TextGatherer::TextGatherer()
        : _listText{}
        , _mapTextIndex{}
        , _listIssue{}
        , _fileCount{ 0 }
    {
    }

    void TextGatherer::gatherCodeText( string_view sourceText, string_view originName )
    {
        TextGathererCodeScanner scanner( sourceText, originName, *this );
        scanner.scan();
    }

    void TextGatherer::gatherReflectedXML( string_view xmlText, string_view originName )
    {
        XMLDocument document;
        if ( document.parse( xmlText, originName ) == false )
        {
            fixed_string<constant::buffer::kMax512> issueText;
            formatstring( issueText.data(), issueText.capacity(), "XML cannot be parsed: %#", document.getLastError() );
            addIssue( originName, issueText, true );
            return;
        }
        const XMLNode root = document.getRoot();
        if ( root.isValid() )
            TextGathererInternal::gatherNode( root, originName, *this, 0 );
    }

    GatheredText& TextGatherer::getOrAddText( string_view key, GatheredTextKind kind, string_view origin, bool& outAdded )
    {
        const auto it = _mapTextIndex.find( string( key ) );
        outAdded      = it == _mapTextIndex.end();
        if ( outAdded == false )
        {
            GatheredText& existing = _listText[it->second];
            TextGathererInternal::appendOrigin( existing._listOrigin, origin );
            // 키가 있는 글(코드)이 참조보다 강하다.
            if ( kind == GatheredTextKind::Keyed )
                existing._kind = kind;
            return existing;
        }
        _mapTextIndex[string( key )] = _listText.size();
        GatheredText& added          = _listText.emplace_back();
        added._key                   = string( key );
        added._kind                  = kind;
        TextGathererInternal::appendOrigin( added._listOrigin, origin );
        return added;
    }

    void TextGatherer::addKeyedText( string_view key, string_view source, string_view context, string_view origin, uint32 maxLength )
    {
        bool          bAdded{ false };
        GatheredText& text               = getOrAddText( key, GatheredTextKind::Keyed, origin, bAdded );
        const bool    bConflictingSource = bAdded == false && text._source.empty() == false && text._source != source;
        if ( bConflictingSource )
        {
            fixed_string<constant::buffer::kMax1024> issueText;
            formatstring( issueText.data(), issueText.capacity(), "key '%#' has two different source texts ('%#' and '%#') - the first one is kept", key, text._source, source );
            addIssue( origin, issueText, true );
            return;
        }
        text._source = string( source );
        if ( text._context.empty() )
            text._context = string( context );
        if ( maxLength != 0 )
            text._maxLength = maxLength;
    }

    void TextGatherer::addTextOrKey( string_view textOrKey, string_view context, string_view origin, uint32 maxLength )
    {
        bool          bAdded{ false };
        GatheredText& text = getOrAddText( textOrKey, GatheredTextKind::TextOrKey, origin, bAdded );
        if ( bAdded )
        {
            text._source  = string( textOrKey );
            text._context = string( context );
        }
        if ( maxLength != 0 && ( text._maxLength == 0 || maxLength < text._maxLength ) )
            text._maxLength = maxLength;
        if ( bAdded == false )
            return;
        if ( const expected<void> valid = TextFormatter::validatePattern( textOrKey ); valid.has_value() == false )
        {
            fixed_string<constant::buffer::kMax512> issueText;
            formatstring( issueText.data(), issueText.capacity(), "text '%#' is not a valid message pattern: %#", textOrKey, valid.error().detail() );
            addIssue( origin, issueText, true );
        }
    }

    void TextGatherer::addKeyReference( string_view key, string_view origin )
    {
        bool bAdded{ false };
        (void)getOrAddText( key, GatheredTextKind::KeyReference, origin, bAdded );
    }

    void TextGatherer::addIssue( string_view location, string_view message, bool bError )
    {
        TextGatherIssue& issue = _listIssue.emplace_back();
        issue._location        = string( location );
        issue._message         = string( message );
        issue._bError          = bError;
    }

    TextGatherReport TextGatherer::mergeInto( SourceStringTable& inoutGatherTable, const vector<const SourceStringTable*>& listOtherTable ) const
    {
        TextGatherReport report;
        report._listIssue = _listIssue;
        set<string> uniqueSeen;

        for ( const GatheredText& text : _listText )
        {
            const bool bInOther = TextGathererInternal::hasKey( listOtherTable, text._key );
            if ( bInOther )
            {
                // 손으로 쓴 다른 테이블의 키는 참조일 뿐이다. 코드가 다른 원문을 적었으면 알린다.
                for ( const SourceStringTable* pTable : listOtherTable )
                {
                    const SourceTextEntry* pEntry   = pTable != nullptr ? pTable->findEntry( text._key ) : nullptr;
                    const bool             bDiffers = pEntry != nullptr && text._kind == GatheredTextKind::Keyed && pEntry->_source != text._source;
                    if ( bDiffers )
                    {
                        StringBuilder<constant::buffer::kMax256> issueBuilder;
                        issueBuilder.appendFormat( "key '%#' is defined by another string table with a different source - the table wins", text._key );
                        report._listIssue.push_back( { text._listOrigin.empty() ? string{} : text._listOrigin.front(), string( issueBuilder.view() ), false } );
                    }
                }
                continue;
            }

            const auto       entryIt = inoutGatherTable.getMutableEntries().find( text._key );
            SourceTextEntry* pEntry  = entryIt != inoutGatherTable.getMutableEntries().end() ? &entryIt->second : nullptr;
            if ( pEntry == nullptr && text._kind == GatheredTextKind::KeyReference )
            {
                StringBuilder<constant::buffer::kMax256> issueBuilder;
                issueBuilder.appendFormat( "key '%#' is referenced but no string table defines it", text._key );
                report._listIssue.push_back( { text._listOrigin.empty() ? string{} : text._listOrigin.front(), string( issueBuilder.view() ), true } );
                continue;
            }
            uniqueSeen.insert( text._key );
            if ( pEntry == nullptr )
            {
                SourceTextEntry& added = inoutGatherTable.getOrAddEntry( text._key );
                added._source          = text._source;
                added._context         = text._context;
                added._maxLength       = text._maxLength;
                added._listOrigin      = text._listOrigin;
                VectorUtil::sort( added._listOrigin );
                report._listAdded.push_back( text._key );
                continue;
            }
            if ( text._kind == GatheredTextKind::Keyed && pEntry->_source != text._source )
            {
                pEntry->_source = text._source;
                report._listChanged.push_back( text._key );
            }
            else
                ++report._unchangedCount;
            if ( pEntry->_context.empty() )
                pEntry->_context = text._context;
            if ( pEntry->_maxLength == 0 )
                pEntry->_maxLength = text._maxLength;
            pEntry->_listOrigin = text._listOrigin;
            VectorUtil::sort( pEntry->_listOrigin );
        }

        // 수집기가 넣었던 줄(위치가 있는 줄)이 이번에 나오지 않았으면 지운다. 손으로 넣은 줄은 둔다.
        vector<string> listStale;
        for ( const auto& [key, entry] : inoutGatherTable.getEntries() )
        {
            if ( entry._listOrigin.empty() == false && uniqueSeen.find( key ) == uniqueSeen.end() )
                listStale.push_back( key );
        }
        for ( const string& key : listStale )
        {
            (void)inoutGatherTable.removeEntry( key ); // 방금 테이블에서 찾은 키라 항상 지워진다
            report._listRemoved.push_back( key );
        }
        return report;
    }
} // namespace sw
