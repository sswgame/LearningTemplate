#include "pch.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include <nlohmann/json.hpp>

namespace sw
{
    namespace
    {
        struct JsonDocumentInternal
        {
            /** @brief json 의 문자열 타입입니다. 키 · 문자열 값 · `dump` 결과가 sw 할당자를 지납니다. */
            using JsonString = std::basic_string<utf8, std::char_traits<utf8>, Allocator<utf8>>;
            /**
             * @brief `nlohmann::ordered_json` 과 같은 모양(키 순서 보존)이되 문자열 · 배열 · 객체 · 이진 값이 sw 할당자를 지나는 json 타입입니다.
             * @details 할당자 인자만 바꾼 것이라 동작은 ordered_json 과 같습니다. nlohmann 을 include 하는 곳은 이 파일 하나입니다.
             *          파서 내부의 작은 버퍼(토큰 바이트 · 상태 스택)는 nlohmann 이 `std::vector` 를 고정으로 써 CRT 에 남습니다.
             */
            using JsonImpl = nlohmann::basic_json<nlohmann::ordered_map, std::vector, JsonString, bool, int64, uint64, float64, Allocator,
                                                  nlohmann::adl_serializer, std::vector<uint8, Allocator<uint8>>>;

            static JsonImpl* asJson( void* pPtr )
            {
                return static_cast<JsonImpl*>( pPtr );
            }

            static bool nameEquals( string_view lhs, string_view rhs, bool bIgnoreCase )
            {
                return StringUtil::equals( lhs, rhs, bIgnoreCase );
            }

            static string fromJsonString( const JsonString& value )
            {
                return string( value.data(), value.size() );
            }

            static JsonString toJsonString( string_view value )
            {
                return JsonString( value.data(), value.size() );
            }

            static JsonImpl* findMember( JsonImpl* pObj, string_view key, bool bIgnoreCase )
            {
                if ( pObj == nullptr || pObj->is_object() == false )
                    return nullptr;
                if ( bIgnoreCase == false )
                {
                    auto it = pObj->find( std::string_view( key.data(), key.size() ) );
                    if ( it != pObj->end() )
                        return &( *it );
                    return nullptr;
                }
                for ( auto it = pObj->begin(); it != pObj->end(); ++it )
                {
                    if ( nameEquals( it.key(), key, true ) )
                        return &( *it );
                }
                return nullptr;
            }

            static string findExistingKey( JsonImpl* pObj, string_view key, bool bIgnoreCase )
            {
                if ( pObj == nullptr || pObj->is_object() == false )
                    return string( key );
                if ( bIgnoreCase == false )
                    return string( key );
                for ( auto it = pObj->begin(); it != pObj->end(); ++it )
                {
                    if ( nameEquals( it.key(), key, true ) )
                        return fromJsonString( it.key() );
                }
                return string( key );
            }

            static string dumpValue( const JsonImpl& value, int32 indent )
            {
                if ( indent < 0 )
                    return fromJsonString( value.dump() );
                return fromJsonString( value.dump( indent ) );
            }

            /**
             * @brief 실패한 글의 오류 자리를 찾을 때만 쓰는 json 타입입니다.
             * @details nlohmann 의 `sax_parse` 는 입력 형식을 실행 중에 고르므로 이진 형식 읽기(UBJSON)까지 인스턴스를 만드는데, 그 길은 문자열 타입이
             *          `std::string` 이어야 컴파일됩니다. 실패한 글을 한 번 더 읽는 드문 길이라 표준 할당자로 둡니다.
             */
            using ErrorScanJson = nlohmann::ordered_json;

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
                bool number_integer( ErrorScanJson::number_integer_t ) { return true; }
                bool number_unsigned( ErrorScanJson::number_unsigned_t ) { return true; }
                bool number_float( ErrorScanJson::number_float_t, const ErrorScanJson::string_t& ) { return true; }
                bool string( ErrorScanJson::string_t& ) { return true; }
                bool binary( ErrorScanJson::binary_t& ) { return true; }
                bool start_object( std::size_t ) { return true; }
                bool key( ErrorScanJson::string_t& ) { return true; }
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
                (void)ErrorScanJson::sax_parse( text.data(), text.data() + text.size(), &recorder );
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
                StringBuilder<constant::kMaxBuffer1024> message;
                message.appendFormat( "%#:%#:%#: %#", sourceName, line, column, string_view{ reason.data(), reason.size() } );
                return sw::string( message.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "JsonDocument" );

    using JsonImpl = JsonDocumentInternal::JsonImpl;

    struct JsonDocument::Impl
    {
        JsonImpl root{ nullptr };
    };

    JsonType JsonValue::getType() const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_null() )
            return JsonType::Null;
        if ( pValue->is_boolean() )
            return JsonType::Bool;
        if ( pValue->is_number() )
            return JsonType::Number;
        if ( pValue->is_string() )
            return JsonType::String;
        if ( pValue->is_array() )
            return JsonType::Array;
        if ( pValue->is_object() )
            return JsonType::Object;
        return JsonType::Null;
    }

    string JsonValue::asString() const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr )
            return {};
        if ( pValue->is_string() )
            return JsonDocumentInternal::fromJsonString( pValue->get_ref<const JsonDocumentInternal::JsonString&>() );
        return JsonDocumentInternal::fromJsonString( pValue->dump() );
    }

    int64 JsonValue::asInt( int64 fallback ) const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_number() == false )
            return fallback;
        // **부호 없는 쪽을 먼저 본다.** nlohmann 의 `is_number_integer()` 는 부호 있는 정수와
        // **부호 없는 정수 둘 다에 참**이다. 부호 없는 검사를 뒤에 두면 그 분기는 영영 돌지 않는다
        // (`asUint` · `asFloat` 도 같은 순서다 — 뒤집히면 큰 부호 없는 값이 음수로 돌아온다).
        if ( pValue->is_number_unsigned() )
            return static_cast<int64>( pValue->get<uint64>() );
        if ( pValue->is_number_integer() )
            return pValue->get<int64>();
        if ( pValue->is_number_float() )
            return static_cast<int64>( pValue->get<float64>() );
        return fallback;
    }

    uint64 JsonValue::asUint( uint64 fallback ) const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_number() == false )
            return fallback;
        if ( pValue->is_number_unsigned() )
            return pValue->get<uint64>();
        if ( pValue->is_number_integer() )
            return static_cast<uint64>( pValue->get<int64>() );
        if ( pValue->is_number_float() )
            return static_cast<uint64>( pValue->get<float64>() );
        return fallback;
    }

    float64 JsonValue::asFloat( float64 fallback ) const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_number() == false )
            return fallback;
        if ( pValue->is_number_float() )
            return pValue->get<float64>();
        // 부호 없는 쪽이 먼저다(이유는 `asInt` 참고). 순서가 뒤집혀 있어서
        // `18446744073709551615` 가 `-1.0` 으로 돌아왔다.
        if ( pValue->is_number_unsigned() )
            return static_cast<float64>( pValue->get<uint64>() );
        if ( pValue->is_number_integer() )
            return static_cast<float64>( pValue->get<int64>() );
        return fallback;
    }

    bool JsonValue::asBool( bool fallback ) const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_boolean() == false )
            return fallback;
        return pValue->get<bool>();
    }

    size_t JsonValue::size() const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr )
            return 0;
        return pValue->size();
    }

    vector<string> JsonValue::getMemberNames() const
    {
        vector<string>  listName;
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_object() == false )
            return listName;
        for ( auto it = pValue->begin(); it != pValue->end(); ++it )
        {
            listName.push_back( JsonDocumentInternal::fromJsonString( it.key() ) );
        }
        return listName;
    }

    JsonValue JsonValue::get( string_view key, bool bIgnoreCaseKeys ) const
    {
        return JsonValue{ JsonDocumentInternal::findMember( JsonDocumentInternal::asJson( _pValue ), key, bIgnoreCaseKeys ) };
    }

    bool JsonValue::has( string_view key, bool bIgnoreCaseKeys ) const
    {
        return get( key, bIgnoreCaseKeys ).isValid();
    }

    JsonValue JsonValue::at( size_t index ) const
    {
        JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr || pValue->is_array() == false || index >= pValue->size() )
            return {};
        return JsonValue{ &( *pValue )[index] };
    }

    string JsonValue::dump( int32 indent ) const
    {
        const JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue == nullptr )
            return "null";
        return JsonDocumentInternal::dumpValue( *pValue, indent );
    }

    void JsonValue::assignFrom( const JsonValue& other ) const
    {
        JsonImpl*       pDst = JsonDocumentInternal::asJson( _pValue );
        const JsonImpl* pSrc = JsonDocumentInternal::asJson( other._pValue );
        if ( pDst == nullptr )
            return;
        if ( pSrc == nullptr )
            *pDst = nullptr;
        else
            *pDst = *pSrc;
    }

    void JsonValue::setNull() const
    {
        JsonImpl* pValue = JsonDocumentInternal::asJson( _pValue );
        if ( pValue != nullptr )
            *pValue = nullptr;
    }

    void JsonValue::setBool( bool value ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = value;
    }

    void JsonValue::setInt( int64 value ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = value;
    }

    void JsonValue::setUint( uint64 value ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = value;
    }

    void JsonValue::setFloat( float64 value ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = value;
    }

    void JsonValue::setString( string_view value ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = JsonDocumentInternal::toJsonString( value );
    }

    void JsonValue::setObject() const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = JsonImpl::object();
    }

    void JsonValue::setArray() const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson != nullptr )
            *pJson = JsonImpl::array();
    }

    JsonValue JsonValue::set( string_view key, bool bIgnoreCaseKeys ) const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson == nullptr )
            return {};
        if ( pJson->is_object() == false )
            *pJson = JsonImpl::object();
        const string storedKey = JsonDocumentInternal::findExistingKey( pJson, key, bIgnoreCaseKeys );
        JsonImpl&    child     = ( *pJson )[JsonDocumentInternal::toJsonString( storedKey )];
        return JsonValue{ &child };
    }

    JsonValue JsonValue::pushBack() const
    {
        JsonImpl* pJson = JsonDocumentInternal::asJson( _pValue );
        if ( pJson == nullptr )
            return {};
        if ( pJson->is_array() == false )
            *pJson = JsonImpl::array();
        pJson->push_back( nullptr );
        return JsonValue{ &( pJson->back() ) };
    }

    JsonDocument::JsonDocument()
        : _impl{ make_unique<Impl>() } {}

    JsonDocument::~JsonDocument() = default;

    JsonDocument::JsonDocument( JsonDocument&& other ) noexcept
        : _impl{ std::move( other._impl ) }
        , _lastError{ std::move( other._lastError ) } {}

    JsonDocument& JsonDocument::operator=( JsonDocument&& other ) noexcept
    {
        if ( this != &other )
        {
            _impl      = std::move( other._impl );
            _lastError = std::move( other._lastError );
        }
        return *this;
    }

    void JsonDocument::clear()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        else
            _impl->root = nullptr;
    }

    bool JsonDocument::parse( string_view jsonText, string_view sourceName )
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
        _impl->root = JsonImpl::parse( jsonText.data(), jsonText.data() + jsonText.size(), nullptr, false, false );
        if ( _impl->root.is_discarded() )
        {
            _lastError = JsonDocumentInternal::describeParseError( jsonText, source );
            SW_LOG_ERROR( "JSON parse error at %#", _lastError );
            clear();
            return false;
        }
        return true;
    }

    bool JsonDocument::tryParse( string_view jsonText )
    {
        _lastError.clear();
        if ( jsonText.empty() )
        {
            clear();
            return false;
        }
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JsonImpl::parse( jsonText.data(), jsonText.data() + jsonText.size(), nullptr, false, false );
        if ( _impl->root.is_discarded() )
        {
            clear();
            return false;
        }
        return true;
    }

    bool JsonDocument::loadFile( string_view absPath )
    {
        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
        {
            _lastError = string( absPath ) + ": cannot read the file";
            return false;
        }
        return parse( text, absPath );
    }

    bool JsonDocument::loadResource( string_view relativePath, string* pOutAbsPath )
    {
        string text;
        string absPath;
        if ( ResourceUtil::readTextResource( relativePath, text, &absPath ) == false )
        {
            _lastError = string( relativePath ) + ": not found (no file at that path and no resource by that name)";
            return false;
        }
        if ( pOutAbsPath != nullptr )
            *pOutAbsPath = absPath;
        return parse( text, absPath );
    }

    bool JsonDocument::loadPath( string_view path, string* pOutAbsPath )
    {
        if ( path.empty() )
        {
            _lastError = "empty path";
            return false;
        }
        if ( FileUtil::fileExists( path ) )
        {
            if ( pOutAbsPath != nullptr )
                *pOutAbsPath = string{ path };
            return loadFile( path );
        }
        return loadResource( path, pOutAbsPath );
    }

    JsonValue JsonDocument::getRoot() const
    {
        if ( _impl == nullptr )
            return {};
        return JsonValue{ &_impl->root };
    }

    JsonValue JsonDocument::makeObject()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JsonImpl::object();
        return getRoot();
    }

    JsonValue JsonDocument::makeArray()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->root = JsonImpl::array();
        return getRoot();
    }

    string JsonDocument::dump( int32 indent ) const
    {
        if ( _impl == nullptr )
            return "null";
        return JsonDocumentInternal::dumpValue( _impl->root, indent );
    }

    bool JsonDocument::saveFile( string_view absPath, int32 indent ) const
    {
        if ( absPath.empty() )
            return false;
        return FileUtil::writeTextFile( absPath, dump( indent ) );
    }

    string JsonDocument::escapeString( string_view value )
    {
        const JsonImpl quoted = JsonDocumentInternal::toJsonString( value );
        string         dumped = JsonDocumentInternal::fromJsonString( quoted.dump() );
        if ( dumped.size() >= 2 && dumped.front() == '"' && dumped.back() == '"' )
            dumped = dumped.substr( 1, dumped.size() - 2 );
        return dumped;
    }

    string JsonDocument::unescapeString( string_view value )
    {
        if ( value.empty() )
            return {};

        string quoted;
        quoted.reserve( value.size() + 2 );
        quoted.push_back( '"' );
        quoted.append( value.data(), value.size() );
        quoted.push_back( '"' );
        const JsonImpl parsed = JsonImpl::parse( JsonDocumentInternal::toJsonString( quoted ), nullptr, false, false );
        if ( parsed.is_discarded() == false && parsed.is_string() )
            return JsonDocumentInternal::fromJsonString( parsed.get_ref<const JsonDocumentInternal::JsonString&>() );
        return string( value );
    }

    string JsonDocument::extractStringField( string_view json, string_view fieldName,
                                             bool bIgnoreCaseKeys )
    {
        JsonDocument doc;
        if ( doc.parse( json ) == false )
            return {};
        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
            return {};
        const JsonValue field = root.get( fieldName, bIgnoreCaseKeys );
        if ( field.isValid() == false )
            return {};
        if ( field.isObject() || field.isArray() )
            return {};
        return field.asString();
    }

} // namespace sw
