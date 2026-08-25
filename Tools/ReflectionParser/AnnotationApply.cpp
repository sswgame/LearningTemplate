
#include "AnnotationApply.h"
#include "AnnotationMeta.h"
#include "ParserDefines.h"

#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

namespace sw
{
	string annotationArgText( const string& spelling, const utf8* prefix )
	{
		const size_t pos = spelling.find( prefix );
		if ( pos == string::npos )
			return {};
		return spelling.substr( pos + StringUtil::strlen( prefix ) );
	}
	/**
	 * @brief `key="value"` 또는 `key=value` 형태의 단일 토큰에서 따옴표를 고려하여 값 문자열을 추출합니다.
	 */
	static string parseAnnotationStringValue( const string& token, size_t eqPos )
	{
		size_t valueStart = eqPos + 1;
		while ( valueStart < token.size() && ( token[valueStart] == ' ' || token[valueStart] == '\t' ) )
			++valueStart;

		if ( valueStart >= token.size() )
			return {};

		if ( token[valueStart] == '"' )
		{
			const size_t endQuote = token.find( '"', valueStart + 1 );
			if ( endQuote == string::npos )
				return {};
			return token.substr( valueStart + 1, endQuote - valueStart - 1 );
		}

		size_t valueEnd = valueStart;
		while ( valueEnd < token.size() )
		{
			const utf8 c = token[valueEnd];
			if ( c == ' ' || c == '\t' )
				break;
			++valueEnd;
		}
		return token.substr( valueStart, valueEnd - valueStart );
	}

	vector<string> splitAnnotationArgs( std::string_view args )
	{
		while ( args.empty() == false && ( args.back() == ')' || args.back() == ';' ) )
			args.remove_suffix( 1 );
		while ( args.empty() == false && ( args.front() == '(' || args.front() == ';' ) )
			args.remove_prefix( 1 );

		vector<string> tokens;
		bool		   bInQuote	  = false;
		size_t		   tokenStart = 0;

		for ( size_t i = 0; i < args.size(); ++i )
		{
			const utf8 c = args[i];
			if ( c == '"' )
			{
				bInQuote = ( bInQuote == false );
				continue;
			}
			if ( c == ',' && bInQuote == false )
			{
				if ( i > tokenStart )
				{
					std::string_view token = StringUtil::trim( args.substr( tokenStart, i - tokenStart ) );
					if ( token.empty() == false )
						tokens.emplace_back( token );
				}
				tokenStart = i + 1;
			}
		}
		if ( args.size() > tokenStart )
		{
			std::string_view token = StringUtil::trim( args.substr( tokenStart ) );
			if ( token.empty() == false )
				tokens.emplace_back( token );
		}
		return tokens;
	}

	/** @brief 빈 값·true·1·True 를 참으로 봅니다. */
	static bool parseAnnotationBool( const string& val )
	{
		return val.empty() || val == "true" || val == "1" || val == "True";
	}

	/** @brief 필드 이름에 맞는 테이블 항목을 찾습니다. */
	template <typename Entry, size_t N>
	const Entry* findFieldEntry( const Entry ( &table )[N], const std::string_view field )
	{
		for ( const Entry& entry : table )
		{
			if ( entry._field == field )
				return &entry;
		}
		return nullptr;
	}

	struct ReflectFlagEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedTypeInfo& );
	};

	static void applyReflectAbstract( ParsedTypeInfo& typeInfo ) { typeInfo._bAbstract = true; }
	static void applyReflectStatic( ParsedTypeInfo& typeInfo ) { typeInfo._bStatic = true; }

	constexpr ReflectFlagEntry kReflectFlags[] = {
		{"Abstract", applyReflectAbstract},
		{  "Static",	applyReflectStatic},
	};

	/** @brief 쉼표/세미콜론으로 나눈 타입 별칭을 붙입니다. */
	static void appendTypeAliases( vector<string>& outAliases, const string& raw )
	{
		const string_splitter parts( raw, { ",", ";" } );
		for ( const std::string_view token : parts.getSplitList() )
		{
			const string trimmed = StringUtil::trim( string( token ).c_str() );
			if ( trimmed.empty() == false )
				outAliases.push_back( trimmed );
		}
	}

	struct ReflectStringEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedTypeInfo&, const string& );
	};

	static void applyReflectAlias( ParsedTypeInfo& typeInfo, const string& value )
	{
		appendTypeAliases( typeInfo._listAliases, value );
	}

	constexpr ReflectStringEntry kReflectStrings[] = {
		{ "Alias", applyReflectAlias },
	};

	struct EnumStringEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedEnumInfo&, const string& );
	};

	/** @brief `Old:Current` 목록을 enumerator ValueAlias 로 넣습니다. */
	static void appendEnumValueAliases( ParsedEnumInfo& enumInfo, const string& raw )
	{
		const string_splitter parts( raw, { ",", ";" } );
		for ( const std::string_view tokenView : parts.getSplitList() )
		{
			string token = StringUtil::trim( string( tokenView ).c_str() );
			if ( token.empty() )
				continue;
			const size_t colon = token.find( ':' );
			if ( colon == string::npos || colon == 0 || colon + 1 >= token.size() )
			{
				SW_LOG_WARNING( "[AstVisitor] ENUM ValueAlias expected Old:Current, got '%#'", token );
				continue;
			}
			string alias	 = StringUtil::trim( token.substr( 0, colon ).c_str() );
			string canonical = StringUtil::trim( token.substr( colon + 1 ).c_str() );
			if ( alias.empty() == false && canonical.empty() == false )
				enumInfo._valueAliases.emplace_back( std::move( alias ), std::move( canonical ) );
		}
	}

	static void applyEnumAlias( ParsedEnumInfo& enumInfo, const string& value )
	{
		appendTypeAliases( enumInfo._listAliases, value );
	}
	static void applyEnumValueAlias( ParsedEnumInfo& enumInfo, const string& value )
	{
		appendEnumValueAliases( enumInfo, value );
	}
	static void applyEnumInvalid( ParsedEnumInfo& enumInfo, const string& value )
	{
		enumInfo._invalidEnumerator = value;
	}
	static void applyEnumCount( ParsedEnumInfo& enumInfo, const string& value )
	{
		enumInfo._countEnumerator = value;
	}

	constexpr EnumStringEntry kEnumStrings[] = {
		{	  "Alias",	   applyEnumAlias},
		{"ValueAlias", applyEnumValueAlias},
		{	  "Invalid",	 applyEnumInvalid},
		{	  "Count",	   applyEnumCount},
	};

	struct EnumFlagEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedEnumInfo& );
	};

	static void applyEnumFlags( ParsedEnumInfo& enumInfo )
	{
		enumInfo._bIsBitFlag   = 1;
		enumInfo._bEmitFlagOps = 1;
	}

	constexpr EnumFlagEntry kEnumFlags[] = {
		{ "Flags", applyEnumFlags },
	};

	struct PropBoolEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedPropertyInfo&, bool );
	};

	static void applyPropReadOnly( ParsedPropertyInfo& prop, bool value ) { prop._bReadOnly = value; }
	static void applyPropXmlAttribute( ParsedPropertyInfo& prop, bool value ) { prop._bXmlAttribute = value; }
	static void applyPropAssetPath( ParsedPropertyInfo& prop, bool value ) { prop._bAssetPath = value; }
	static void applyPropPolymorphic( ParsedPropertyInfo& prop, bool value ) { prop._bPolymorphic = value; }

	constexpr PropBoolEntry kPropBools[] = {
		{	  "ReadOnly",	  applyPropReadOnly},
		{"XmlAttribute", applyPropXmlAttribute},
		{	  "AssetPath",	   applyPropAssetPath},
		{ "Polymorphic",	 applyPropPolymorphic},
	};

	struct PropStringEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedPropertyInfo&, const string& );
	};

	static void applyPropAlias( ParsedPropertyInfo& prop, const string& value )
	{
		appendTypeAliases( prop._listAliases, value );
	}
	static void applyPropCategory( ParsedPropertyInfo& prop, const string& value ) { prop._category = value; }
	static void applyPropDisplayName( ParsedPropertyInfo& prop, const string& value ) { prop._displayName = value; }
	static void applyPropTooltip( ParsedPropertyInfo& prop, const string& value ) { prop._tooltip = value; }
	static void applyPropDefaultValue( ParsedPropertyInfo& prop, const string& value ) { prop._defaultValue = value; }
	static void applyPropAssetType( ParsedPropertyInfo& prop, const string& value )
	{
		prop._assetType	 = value;
		prop._bAssetPath = true;
	}

	constexpr PropStringEntry kPropStrings[] = {
		{		  "Alias",		   applyPropAlias},
		{	  "Category",	  applyPropCategory},
		{ "DisplayName",	 applyPropDisplayName},
		{	  "Tooltip",		 applyPropTooltip},
		{"DefaultValue", applyPropDefaultValue},
		{	  "AssetType",	   applyPropAssetType},
	};

	struct PropFloatEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedPropertyInfo&, float32 );
	};

	static void applyPropMinRange( ParsedPropertyInfo& prop, float32 value )
	{
		prop._minRange	= value;
		prop._bHasRange = true;
	}
	static void applyPropMaxRange( ParsedPropertyInfo& prop, float32 value )
	{
		prop._maxRange	= value;
		prop._bHasRange = true;
	}

	constexpr PropFloatEntry kPropFloats[] = {
		{"MinRange", applyPropMinRange},
		{"MaxRange", applyPropMaxRange},
	};

	struct FuncFlagEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedFunctionInfo& );
	};

	static void applyFuncReliable( ParsedFunctionInfo& method ) { method._bReliable = true; }
	static void applyFuncValidate( ParsedFunctionInfo& method ) { method._bValidate = true; }

	constexpr FuncFlagEntry kFuncFlags[] = {
		{"Reliable", applyFuncReliable},
		{"Validate", applyFuncValidate},
	};

	struct FuncStringEntry
	{
		std::string_view _field;
		void ( *apply )( ParsedFunctionInfo&, const string& );
	};

	static void applyFuncCategory( ParsedFunctionInfo& method, const string& value ) { method._category = value; }
	static void applyFuncDisplayName( ParsedFunctionInfo& method, const string& value ) { method._displayName = value; }
	static void applyFuncTooltip( ParsedFunctionInfo& method, const string& value ) { method._tooltip = value; }

	constexpr FuncStringEntry kFuncStrings[] = {
		{	  "Category",	  applyFuncCategory},
		{"DisplayName", applyFuncDisplayName},
		{	  "Tooltip",	 applyFuncTooltip},
	};

	/** @brief REFLECT(...) 토큰을 ParsedTypeInfo 플래그·별칭에 적용합니다. */
	void parseReflectAnnotation( const string& annotationSpelling, ParsedTypeInfo& typeInfo )
	{
		const utf8* prefix	  = annotationConstants::kReflectPrefix;
		size_t		prefixPos = annotationSpelling.find( prefix );
		if ( prefixPos == string::npos )
		{
			prefix	  = annotationConstants::kReflectScriptPrefix;
			prefixPos = annotationSpelling.find( prefix );
		}
		if ( prefixPos == string::npos )
			return;

		const AnnotationMeta& meta = AnnotationMeta::instance();
		for ( const string& token :
			  splitAnnotationArgs( annotationSpelling.substr( prefixPos + StringUtil::strlen( prefix ) ) ) )
		{
			if ( token.empty() )
				continue;

			const size_t eqPos = token.find( '=' );
			if ( eqPos == string::npos )
			{
				const AnnotationBinding* binding = meta.findBare( annotationConstants::kReflectScope, token );
				if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::Flag )
					continue;
				if ( const ReflectFlagEntry* entry = findFieldEntry( kReflectFlags, binding->_field ) )
					entry->apply( typeInfo );
				continue;
			}

			const string			 key	 = StringUtil::trim( token.substr( 0, eqPos ).c_str() );
			const string			 val	 = parseAnnotationStringValue( token, eqPos );
			const AnnotationBinding* binding = meta.findKey( annotationConstants::kReflectScope, key );
			if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::String )
				continue;
			if ( const ReflectStringEntry* entry = findFieldEntry( kReflectStrings, binding->_field ) )
				entry->apply( typeInfo, val );
		}
	}

	/** @brief ENUM(...) 토큰을 ParsedEnumInfo 에 적용합니다. */
	void parseEnumAnnotation( const string& annotationSpelling, ParsedEnumInfo& enumInfo )
	{
		const string args = annotationArgText( annotationSpelling, annotationConstants::kEnumPrefix );
		if ( annotationSpelling.find( annotationConstants::kEnumPrefix ) == string::npos )
			return;

		const AnnotationMeta& meta = AnnotationMeta::instance();
		for ( const string& token : splitAnnotationArgs( args ) )
		{
			if ( token.empty() )
				continue;
			const size_t eqPos = token.find( '=' );
			if ( eqPos == string::npos )
			{
				if ( const AnnotationBinding* binding = meta.findBare( annotationConstants::kEnumScope, token ) )
				{
					if ( binding->_kind == AnnotationBinding::Kind::Flag )
					{
						if ( const EnumFlagEntry* entry = findFieldEntry( kEnumFlags, binding->_field ) )
							entry->apply( enumInfo );
					}
				}
				continue;
			}

			const string			 key	 = StringUtil::trim( token.substr( 0, eqPos ).c_str() );
			const string			 val	 = parseAnnotationStringValue( token, eqPos );
			const AnnotationBinding* binding = meta.findKey( annotationConstants::kEnumScope, key );
			if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::String )
				continue;
			if ( const EnumStringEntry* entry = findFieldEntry( kEnumStrings, binding->_field ) )
				entry->apply( enumInfo, val );
		}
	}

	/** @brief PROPERTY 바인딩 Kind 에 따라 프로퍼티 필드를 채웁니다. */
	static void applyPropertyBinding( ParsedPropertyInfo& prop, const AnnotationBinding& binding, const string& val )
	{
		using Kind = AnnotationBinding::Kind;
		switch ( binding._kind )
		{
			case Kind::Flag:
			case Kind::Bool:
				if ( const PropBoolEntry* entry = findFieldEntry( kPropBools, binding._field ) )
					entry->apply( prop, binding._kind == Kind::Flag ? true : parseAnnotationBool( val ) );
				break;
			case Kind::String:
				if ( const PropStringEntry* entry = findFieldEntry( kPropStrings, binding._field ) )
					entry->apply( prop, val );
				break;
			case Kind::Float:
				if ( const PropFloatEntry* entry = findFieldEntry( kPropFloats, binding._field ) )
					entry->apply( prop, std::strtof( val.c_str(), nullptr ) );
				break;
			case Kind::NetRole:
				break;
		}
	}

	/** @brief PROPERTY(...) 토큰을 ParsedPropertyInfo 에 적용합니다. */
	void parsePropertyAnnotation( const string& annotationSpelling, ParsedPropertyInfo& prop )
	{
		const string args = annotationArgText( annotationSpelling, annotationConstants::kPropertyPrefix );
		if ( annotationSpelling.find( annotationConstants::kPropertyPrefix ) == string::npos )
			return;

		const AnnotationMeta& meta = AnnotationMeta::instance();
		for ( const string& token : splitAnnotationArgs( args ) )
		{
			if ( token.empty() )
				continue;

			const size_t eqPos = token.find( '=' );
			if ( eqPos == string::npos )
			{
				if ( const AnnotationBinding* binding = meta.findBare( annotationConstants::kPropertyScope, token ) )
					applyPropertyBinding( prop, *binding, {} );
				continue;
			}

			const string key = StringUtil::trim( token.substr( 0, eqPos ).c_str() );
			const string val = parseAnnotationStringValue( token, eqPos );
			if ( const AnnotationBinding* binding = meta.findKey( annotationConstants::kPropertyScope, key ) )
				applyPropertyBinding( prop, *binding, val );
		}
	}

	/** @brief FUNCTION(...) 토큰을 ParsedFunctionInfo 에 적용합니다. */
	void parseFunctionAnnotation( const string& annotationSpelling, ParsedFunctionInfo& method )
	{
		const string args = annotationArgText( annotationSpelling, annotationConstants::kFunctionPrefix );
		if ( annotationSpelling.find( annotationConstants::kFunctionPrefix ) == string::npos )
			return;

		const AnnotationMeta& meta = AnnotationMeta::instance();
		for ( const string& token : splitAnnotationArgs( args ) )
		{
			if ( token.empty() )
				continue;
			const size_t eqPos = token.find( '=' );
			if ( eqPos == string::npos )
			{
				if ( const AnnotationBinding* binding = meta.findBare( annotationConstants::kFunctionScope, token ) )
				{
					if ( binding->_kind == AnnotationBinding::Kind::NetRole )
					{
						FunctionNetRole role = FunctionNetRole::Local;
						if ( tryParseFunctionNetRole( binding->_field, role ) )
							method._netRole = role;
					}
					else if ( binding->_kind == AnnotationBinding::Kind::Flag )
					{
						if ( const FuncFlagEntry* entry = findFieldEntry( kFuncFlags, binding->_field ) )
							entry->apply( method );
					}
				}
				continue;
			}
			const string key = StringUtil::trim( token.substr( 0, eqPos ).c_str() );
			const string val = parseAnnotationStringValue( token, eqPos );
			if ( const AnnotationBinding* binding = meta.findKey( annotationConstants::kFunctionScope, key ) )
			{
				if ( binding->_kind == AnnotationBinding::Kind::String )
				{
					if ( const FuncStringEntry* entry = findFieldEntry( kFuncStrings, binding->_field ) )
						entry->apply( method, val );
				}
			}
		}
	}

} // namespace sw
