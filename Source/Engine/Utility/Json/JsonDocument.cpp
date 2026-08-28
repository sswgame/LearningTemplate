#include "pch.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include <nlohmann/json.hpp>

namespace sw
{
	SW_LOG_CALLER( "JsonDocument" );

	using JsonImpl = nlohmann::ordered_json;

	namespace
	{

		JsonImpl* asJson( void* pPtr )
		{
			return static_cast<JsonImpl*>( pPtr );
		}

		bool nameEquals( string_view lhs, string_view rhs, bool bIgnoreCase )
		{
			if ( lhs.size() != rhs.size() )
				return false;
			if ( bIgnoreCase == false )
				return lhs == rhs;
			for ( size_t elementIndex = 0; elementIndex < lhs.size(); ++elementIndex )
			{
				if ( StringUtil::toLowerChar( lhs[elementIndex] ) != StringUtil::toLowerChar( rhs[elementIndex] ) )
					return false;
			}
			return true;
		}

		string fromStdString( const std::string& value )
		{
			return string( value.data(), value.size() );
		}

		std::string toStdString( string_view value )
		{
			return std::string( value.data(), value.size() );
		}

		JsonImpl* findMember( JsonImpl* pObj, string_view key, bool bIgnoreCase )
		{
			if ( pObj == nullptr || pObj->is_object() == false )
				return nullptr;
			for ( auto it = pObj->begin(); it != pObj->end(); ++it )
			{
				if ( nameEquals( it.key(), key, bIgnoreCase ) )
					return &( *it );
			}
			return nullptr;
		}

		string findExistingKey( JsonImpl* pObj, string_view key, bool bIgnoreCase )
		{
			if ( pObj == nullptr || pObj->is_object() == false )
				return string( key );
			if ( bIgnoreCase == false )
				return string( key );
			for ( auto it = pObj->begin(); it != pObj->end(); ++it )
			{
				if ( nameEquals( it.key(), key, true ) )
					return fromStdString( it.key() );
			}
			return string( key );
		}

		string dumpValue( const JsonImpl& value, int32 indent )
		{
			if ( indent < 0 )
				return fromStdString( value.dump() );
			return fromStdString( value.dump( indent ) );
		}

	} // namespace

	struct JsonDocument::Impl
	{
		JsonImpl root{ nullptr };
	};

	JsonType JsonValue::type() const
	{
		const JsonImpl* pValue = asJson( _pValue );
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
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr )
			return {};
		if ( pValue->is_string() )
			return fromStdString( pValue->get<std::string>() );
		return fromStdString( pValue->dump() );
	}

	int64 JsonValue::asInt( int64 fallback ) const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_number() == false )
			return fallback;
		return pValue->get<int64>();
	}

	uint64 JsonValue::asUint( uint64 fallback ) const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_number() == false )
			return fallback;
		return pValue->get<uint64>();
	}

	float64 JsonValue::asFloat( float64 fallback ) const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_number() == false )
			return fallback;
		return pValue->get<float64>();
	}

	bool JsonValue::asBool( bool fallback ) const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_boolean() == false )
			return fallback;
		return pValue->get<bool>();
	}

	size_t JsonValue::size() const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr )
			return 0;
		return pValue->size();
	}

	vector<string> JsonValue::memberNames() const
	{
		vector<string>	listNames;
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_object() == false )
			return listNames;
		for ( auto it = pValue->begin(); it != pValue->end(); ++it )
		{
			listNames.push_back( fromStdString( it.key() ) );
		}
		return listNames;
	}

	JsonValue JsonValue::get( string_view key, bool bIgnoreCaseKeys ) const
	{
		return JsonValue{ findMember( asJson( _pValue ), key, bIgnoreCaseKeys ) };
	}

	bool JsonValue::has( string_view key, bool bIgnoreCaseKeys ) const
	{
		return get( key, bIgnoreCaseKeys ).isValid();
	}

	JsonValue JsonValue::at( size_t index ) const
	{
		JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr || pValue->is_array() == false || index >= pValue->size() )
			return {};
		return JsonValue{ &( *pValue )[index] };
	}

	string JsonValue::dump( int32 indent ) const
	{
		const JsonImpl* pValue = asJson( _pValue );
		if ( pValue == nullptr )
			return "null";
		return dumpValue( *pValue, indent );
	}

	void JsonValue::assignFrom( const JsonValue& other ) const
	{
		JsonImpl*		pDst = asJson( _pValue );
		const JsonImpl* pSrc = asJson( other._pValue );
		if ( pDst == nullptr )
			return;
		if ( pSrc == nullptr )
			*pDst = nullptr;
		else
			*pDst = *pSrc;
	}

	void JsonValue::setNull() const
	{
		JsonImpl* pValue = asJson( _pValue );
		if ( pValue != nullptr )
			*pValue = nullptr;
	}

	void JsonValue::setBool( bool value ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = value;
	}

	void JsonValue::setInt( int64 value ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = value;
	}

	void JsonValue::setUint( uint64 value ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = value;
	}

	void JsonValue::setFloat( float64 value ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = value;
	}

	void JsonValue::setString( string_view value ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = toStdString( value );
	}

	void JsonValue::setObject() const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = JsonImpl::object();
	}

	void JsonValue::setArray() const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson != nullptr )
			*pJson = JsonImpl::array();
	}

	JsonValue JsonValue::set( string_view key, bool bIgnoreCaseKeys ) const
	{
		JsonImpl* pJson = asJson( _pValue );
		if ( pJson == nullptr )
			return {};
		if ( pJson->is_object() == false )
			*pJson = JsonImpl::object();
		const string storedKey = findExistingKey( pJson, key, bIgnoreCaseKeys );
		JsonImpl&	 child	   = ( *pJson )[toStdString( storedKey )];
		return JsonValue{ &child };
	}

	JsonValue JsonValue::pushBack() const
	{
		JsonImpl* pJson = asJson( _pValue );
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
		: _impl{ std::move( other._impl ) } {}

	JsonDocument& JsonDocument::operator=( JsonDocument&& other ) noexcept
	{
		if ( this != &other )
			_impl = std::move( other._impl );
		return *this;
	}

	void JsonDocument::clear()
	{
		if ( _impl == nullptr )
			_impl = make_unique<Impl>();
		else
			_impl->root = nullptr;
	}

	bool JsonDocument::parse( string_view jsonText )
	{
		if ( jsonText.empty() )
		{
			clear();
			return false;
		}

		if ( _impl == nullptr )
			_impl = make_unique<Impl>();
		_impl->root = JsonImpl::parse( toStdString( jsonText ), nullptr, false, false );
		if ( _impl->root.is_discarded() )
		{
			SW_LOG_ERROR( "Parse error in json text" );
			clear();
			return false;
		}
		return true;
	}

	bool JsonDocument::loadFile( string_view absPath )
	{
		string text;
		if ( FileUtil::readTextFile( absPath, text ) == false )
			return false;
		return parse( text );
	}

	bool JsonDocument::loadResource( string_view relativePath, string* pOutAbsPath )
	{
		string text;
		string absPath;
		if ( ResourceUtil::readTextResource( relativePath, text, &absPath ) == false )
			return false;
		if ( pOutAbsPath != nullptr )
			*pOutAbsPath = absPath;
		return parse( text );
	}

	JsonValue JsonDocument::root() const
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
		return root();
	}

	JsonValue JsonDocument::makeArray()
	{
		if ( _impl == nullptr )
			_impl = make_unique<Impl>();
		_impl->root = JsonImpl::array();
		return root();
	}

	string JsonDocument::dump( int32 indent ) const
	{
		if ( _impl == nullptr )
			return "null";
		return dumpValue( _impl->root, indent );
	}

	string JsonDocument::escapeString( string_view value )
	{
		const JsonImpl quoted = toStdString( value );
		string		   dumped = fromStdString( quoted.dump() );
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
		const JsonImpl parsed = JsonImpl::parse( toStdString( quoted ), nullptr, false, false );
		if ( parsed.is_discarded() == false && parsed.is_string() )
			return fromStdString( parsed.get<std::string>() );
		return string( value );
	}

	string JsonDocument::extractStringField( string_view json, string_view fieldName,
											 bool bIgnoreCaseKeys )
	{
		JsonDocument doc;
		if ( doc.parse( json ) == false )
			return {};
		const JsonValue root = doc.root();
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
