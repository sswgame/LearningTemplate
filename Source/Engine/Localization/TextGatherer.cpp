#include "pch.h"

#include "Engine/Localization/TextGatherer.h"

#include "Core/Container/set.h"
#include "Core/String/StringUtil.h"

#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        /**
         * @class TextGathererCodeScanner
         * @brief C++ 글을 한 번 훑으며 주석 · 문자열 · 전처리 줄을 건너뛰고 매크로 호출의 문자열 리터럴 인자를 읽습니다.
         */
        class TextGathererCodeScanner
        {
        public:
            static constexpr utf8 kDoubleQuote = 0x22; ///< `"` — 문자 리터럴로 적으면 린트의 글 토큰이 어긋난다
            static constexpr utf8 kSingleQuote = 0x27;

            TextGathererCodeScanner( string_view text, string_view originName, TextGatherer& gatherer )
                : _text{ text }
                , _originName{ originName }
                , _pGatherer{ &gatherer }
                , _position{ 0 }
                , _line{ 1 }
                , _bLineStart{ true }
                , _bPreprocessorLine{ false }
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
                            ++_position;
                        continue;
                    }
                    if ( isPair( '/', '*' ) )
                    {
                        skipBlockComment();
                        continue;
                    }
                    if ( character == '#' && _bLineStart )
                    {
                        _bPreprocessorLine = true;
                        ++_position;
                        continue;
                    }
                    if ( character == ' ' || character == '\t' || character == '\r' )
                    {
                        ++_position;
                        continue;
                    }
                    _bLineStart = false;
                    if ( character == kDoubleQuote || character == kSingleQuote || isRawStringStart() )
                    {
                        string ignored;
                        (void)readLiteral( ignored );
                        continue;
                    }
                    if ( isIdentifierStart( character ) )
                    {
                        const size_t start = _position;
                        while ( _position < _text.size() && isIdentifierCharacter( _text[_position] ) )
                            ++_position;
                        const string_view identifier = _text.substr( start, _position - start );
                        if ( isCodeMacro( identifier ) && _bPreprocessorLine == false )
                            readMacroCall( identifier );
                        continue;
                    }
                    ++_position;
                }
            }

        private:
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
            /** @brief 지금 자리의 두 글자가 @p first · @p second 인지(주석 표시 — 글로 적으면 린트가 그것을 주석으로 읽는다)입니다. */
            bool isPair( utf8 first, utf8 second ) const { return _position + 1 < _text.size() && _text[_position] == first && _text[_position + 1] == second; }

            void advanceLine()
            {
                // 줄 끝의 `\` 는 전처리 줄을 잇는다.
                const bool bContinued = _position > 0 && _text[_position - 1] == '\\';
                ++_position;
                ++_line;
                _bLineStart = true;
                if ( bContinued == false )
                    _bPreprocessorLine = false;
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
                    const utf8 character = _text[_position];
                    if ( character == '\n' )
                    {
                        ++_line;
                        ++_position;
                    }
                    else if ( character == ' ' || character == '\t' || character == '\r' )
                        ++_position;
                    else if ( isPair( '/', '/' ) )
                    {
                        while ( _position < _text.size() && _text[_position] != '\n' )
                            ++_position;
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

            /** @brief 지금 자리의 문자열 · 문자 리터럴 하나를 읽어 풀린 글을 @p outValue 에 붙입니다. */
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
                        return false; // 닫히지 않은 리터럴
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
                    if ( '0' <= character && character <= '9' )
                        digit = static_cast<uint32>( character - '0' );
                    else if ( 'a' <= character && character <= 'f' )
                        digit = static_cast<uint32>( character - 'a' + 10 );
                    else if ( 'A' <= character && character <= 'F' )
                        digit = static_cast<uint32>( character - 'A' + 10 );
                    else
                        break;
                    value = value * 16 + digit;
                    ++digitCount;
                    ++_position;
                }
                if ( bCodepoint )
                    StringUtil::appendUtf8( outValue, value );
                else
                    outValue.push_back( static_cast<utf8>( value ) );
            }

            [[nodiscard]] bool readRawLiteral( string& outValue )
            {
                _position += 2; // R"
                const size_t delimiterEnd = _text.find( '(', _position );
                if ( delimiterEnd == string_view::npos )
                    return false;
                const string closing   = ")" + string( _text.substr( _position, delimiterEnd - _position ) ) + "\"";
                const size_t bodyStart = delimiterEnd + 1;
                const size_t bodyEnd   = _text.find( closing, bodyStart );
                if ( bodyEnd == string_view::npos )
                    return false;
                const string_view body = _text.substr( bodyStart, bodyEnd - bodyStart );
                for ( const utf8 character : body )
                    _line += character == '\n' ? 1u : 0u;
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

            string makeLocation( uint32 line ) const { return string( _originName ) + ":" + to_string( line ); }

            void readMacroCall( string_view macroName )
            {
                const uint32 callLine = _line;
                skipSpaceAndComments();
                if ( _position >= _text.size() || _text[_position] != '(' )
                    return; // 매크로 이름만 쓴 자리(문서 · 다른 매크로의 인자)
                ++_position;
                string arrArgument[3];
                for ( uint32 argumentIndex = 0; argumentIndex < 3; ++argumentIndex )
                {
                    if ( readConcatenatedLiteral( arrArgument[argumentIndex] ) == false )
                    {
                        _pGatherer->addIssue( makeLocation( callLine ), string( macroName ) + " arguments must be string literals - the text is not gathered", true );
                        return;
                    }
                    const bool bLast = argumentIndex == 2;
                    if ( _position >= _text.size() || ( bLast == false && _text[_position] != ',' ) )
                    {
                        _pGatherer->addIssue( makeLocation( callLine ), string( macroName ) + " needs ( \"Namespace\", \"Key\", \"Source\" )", true );
                        return;
                    }
                    if ( bLast == false )
                        ++_position;
                }
                if ( arrArgument[1].empty() )
                {
                    _pGatherer->addIssue( makeLocation( callLine ), string( macroName ) + " has an empty key", true );
                    return;
                }
                string error;
                if ( TextFormatter::validatePattern( arrArgument[2], &error ) == false )
                    _pGatherer->addIssue( makeLocation( callLine ), "source text is not a valid message pattern: " + error, true );
                _pGatherer->addKeyedText( LocalizationTextUtil::makeFullKey( arrArgument[0], arrArgument[1] ), arrArgument[2], {}, _originName );
            }

        private:
            string_view   _text;
            string_view   _originName;
            TextGatherer* _pGatherer;
            size_t        _position;
            uint32        _line;
            bool          _bLineStart;
            bool          _bPreprocessorLine;
        };

        struct TextGathererInternal
        {
            /** @brief 사람이 읽는 글로 보이는지 — 글자 둘 이상인 낱말이 둘 이상이고, 경로 · 식별자 · 숫자 목록이 아닙니다. 싼 추정이라 경고만 합니다. */
            static bool looksLikeUserFacingText( string_view value )
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
                static const hashed_string s_string( "string" );
                return property._typeName == s_string && property._containerKind == ContainerKind::None;
            }

            static string_view readPropertyValue( const XmlNode& node, const PropertyInfo& property )
            {
                const string_view attribute = node.getAttributeText( property._name.c_str(), false );
                if ( attribute.empty() == false )
                    return attribute;
                const utf8* pChildText = node.findChildText( property._name.c_str(), false );
                return pChildText != nullptr ? string_view( pChildText ) : string_view{};
            }

            static void gatherNode( const XmlNode& node, string_view originName, TextGatherer& gatherer, uint32 depth )
            {
                if ( depth > 64 )
                    return;
                const utf8*     pName = node.getName();
                const TypeInfo* pType = StringUtil::isNullOrEmpty( pName ) ? nullptr : engine::getTypeRegistry().findType( hashed_string( pName ) );
                if ( pType != nullptr )
                {
                    pType->forEachProperty( [&node, originName, &gatherer, pType]( const PropertyInfo& property )
                    {
                        if ( isStringProperty( property ) == false )
                            return;
                        const string_view value = readPropertyValue( node, property );
                        // `{` 로 시작하는 값은 UI 문서의 바인딩 식이다(`{bind:_health}`) — 글이 아니다.
                        if ( value.empty() || value.front() == '{' )
                            return;
                        const string context = string( pType->_name.c_str() ) + "." + property._name.c_str();
                        if ( property._metadata.findCustomMeta( hashed_string( TextGatherer::kMetaLocalizable ) ) != nullptr )
                        {
                            const string* pMaxLength = property._metadata.findCustomMeta( hashed_string( TextGatherer::kMetaMaxLength ) );
                            int32         maxLength{ 0 };
                            if ( pMaxLength != nullptr && StringUtil::parseInt( *pMaxLength, maxLength ) == false )
                                maxLength = 0;
                            gatherer.addTextOrKey( value, context, originName, static_cast<uint32>( maxLength > 0 ? maxLength : 0 ) );
                            return;
                        }
                        const bool bExempt = property._metadata.findCustomMeta( hashed_string( TextGatherer::kMetaNotLocalizable ) ) != nullptr ||
                                             property._metadata._bAssetPath != SW_FALSE || property._metadata._assetType.empty() == false;
                        if ( bExempt == false && looksLikeUserFacingText( value ) )
                            gatherer.addIssue( originName, "possible hardcoded user-facing text in " + context + ": '" + string( value ) + "' - mark the property Meta = \"Localizable\" (or \"NotLocalizable\")",
                                               false );
                    }, true );
                }
                for ( XmlNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                    gatherNode( child, originName, gatherer, depth + 1 );
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
        for ( const TextGatherIssue& issue : _listIssue )
        {
            if ( issue._bError )
                return true;
        }
        return false;
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

    void TextGatherer::gatherReflectedXml( string_view xmlText, string_view originName )
    {
        XmlDocument document;
        if ( document.parse( xmlText, originName ) == false )
        {
            addIssue( originName, "XML cannot be parsed: " + document.getLastError(), true );
            return;
        }
        const XmlNode root = document.getRoot();
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
        GatheredText& text = getOrAddText( key, GatheredTextKind::Keyed, origin, bAdded );
        if ( bAdded == false && text._source.empty() == false && text._source != source )
        {
            addIssue( origin, "key '" + string( key ) + "' has two different source texts ('" + text._source + "' and '" + string( source ) + "') - the first one is kept", true );
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
        string error;
        if ( bAdded && TextFormatter::validatePattern( textOrKey, &error ) == false )
            addIssue( origin, "text '" + string( textOrKey ) + "' is not a valid message pattern: " + error, true );
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
                // 손으로 쓴 다른 표의 키 — 참조일 뿐이다. 코드가 다른 원문을 적었으면 알린다.
                for ( const SourceStringTable* pTable : listOtherTable )
                {
                    const SourceTextEntry* pEntry   = pTable != nullptr ? pTable->findEntry( text._key ) : nullptr;
                    const bool             bDiffers = pEntry != nullptr && text._kind == GatheredTextKind::Keyed && pEntry->_source != text._source;
                    if ( bDiffers )
                        report._listIssue.push_back( { text._listOrigin.empty() ? string{} : text._listOrigin.front(),
                                                       "key '" + text._key + "' is defined by another string table with a different source - the table wins", false } );
                }
                continue;
            }

            const auto       entryIt = inoutGatherTable.getMutableEntries().find( text._key );
            SourceTextEntry* pEntry  = entryIt != inoutGatherTable.getMutableEntries().end() ? &entryIt->second : nullptr;
            if ( pEntry == nullptr && text._kind == GatheredTextKind::KeyReference )
            {
                report._listIssue.push_back( { text._listOrigin.empty() ? string{} : text._listOrigin.front(),
                                               "key '" + text._key + "' is referenced but no string table defines it", true } );
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
                std::sort( added._listOrigin.begin(), added._listOrigin.end() );
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
            std::sort( pEntry->_listOrigin.begin(), pEntry->_listOrigin.end() );
        }

        // 수집기가 넣었던 줄(자리가 있는 줄)이 이번에 나오지 않았으면 지운다. 손으로 넣은 줄은 둔다.
        vector<string> listStale;
        for ( const auto& [key, entry] : inoutGatherTable.getEntries() )
        {
            if ( entry._listOrigin.empty() == false && uniqueSeen.find( key ) == uniqueSeen.end() )
                listStale.push_back( key );
        }
        for ( const string& key : listStale )
        {
            (void)inoutGatherTable.removeEntry( key );
            report._listRemoved.push_back( key );
        }
        return report;
    }
} // namespace sw
