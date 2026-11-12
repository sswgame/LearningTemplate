#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"

#include <nlohmann/json.hpp>

namespace sw
{
    namespace
    {
        struct JSONDocumentInternal
        {
            /** @brief json 의 문자열 타입입니다. 키 · 문자열 값 · `dump` 결과가 sw 할당자를 지납니다. */
            using JSONString = std::basic_string<utf8, std::char_traits<utf8>, Allocator<utf8>>;

            /**
             * @brief `nlohmann::ordered_json` 과 같은 모양(키 순서 보존)이되 문자열 · 배열 · 객체 · 이진 값이 sw 할당자를 지나는 json 타입입니다.
             * @details 할당자 인자만 바꾼 것이라 동작은 ordered_json 과 같습니다. nlohmann 을 include 하는 곳은 이 파일 하나입니다.
             *          파서 내부의 작은 버퍼(토큰 바이트 · 상태 스택)는 nlohmann 이 `std::vector` 를 고정으로 써 CRT 에 남습니다.
             */
            using JSONImpl = nlohmann::basic_json<nlohmann::ordered_map, std::vector, JSONString, bool, int64, uint64, float64, Allocator,
                                                  nlohmann::adl_serializer, std::vector<uint8, Allocator<uint8>>>;

            static JSONImpl* asJSON( void* pPtr )
            {
                return static_cast<JSONImpl*>( pPtr );
            }

            static string fromJSONString( const JSONString& value )
            {
                return string( value.data(), value.size() );
            }

            static JSONString toJSONString( string_view value )
            {
                return JSONString( value.data(), value.size() );
            }

            /** @brief 키(철자 그대로)로 멤버를 찾습니다. 대소문자를 무시하면 같은 칸을 여러 철자로 적는 데이터가 쌓인다(별칭). */
            static JSONImpl* findMember( JSONImpl* pObj, string_view key )
            {
                if ( pObj == nullptr || pObj->is_object() == false )
                    return nullptr;
                auto it = pObj->find( std::string_view( key.data(), key.size() ) );
                if ( it != pObj->end() )
                    return &( *it );
                return nullptr;
            }

            static string_view toStringView( const JSONString& value )
            {
                return string_view( value.data(), value.size() );
            }

            /**
             * @brief 키 · 문자열 값 가운데 잘못된 UTF-8 인 첫 글을 찾아, 로그에 실을 수 있게 바꾼 글(`StringUtil::escapeInvalidUTF8`)로 돌려줍니다.
             * @details 재귀 대신 스택으로 돈다 — 파서가 받은 깊은 중첩에서 C 스택을 쓰지 않는다.
             */
            static optional<string> findInvalidUTF8( const JSONImpl& root )
            {
                vector<const JSONImpl*> listPending{ &root };
                while ( listPending.empty() == false )
                {
                    const JSONImpl* const pValue = listPending.back();
                    listPending.pop_back();
                    if ( pValue->is_string() )
                    {
                        const string_view text = toStringView( pValue->get_ref<const JSONString&>() );
                        if ( StringUtil::isValidUTF8( text ) == false )
                            return StringUtil::escapeInvalidUTF8( text );
                    }
                    else if ( pValue->is_object() )
                    {
                        for ( auto it = pValue->cbegin(); it != pValue->cend(); ++it )
                        {
                            const string_view key = toStringView( it.key() );
                            if ( StringUtil::isValidUTF8( key ) == false )
                                return StringUtil::escapeInvalidUTF8( key );
                            listPending.push_back( &it.value() );
                        }
                    }
                    else if ( pValue->is_array() )
                    {
                        for ( const JSONImpl& element : *pValue )
                        {
                            listPending.push_back( &element );
                        }
                    }
                }
                return nullopt;
            }

            /**
             * @brief 값을 JSON 글로 씁니다. 키 · 문자열에 잘못된 UTF-8 바이트가 있으면 오류를 돌려줍니다.
             * @details nlohmann 의 기본(`strict`)은 잘못된 바이트에서 `type_error 316` 을 던지는데 받는 곳이 없고, 웹(Emscripten 기본)은 예외를 받지도
             *          못한다. 그래서 먼저 검사해 오류 값으로 돌려주고, 던지는 길 자체를 두지 않는다(검사를 지난 글에는 `replace` 가 닿지 않는다).
             *          저장 글은 바이트를 몰래 U+FFFD 로 바꾸지 않고 거절한다(바뀐 설정 · 프리팹이 조용히 디스크에 남는다). 언리얼 `TJsonWriter` · Godot
             *          `JSON.stringify` 도 쓰기에서 프로세스를 끝내지 않는다. 사람이 읽는 글(로그 · 오류 세부)은 `describeValue` 가 바꿔 쓴다.
             */
            static expected<string> dumpValue( const JSONImpl& value, int32 indent )
            {
                const optional<string> invalidText = findInvalidUTF8( value );
                if ( invalidText.has_value() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "JSON string is not valid UTF-8", *invalidText } );
                return fromJSONString( value.dump( indent, ' ', false, JSONImpl::error_handler_t::replace ) ); // 음수 indent 는 한 줄이다
            }

            /** @brief 로그 · 오류 세부에 넣을 한 줄 글입니다. 잘못된 UTF-8 은 U+FFFD 로 바꿔 던지지 않습니다(`JSONWriter` 와 같은 규칙). */
            static string describeValue( const JSONImpl& value )
            {
                return fromJSONString( value.dump( JSONDocument::kSingleLineIndent, ' ', false, JSONImpl::error_handler_t::replace ) );
            }

            /**
             * @brief 실패한 글의 오류 자리를 찾을 때만 쓰는 json 타입입니다.
             * @details nlohmann 의 `sax_parse` 는 입력 형식을 실행 중에 고르므로 이진 형식 읽기(UBJSON)까지 인스턴스를 만드는데, 그 길은 문자열 타입이
             *          `std::string` 이어야 컴파일됩니다. 실패한 글을 한 번 더 읽는 드문 길이라 표준 할당자로 둡니다.
             */
            using ErrorScanJSON = nlohmann::ordered_json;

            /**
             * @brief 실패한 글을 SAX 로 다시 읽어 오류 자리(읽은 바이트 수)와 이유를 받는 처리기입니다. 값은 만들지 않습니다.
             * @note 함수 이름은 nlohmann 의 SAX 규약이다(`sax_parse` 가 이 이름들을 부른다).
             */
            struct ParseErrorRecorder
            {
                size_t     _position{ 0 };
                sw::string _message;

                bool null() { return true; }
                bool boolean( bool ) { return true; }
                bool number_integer( ErrorScanJSON::number_integer_t ) { return true; }
                bool number_unsigned( ErrorScanJSON::number_unsigned_t ) { return true; }
                bool number_float( ErrorScanJSON::number_float_t, const ErrorScanJSON::string_t& ) { return true; }
                bool string( ErrorScanJSON::string_t& ) { return true; }
                bool binary( ErrorScanJSON::binary_t& ) { return true; }
                bool start_object( std::size_t ) { return true; }
                bool key( ErrorScanJSON::string_t& ) { return true; }
                bool end_object() { return true; }
                bool start_array( std::size_t ) { return true; }
                bool end_array() { return true; }
                bool parse_error( std::size_t position, const std::string&, const nlohmann::detail::exception& error )
                {
                    _position = position;
                    _message  = error.what();
                    return false;
                }
            };

            /**
             * @brief 파싱에 실패한 글의 오류를 `이름:줄:열: 이유` 로 만듭니다. 실패했을 때만 부릅니다(글을 한 번 더 읽는다).
             * @details 값을 만드는 `parse( …, allow_exceptions = false )` 는 실패하면 버려진 값만 주고 자리 · 이유를 주지 않는다.
             */
            static sw::string describeParseError( string_view text, string_view sourceName )
            {
                ParseErrorRecorder recorder;
                (void)ErrorScanJSON::sax_parse( text.data(), text.data() + text.size(), &recorder );
                // what() 은 "[json.exception.parse_error.101] parse error at line 3, column 5: <이유>" 다. 줄 · 열은 우리 꼴로 다시 적으므로 이유만 남긴다.
                std::string_view reason{ recorder._message };
                const size_t     columnPos = reason.find( "column " );
                const size_t     reasonPos = columnPos != std::string_view::npos ? reason.find( ": ", columnPos ) : std::string_view::npos;
                if ( reasonPos != std::string_view::npos )
                    reason = reason.substr( reasonPos + 2 );
                // 자리는 읽은 바이트 수다 — 틀린 글자는 그 직전 바이트다.
                uint32 line   = 0;
                uint32 column = 0;
                StringUtil::getLineAndColumn( string_view{ text.data(), text.size() }, recorder._position > 0 ? recorder._position - 1 : 0, line, column );
                StringBuilder<constant::buffer::kMax1024> message;
                message.appendFormat( "%#:%#:%#: %#", sourceName, line, column, string_view{ reason.data(), reason.size() } );
                return sw::string( message.c_str() );
            }
        };

        /** @brief 멤버 값 하나를 타입으로 읽습니다. 정수 칸은 실수 · 범위 밖을 받지 않는다(조용히 자르지 않는다). 결과는 0 읽음 · 1 없음 · 2 깨짐입니다. */
        struct JSONMemberReader
        {
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, int64& outValue )
            {
                const bool bSignedFits = json.is_number_integer() && ( json.is_number_unsigned() == false || json.get<uint64>() <= static_cast<uint64>( MathUtil::kMaxInt64 ) );
                if ( bSignedFits )
                    outValue = json.is_number_unsigned() ? static_cast<int64>( json.get<uint64>() ) : json.get<int64>();
                return bSignedFits;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, uint64& outValue )
            {
                const bool bUnsigned = json.is_number_unsigned() || ( json.is_number_integer() && json.get<int64>() >= 0 );
                if ( bUnsigned )
                    outValue = json.is_number_unsigned() ? json.get<uint64>() : static_cast<uint64>( json.get<int64>() );
                return bUnsigned;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, int32& outValue )
            {
                int64      wide{ 0 };
                const bool bFits = parse( json, wide ) && MathUtil::kMinInt32 <= wide && wide <= MathUtil::kMaxInt32;
                outValue         = static_cast<int32>( wide );
                return bFits;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, uint32& outValue )
            {
                uint64     wide{ 0 };
                const bool bFits = parse( json, wide ) && wide <= MathUtil::kMaxUInt32;
                outValue         = static_cast<uint32>( wide );
                return bFits;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, float64& outValue )
            {
                if ( json.is_number() == false )
                    return false;
                outValue = json.is_number_float()    ? json.get<float64>()
                         : json.is_number_unsigned() ? static_cast<float64>( json.get<uint64>() )
                                                     : static_cast<float64>( json.get<int64>() );
                return true;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, float32& outValue )
            {
                float64    wide{ 0.0 };
                const bool bNumber = parse( json, wide );
                outValue           = static_cast<float32>( wide );
                return bNumber;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, bool& outValue )
            {
                if ( json.is_boolean() == false )
                    return false;
                outValue = json.get<bool>();
                return true;
            }
            [[nodiscard]] static bool parse( const JSONDocumentInternal::JSONImpl& json, string& outValue )
            {
                if ( json.is_string() == false )
                    return false;
                const JSONDocumentInternal::JSONString& text = json.get_ref<const JSONDocumentInternal::JSONString&>();
                outValue                                     = string( text.data(), text.size() );
                return true;
            }

            static const utf8* getTypeLabel( const int32& ) { return "32-bit integer"; }
            static const utf8* getTypeLabel( const uint32& ) { return "32-bit unsigned integer"; }
            static const utf8* getTypeLabel( const int64& ) { return "integer"; }
            static const utf8* getTypeLabel( const uint64& ) { return "unsigned integer"; }
            static const utf8* getTypeLabel( const float32& ) { return "number"; }
            static const utf8* getTypeLabel( const float64& ) { return "number"; }
            static const utf8* getTypeLabel( const bool& ) { return "boolean"; }
            static const utf8* getTypeLabel( const string& ) { return "string"; }

            /** @brief 값 하나를 읽습니다. 결과는 0 읽음 · 1 없음(무효 · null) · 2 깨짐입니다. @p key 가 비면 키 없는 값(배열 원소 등)이다. */
            template <typename T>
            static uint8 read( const JSONDocumentInternal::JSONImpl* pMember, string_view key, T& outValue )
            {
                if ( pMember == nullptr || pMember->is_null() )
                    return 1;
                if ( parse( *pMember, outValue ) )
                    return 0;
                if ( key.empty() )
                    SW_LOG_ERROR_ID( "JSON.WrongType", "JSON value is not a %#: %#", getTypeLabel( outValue ), JSONDocumentInternal::describeValue( *pMember ) );
                else
                    SW_LOG_ERROR_ID( "JSON.WrongType", "JSON member '%#' is not a %#: %#", key, getTypeLabel( outValue ), JSONDocumentInternal::describeValue( *pMember ) );
                return 2;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    using JSONImpl = JSONDocumentInternal::JSONImpl;

    struct JSONDocument::Impl
    {
        JSONImpl root{ nullptr };
    };

    JSONType JSONValue::getType() const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_null() )
            return JSONType::Null;
        if ( pValue->is_boolean() )
            return JSONType::Bool;
        if ( pValue->is_number() )
            return JSONType::Number;
        if ( pValue->is_string() )
            return JSONType::String;
        if ( pValue->is_array() )
            return JSONType::Array;
        if ( pValue->is_object() )
            return JSONType::Object;
        return JSONType::Null;
    }

    string JSONValue::asString() const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr )
            return {};
        if ( pValue->is_string() )
            return JSONDocumentInternal::fromJSONString( pValue->get_ref<const JSONDocumentInternal::JSONString&>() );
        return JSONDocumentInternal::describeValue( *pValue );
    }

    // 정수 읽기는 **부호 없는 쪽을 먼저 본다**(`JSONMemberReader::parse`). nlohmann 의 `is_number_integer()` 는 부호 있는 정수와 부호 없는 정수
    // 둘 다에 참이라, 순서가 뒤집히면 큰 부호 없는 값이 음수로 돌아온다.
#define SW_JSON_MEMBER_READER( Type )                                                                                                    \
    JSONValue::JSONValueRead JSONValue::parseMember( string_view key, Type& outValue ) const                                             \
    {                                                                                                                                    \
        const JSONValue member = get( key );                                                                                             \
        return static_cast<JSONValueRead>( JSONMemberReader::read( JSONDocumentInternal::asJSON( member._pValue ), key, outValue ) );    \
    }                                                                                                                                    \
    JSONValue::JSONValueRead JSONValue::parseSelf( Type& outValue ) const                                                                \
    {                                                                                                                                    \
        return static_cast<JSONValueRead>( JSONMemberReader::read( JSONDocumentInternal::asJSON( _pValue ), string_view{}, outValue ) ); \
    }
    SW_JSON_MEMBER_READER( int32 )
    SW_JSON_MEMBER_READER( uint32 )
    SW_JSON_MEMBER_READER( int64 )
    SW_JSON_MEMBER_READER( uint64 )
    SW_JSON_MEMBER_READER( float32 )
    SW_JSON_MEMBER_READER( float64 )
    SW_JSON_MEMBER_READER( bool )
    SW_JSON_MEMBER_READER( string )
#undef SW_JSON_MEMBER_READER

    void JSONValue::reportMissing( string_view key ) const
    {
        SW_LOG_ERROR_ID( "JSON.MissingMember", "JSON object has no required member '%#'", key );
    }

    size_t JSONValue::size() const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr )
            return 0;
        return pValue->size();
    }

    vector<string> JSONValue::getMemberNames() const
    {
        vector<string>  listName;
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_object() == false )
            return listName;
        for ( auto it = pValue->begin(); it != pValue->end(); ++it )
        {
            listName.push_back( JSONDocumentInternal::fromJSONString( it.key() ) );
        }
        return listName;
    }

    JSONValue JSONValue::get( string_view key ) const
    {
        return JSONValue{ JSONDocumentInternal::findMember( JSONDocumentInternal::asJSON( _pValue ), key ) };
    }

    bool JSONValue::has( string_view key ) const
    {
        return get( key ).isValid();
    }

    bool JSONValue::findPathText( string_view dottedPath, string& outText ) const
    {
        JSONValue current = *this;
        while ( dottedPath.empty() == false )
        {
            const size_t      dot  = dottedPath.find( '.' );
            const string_view part = dottedPath.substr( 0, dot );
            dottedPath             = dot == string_view::npos ? string_view{} : dottedPath.substr( dot + 1 );
            if ( current.isObject() == false )
                return false;
            current = current.get( part );
            if ( current.isValid() == false )
                return false;
        }
        if ( current.isString() == false && current.isNumber() == false )
            return false;
        outText = current.asString();
        return true;
    }

    JSONValue JSONValue::at( size_t index ) const
    {
        JSONImpl*  pValue             = JSONDocumentInternal::asJSON( _pValue );
        const bool bInvalidArrayIndex = pValue == nullptr || pValue->is_array() == false || index >= pValue->size();
        if ( bInvalidArrayIndex )
            return {};
        return JSONValue{ &( *pValue )[index] };
    }

    bool JSONValue::readFloats( size_t offset, float32* pOutValue, size_t count ) const
    {
        const size_t totalCount  = size();
        const bool   bOutOfRange = isArray() == false || offset > totalCount || count > totalCount - offset;
        if ( bOutOfRange )
            return false;
        for ( size_t index = 0; index < count; ++index )
        {
            const optional<float32> number = at( offset + index ).read<float32>();
            if ( number.has_value() == false )
                return false;
            pOutValue[index] = *number;
        }
        return true;
    }

    bool JSONValue::readFloat3( size_t offset, float3& outValue ) const
    {
        float32 arrValue[3];
        if ( readFloats( offset, arrValue, 3 ) == false )
            return false;
        outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    expected<string> JSONValue::dump( int32 indent ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr )
            return string( "null" );
        return JSONDocumentInternal::dumpValue( *pValue, indent );
    }

    string JSONValue::describe() const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr )
            return "null";
        return JSONDocumentInternal::describeValue( *pValue );
    }

    void JSONValue::assignFrom( const JSONValue& other ) const
    {
        JSONImpl*       pDst = JSONDocumentInternal::asJSON( _pValue );
        const JSONImpl* pSrc = JSONDocumentInternal::asJSON( other._pValue );
        if ( pDst == nullptr )
            return;
        if ( pSrc == nullptr )
            *pDst = nullptr;
        else
            *pDst = *pSrc;
    }

    void JSONValue::setNull() const
    {
        JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue != nullptr )
            *pValue = nullptr;
    }

    void JSONValue::setBool( bool value ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = value;
    }

    void JSONValue::setInt( int64 value ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = value;
    }

    void JSONValue::setUint( uint64 value ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = value;
    }

    void JSONValue::setFloat( float64 value ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = value;
    }

    void JSONValue::setString( string_view value ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = JSONDocumentInternal::toJSONString( value );
    }

    void JSONValue::setObject() const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = JSONImpl::object();
    }

    void JSONValue::setArray() const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON != nullptr )
            *pJSON = JSONImpl::array();
    }

    JSONValue JSONValue::set( string_view key ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON == nullptr )
            return {};
        if ( pJSON->is_object() == false )
            *pJSON = JSONImpl::object();
        JSONImpl& child = ( *pJSON )[JSONDocumentInternal::toJSONString( key )];
        return JSONValue{ &child };
    }

    JSONValue JSONValue::pushBack() const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON == nullptr )
            return {};
        if ( pJSON->is_array() == false )
            *pJSON = JSONImpl::array();
        pJSON->push_back( nullptr );
        return JSONValue{ &( pJSON->back() ) };
    }

    JSONDocument::JSONDocument()
        : _impl{ make_unique<Impl>() }
    {
    }

    JSONDocument::~JSONDocument() = default;

    JSONDocument::JSONDocument( JSONDocument&& other ) noexcept
        : _impl{ std::move( other._impl ) }
        , _lastError{ std::move( other._lastError ) }
    {
    }

    JSONDocument& JSONDocument::operator=( JSONDocument&& other ) noexcept
    {
        if ( this != &other )
        {
            _impl      = std::move( other._impl );
            _lastError = std::move( other._lastError );
        }
        return *this;
    }

    void JSONDocument::clear()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        else
            _impl->root = nullptr;
    }

    bool JSONDocument::parse( string_view jsonText, string_view sourceName )
    {
        _lastError.clear();
        const string_view source = sourceName.empty() ? string_view{ "<memory>" } : sourceName;
        if ( jsonText.empty() )
        {
            clear();
            _lastError = string( source ) + ": empty document";
            return false;
        }

        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JSONImpl::parse( jsonText.data(), jsonText.data() + jsonText.size(), nullptr, false, false );
        if ( _impl->root.is_discarded() )
        {
            _lastError = JSONDocumentInternal::describeParseError( jsonText, source );
            SW_LOG_ERROR_ID( "JSON.ParseError", "JSON parse error at %#", _lastError );
            clear();
            return false;
        }
        return true;
    }

    bool JSONDocument::tryParse( string_view jsonText )
    {
        _lastError.clear();
        if ( jsonText.empty() )
        {
            clear();
            return false;
        }
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JSONImpl::parse( jsonText.data(), jsonText.data() + jsonText.size(), nullptr, false, false );
        if ( _impl->root.is_discarded() )
        {
            clear();
            return false;
        }
        return true;
    }

    bool JSONDocument::loadFromFile( string_view absPath )
    {
        // 경로로 읽은 파일도 쿠킹 작업의 입력이다 — `FileUtil` 로 바로 읽으면 기록에서 빠진다(`AssetIO::readTextFile`).
        string text;
        if ( AssetIO::readTextFile( absPath, text ) == false )
        {
            _lastError = string( absPath ) + ": cannot read the file (or it was read during cooking outside a cook job)";
            return false;
        }
        return parse( text, absPath );
    }

    bool JSONDocument::loadResource( const AssetID& assetID, string* pOutAbsPath )
    {
        const string_view relativePath = assetID.view();
        if ( relativePath.empty() )
        {
            _lastError = "empty path";
            return false;
        }
        string text;
        string absPath;
        if ( AssetIO::readTextAsset( assetID, text, &absPath ) == false )
        {
            _lastError = string( relativePath ) + ": not found (no file at that path and no resource by that name)";
            return false;
        }
        if ( pOutAbsPath != nullptr )
            *pOutAbsPath = absPath;
        return parse( text, absPath );
    }

    JSONValue JSONDocument::getRoot() const
    {
        if ( _impl == nullptr )
            return {};
        return JSONValue{ &_impl->root };
    }

    JSONValue JSONDocument::makeObject()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JSONImpl::object();
        return getRoot();
    }

    JSONValue JSONDocument::makeArray()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JSONImpl::array();
        return getRoot();
    }

    expected<string> JSONDocument::dump( int32 indent ) const
    {
        if ( _impl == nullptr )
            return string( "null" );
        return JSONDocumentInternal::dumpValue( _impl->root, indent );
    }

    bool JSONDocument::saveToFile( string_view absPath, int32 indent ) const
    {
        if ( absPath.empty() )
            return false;
        // 루트가 없는 문서(읽기에 실패해 비워졌거나 아직 아무것도 안 만든)는 "null" 이 된다 - 그대로 쓰면 있던 파일이 "null" 로 바뀐다.
        if ( getRoot().isNull() )
        {
            SW_LOG_ERROR_ID( "JSONDocument.JSONDocumentNoRoot", "JSON document has no root - %# was not written", string{ absPath }.c_str() );
            return false;
        }
        const expected<string> text = dump( indent );
        if ( text.has_value() == false )
        {
            SW_LOG_ERROR_ID( "JSONDocument.DumpFailed", "JSON document could not be written - %# was not written: %# (%#)", string{ absPath }.c_str(),
                             text.error().message(), text.error().detail() );
            return false;
        }
        return FileUtil::writeTextFile( absPath, *text );
    }

    string JSONDocument::unescapeString( string_view value )
    {
        if ( value.empty() )
            return {};

        string quoted;
        quoted.reserve( value.size() + 2 );
        quoted.push_back( '"' );
        quoted.append( value.data(), value.size() );
        quoted.push_back( '"' );
        const JSONImpl parsed = JSONImpl::parse( JSONDocumentInternal::toJSONString( quoted ), nullptr, false, false );
        if ( parsed.is_discarded() == false && parsed.is_string() )
            return JSONDocumentInternal::fromJSONString( parsed.get_ref<const JSONDocumentInternal::JSONString&>() );
        return string( value );
    }

    string JSONDocument::extractStringField( string_view json, string_view fieldName )
    {
        JSONDocument doc;
        if ( doc.parse( json ) == false )
            return {};
        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
            return {};
        const JSONValue field = root.get( fieldName );
        if ( field.isValid() == false )
            return {};
        if ( field.isObject() || field.isArray() )
            return {};
        return field.asString();
    }

} // namespace sw
