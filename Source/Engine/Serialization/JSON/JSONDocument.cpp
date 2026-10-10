#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Resource/ResourceUtil.h"

#include <nlohmann/json.hpp>

namespace sw
{
    namespace
    {
        struct JSONDocumentInternal
        {
            /** @brief json 의 문자열 타입입니다. 키 · 문자열 값 · `dump` 결과가 sw 할당자를 지납니다. */
            using JSONString = std::basic_string<utf8, std::char_traits<utf8>, Allocator<utf8>>;

            /** @brief 실수를 정수 범위로 잘라 바꿉니다. 범위 밖 실수를 그대로 `static_cast` 하면 정의되지 않은 동작입니다(`1e20` → int64). NaN 은 @p fallback 입니다. */
            static int64 toInt64Saturated( float64 value, int64 fallback )
            {
                if ( value != value )
                    return fallback;
                if ( value >= static_cast<float64>( std::numeric_limits<int64>::max() ) )
                    return std::numeric_limits<int64>::max();
                if ( value <= static_cast<float64>( std::numeric_limits<int64>::min() ) )
                    return std::numeric_limits<int64>::min();
                return static_cast<int64>( value );
            }

            /** @brief 위와 같되 부호 없는 범위입니다(음수는 0). */
            static uint64 toUint64Saturated( float64 value, uint64 fallback )
            {
                if ( value != value )
                    return fallback;
                if ( value >= static_cast<float64>( std::numeric_limits<uint64>::max() ) )
                    return std::numeric_limits<uint64>::max();
                if ( value <= 0.0 )
                    return 0;
                return static_cast<uint64>( value );
            }
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

            static bool nameEquals( string_view lhs, string_view rhs, bool bIgnoreCase )
            {
                return StringUtil::equals( lhs, rhs, bIgnoreCase );
            }

            static string fromJSONString( const JSONString& value )
            {
                return string( value.data(), value.size() );
            }

            static JSONString toJSONString( string_view value )
            {
                return JSONString( value.data(), value.size() );
            }

            static JSONImpl* findMember( JSONImpl* pObj, string_view key, bool bIgnoreCase )
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

            static string findExistingKey( JSONImpl* pObj, string_view key, bool bIgnoreCase )
            {
                if ( pObj == nullptr || pObj->is_object() == false )
                    return string( key );
                if ( bIgnoreCase == false )
                    return string( key );
                for ( auto it = pObj->begin(); it != pObj->end(); ++it )
                {
                    if ( nameEquals( it.key(), key, true ) )
                        return fromJSONString( it.key() );
                }
                return string( key );
            }

            static string dumpValue( const JSONImpl& value, int32 indent )
            {
                if ( indent < 0 )
                    return fromJSONString( value.dump() );
                return fromJSONString( value.dump( indent ) );
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
                StringBuilder<constant::kMaxBuffer1024> message;
                message.appendFormat( "%#:%#:%#: %#", sourceName, line, column, string_view{ reason.data(), reason.size() } );
                return sw::string( message.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "JSONDocument" );

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
        return JSONDocumentInternal::fromJSONString( pValue->dump() );
    }

    int64 JSONValue::asInt( int64 fallback ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
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
            return JSONDocumentInternal::toInt64Saturated( pValue->get<float64>(), fallback );
        return fallback;
    }

    uint64 JSONValue::asUint( uint64 fallback ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_number() == false )
            return fallback;
        if ( pValue->is_number_unsigned() )
            return pValue->get<uint64>();
        if ( pValue->is_number_integer() )
            return static_cast<uint64>( pValue->get<int64>() );
        if ( pValue->is_number_float() )
            return JSONDocumentInternal::toUint64Saturated( pValue->get<float64>(), fallback );
        return fallback;
    }

    float64 JSONValue::asFloat( float64 fallback ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_number() == false )
            return fallback;
        if ( pValue->is_number_float() )
            return pValue->get<float64>();
        // 부호 없는 쪽이 먼저다(이유는 `asInt` 참고). 순서가 뒤집히면 `18446744073709551615` 가 `-1.0` 으로 돌아온다.
        if ( pValue->is_number_unsigned() )
            return static_cast<float64>( pValue->get<uint64>() );
        if ( pValue->is_number_integer() )
            return static_cast<float64>( pValue->get<int64>() );
        return fallback;
    }

    bool JSONValue::asBool( bool fallback ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_boolean() == false )
            return fallback;
        return pValue->get<bool>();
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

    JSONValue JSONValue::get( string_view key, bool bIgnoreCaseKeys ) const
    {
        return JSONValue{ JSONDocumentInternal::findMember( JSONDocumentInternal::asJSON( _pValue ), key, bIgnoreCaseKeys ) };
    }

    bool JSONValue::has( string_view key, bool bIgnoreCaseKeys ) const
    {
        return get( key, bIgnoreCaseKeys ).isValid();
    }

    JSONValue JSONValue::at( size_t index ) const
    {
        JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr || pValue->is_array() == false || index >= pValue->size() )
            return {};
        return JSONValue{ &( *pValue )[index] };
    }

    string JSONValue::dump( int32 indent ) const
    {
        const JSONImpl* pValue = JSONDocumentInternal::asJSON( _pValue );
        if ( pValue == nullptr )
            return "null";
        return JSONDocumentInternal::dumpValue( *pValue, indent );
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

    JSONValue JSONValue::set( string_view key, bool bIgnoreCaseKeys ) const
    {
        JSONImpl* pJSON = JSONDocumentInternal::asJSON( _pValue );
        if ( pJSON == nullptr )
            return {};
        if ( pJSON->is_object() == false )
            *pJSON = JSONImpl::object();
        const string storedKey = JSONDocumentInternal::findExistingKey( pJSON, key, bIgnoreCaseKeys );
        JSONImpl&    child     = ( *pJSON )[JSONDocumentInternal::toJSONString( storedKey )];
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
        : _impl{ make_unique<Impl>() } {}

    JSONDocument::~JSONDocument() = default;

    JSONDocument::JSONDocument( JSONDocument&& other ) noexcept
        : _impl{ std::move( other._impl ) }
        , _lastError{ std::move( other._lastError ) } {}

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
            SW_LOG_ERROR( "JSON parse error at %#", _lastError );
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

    bool JSONDocument::loadFile( string_view absPath )
    {
        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
        {
            _lastError = string( absPath ) + ": cannot read the file";
            return false;
        }
        return parse( text, absPath );
    }

    bool JSONDocument::loadResource( string_view relativePath, string* pOutAbsPath )
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

    bool JSONDocument::loadPath( string_view path, string* pOutAbsPath )
    {
        if ( path.empty() )
        {
            _lastError = "empty path";
            return false;
        }
        if ( FileUtil::exists( path ) )
        {
            if ( pOutAbsPath != nullptr )
                *pOutAbsPath = string{ path };
            return loadFile( path );
        }
        return loadResource( path, pOutAbsPath );
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

    string JSONDocument::dump( int32 indent ) const
    {
        if ( _impl == nullptr )
            return "null";
        return JSONDocumentInternal::dumpValue( _impl->root, indent );
    }

    bool JSONDocument::saveFile( string_view absPath, int32 indent ) const
    {
        if ( absPath.empty() )
            return false;
        return FileUtil::writeTextFile( absPath, dump( indent ) );
    }

    string JSONDocument::escapeString( string_view value )
    {
        const JSONImpl quoted = JSONDocumentInternal::toJSONString( value );
        string         dumped = JSONDocumentInternal::fromJSONString( quoted.dump() );
        if ( dumped.size() >= 2 && dumped.front() == '"' && dumped.back() == '"' )
            dumped = dumped.substr( 1, dumped.size() - 2 );
        return dumped;
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

    string JSONDocument::extractStringField( string_view json, string_view fieldName,
                                             bool bIgnoreCaseKeys )
    {
        JSONDocument doc;
        if ( doc.parse( json ) == false )
            return {};
        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
            return {};
        const JSONValue field = root.get( fieldName, bIgnoreCaseKeys );
        if ( field.isValid() == false )
            return {};
        if ( field.isObject() || field.isArray() )
            return {};
        return field.asString();
    }

} // namespace sw
