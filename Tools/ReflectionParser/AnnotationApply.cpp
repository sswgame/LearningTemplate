#include "pch.h"

#include "ReflectionParser/AnnotationApply.h"

#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserDefines.h"

SW_LOG_CALLER( "AnnotationApply" );
namespace sw
{
	namespace
	{
		struct AnnotationApplyInternal
		{
			/**
			 * @brief `key="value"` 또는 `key=value` 형태의 단일 토큰에서 따옴표를 고려하여 값 문자열을 추출합니다.
			 */
			static string_view parseAnnotationStringValue( string_view token, size_t eqPos )
			{
				size_t valueStart = eqPos + 1;
				while ( valueStart < token.size() && ( token[valueStart] == ' ' || token[valueStart] == '\t' ) )
					++valueStart;

				if ( valueStart >= token.size() )
					return {};

				if ( token[valueStart] == '"' )
				{
					const size_t endQuote = token.find( '"', valueStart + 1 );
					if ( endQuote == string_view::npos )
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

			/** @brief 빈 값·true·1·True 를 참으로 봅니다. */
			static bool parseAnnotationBool( string_view val )
			{
				return StringUtil::parseBool( val, true );
			}

			/** @brief 필드 이름에 맞는 테이블 항목을 찾습니다. */
			template <typename Entry, size_t N>
			static const Entry* findFieldEntry( const Entry ( &table )[N], const string_view field )
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
				string_view _field;
				void ( *_pApply )( ParsedTypeInfo& );
			};

			static void applyReflectAbstract( ParsedTypeInfo& typeInfo ) { typeInfo._bAbstract = SW_TRUE; }
			static void applyReflectStatic( ParsedTypeInfo& typeInfo ) { typeInfo._bStatic = SW_TRUE; }
			static void applyReflectHideInMenu( ParsedTypeInfo& typeInfo ) { typeInfo._bHideInMenu = SW_TRUE; }

			static constexpr ReflectFlagEntry kReflectFlags[] = {
				{  annotationFieldConstants::kAbstract,   applyReflectAbstract},
				{	  annotationFieldConstants::kStatic,	 applyReflectStatic},
				{annotationFieldConstants::kHideInMenu, applyReflectHideInMenu},
			};

			/** @brief 쉼표/세미콜론으로 나눈 타입 별칭을 붙입니다. */
			static void appendTypeAliases( vector<string>& outListAlias, const string& raw )
			{
				const string_splitter parts( raw, { ",", ";" } );
				for ( const string_view token : parts.getSplitList() )
				{
					const string trimmed = StringUtil::trim( string( token ).c_str() );
					if ( trimmed.empty() == false )
						outListAlias.push_back( trimmed );
				}
			}

			/** @brief `Key=Value, Key2=Value2` 목록을 커스텀 메타데이터 페어로 파싱합니다. */
			static void parseCustomMetaPairs( string_view raw, vector<pair<string, string>>& outList )
			{
				const string_splitter parts( raw, { ",", ";" } );
				for ( const string_view tokenView : parts.getSplitList() )
				{
					string token = StringUtil::trim( string( tokenView ).c_str() );
					if ( token.empty() )
						continue;
					const size_t eqPos = token.find( '=' );
					if ( eqPos != string::npos )
					{
						string key = StringUtil::trim( token.substr( 0, eqPos ).c_str() );
						string val = StringUtil::trim( token.substr( eqPos + 1 ).c_str() );
						if ( key.empty() == false )
							outList.emplace_back( std::move( key ), std::move( val ) );
					}
					else
					{
						outList.emplace_back( std::move( token ), "1" );
					}
				}
			}

			struct ReflectStringEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedTypeInfo&, const string& );
			};

			static void applyReflectAlias( ParsedTypeInfo& typeInfo, const string& value )
			{
				appendTypeAliases( typeInfo._listAlias, value );
			}
			static void applyReflectCategory( ParsedTypeInfo& typeInfo, const string& value ) { typeInfo._category = value; }
			static void applyReflectDisplayName( ParsedTypeInfo& typeInfo, const string& value ) { typeInfo._displayName = value; }
			static void applyReflectTooltip( ParsedTypeInfo& typeInfo, const string& value ) { typeInfo._tooltip = value; }
			static void applyReflectMeta( ParsedTypeInfo& typeInfo, const string& value )
			{
				parseCustomMetaPairs( value, typeInfo._listCustomMeta );
			}

			static constexpr ReflectStringEntry kReflectStrings[] = {
				{	  annotationFieldConstants::kAlias,		applyReflectAlias},
				{	  annotationFieldConstants::kCategory,	   applyReflectCategory},
				{annotationFieldConstants::kDisplayName, applyReflectDisplayName},
				{	  annotationFieldConstants::kTooltip,	  applyReflectTooltip},
				{		  annotationFieldConstants::kMeta,		   applyReflectMeta},
			};

			struct EnumStringEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedEnumInfo&, const string& );
			};

			/** @brief `Old:Current` 목록을 enumerator ValueAlias 로 넣습니다. */
			static void appendEnumValueAliases( ParsedEnumInfo& enumInfo, const string& raw )
			{
				const string_splitter parts( raw, { ",", ";" } );
				for ( const string_view tokenView : parts.getSplitList() )
				{
					string token = StringUtil::trim( string( tokenView ).c_str() );
					if ( token.empty() )
						continue;
					const size_t colon = token.find( ':' );
					if ( colon == string::npos || colon == 0 || colon + 1 >= token.size() )
					{
						SW_LOG_WARNING( "ENUM ValueAlias expected Old:Current, got '%#'", token );
						continue;
					}
					string alias	 = StringUtil::trim( token.substr( 0, colon ).c_str() );
					string canonical = StringUtil::trim( token.substr( colon + 1 ).c_str() );
					if ( alias.empty() == false && canonical.empty() == false )
						enumInfo._listValueAlias.emplace_back( std::move( alias ), std::move( canonical ) );
				}
			}

			static void applyEnumAlias( ParsedEnumInfo& enumInfo, const string& value )
			{
				appendTypeAliases( enumInfo._listAlias, value );
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
			static void applyEnumMeta( ParsedEnumInfo& enumInfo, const string& value )
			{
				parseCustomMetaPairs( value, enumInfo._listCustomMeta );
			}

			static constexpr EnumStringEntry kEnumStrings[] = {
				{	  annotationFieldConstants::kAlias,		applyEnumAlias},
				{annotationFieldConstants::kValueAlias, applyEnumValueAlias},
				{	  annotationFieldConstants::kInvalid,	  applyEnumInvalid},
				{	  annotationFieldConstants::kCount,		applyEnumCount},
				{	  annotationFieldConstants::kMeta,	   applyEnumMeta},
			};

			struct EnumFlagEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedEnumInfo& );
			};

			static void applyEnumFlags( ParsedEnumInfo& enumInfo )
			{
				enumInfo._bIsBitFlag   = 1;
				enumInfo._bEmitFlagOps = 1;
			}

			static constexpr EnumFlagEntry kEnumFlags[] = {
				{ annotationFieldConstants::kFlags, applyEnumFlags },
			};

			struct PropBoolEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedPropertyInfo&, bool );
			};

			static void applyPropReadOnly( ParsedPropertyInfo& prop, bool value ) { prop._bReadOnly = value; }
			static void applyPropXmlAttribute( ParsedPropertyInfo& prop, bool value ) { prop._bXmlAttribute = value; }
			static void applyPropAssetPath( ParsedPropertyInfo& prop, bool value ) { prop._bAssetPath = value; }
			static void applyPropPolymorphic( ParsedPropertyInfo& prop, bool value ) { prop._bPolymorphic = value; }
			static void applyPropTransient( ParsedPropertyInfo& prop, bool value ) { prop._bTransient = value; }
			static void applyPropHideInInspector( ParsedPropertyInfo& prop, bool value ) { prop._bHideInInspector = value; }

			static constexpr PropBoolEntry kPropBools[] = {
				{		  annotationFieldConstants::kReadOnly,		   applyPropReadOnly},
				{	  annotationFieldConstants::kXmlAttribute,	   applyPropXmlAttribute},
				{	  annotationFieldConstants::kAssetPath,		applyPropAssetPath},
				{	  annotationFieldConstants::kPolymorphic,	  applyPropPolymorphic},
				{	  annotationFieldConstants::kTransient,		applyPropTransient},
				{annotationFieldConstants::kHideInInspector, applyPropHideInInspector},
			};

			struct PropStringEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedPropertyInfo&, const string& );
			};

			static void applyPropAlias( ParsedPropertyInfo& prop, const string& value )
			{
				appendTypeAliases( prop._listAlias, value );
			}
			static void applyPropCategory( ParsedPropertyInfo& prop, const string& value ) { prop._category = value; }
			static void applyPropDisplayName( ParsedPropertyInfo& prop, const string& value ) { prop._displayName = value; }
			static void applyPropTooltip( ParsedPropertyInfo& prop, const string& value ) { prop._tooltip = value; }
			static void applyPropDefaultValue( ParsedPropertyInfo& prop, const string& value ) { prop._defaultValue = value; }
			static void applyPropAssetType( ParsedPropertyInfo& prop, const string& value )
			{
				prop._assetType	 = value;
				prop._bAssetPath = SW_TRUE;
			}
			static void applyPropMeta( ParsedPropertyInfo& prop, const string& value )
			{
				parseCustomMetaPairs( value, prop._listCustomMeta );
			}

			static constexpr PropStringEntry kPropStrings[] = {
				{		  annotationFieldConstants::kAlias,		applyPropAlias},
				{	  annotationFieldConstants::kCategory,	   applyPropCategory},
				{ annotationFieldConstants::kDisplayName,  applyPropDisplayName},
				{	  annotationFieldConstants::kTooltip,	  applyPropTooltip},
				{annotationFieldConstants::kDefaultValue, applyPropDefaultValue},
				{	  annotationFieldConstants::kAssetType,	applyPropAssetType},
				{		  annotationFieldConstants::kMeta,		   applyPropMeta},
			};

			struct PropFloatEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedPropertyInfo&, float32 );
			};

			static void applyPropMinRange( ParsedPropertyInfo& prop, float32 value )
			{
				prop._minRange	= value;
				prop._bHasRange = SW_TRUE;
			}
			static void applyPropMaxRange( ParsedPropertyInfo& prop, float32 value )
			{
				prop._maxRange	= value;
				prop._bHasRange = SW_TRUE;
			}

			static constexpr PropFloatEntry kPropFloats[] = {
				{annotationFieldConstants::kMinRange, applyPropMinRange},
				{annotationFieldConstants::kMaxRange, applyPropMaxRange},
			};

			struct FuncFlagEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedFunctionInfo& );
			};

			static void applyFuncReliable( ParsedFunctionInfo& method ) { method._bReliable = SW_TRUE; }
			static void applyFuncValidate( ParsedFunctionInfo& method ) { method._bValidate = SW_TRUE; }
			static void applyFuncCallInEditor( ParsedFunctionInfo& method ) { method._bCallInEditor = SW_TRUE; }

			static constexpr FuncFlagEntry kFuncFlags[] = {
				{	  annotationFieldConstants::kReliable,	   applyFuncReliable},
				{	  annotationFieldConstants::kValidate,	   applyFuncValidate},
				{annotationFieldConstants::kCallInEditor, applyFuncCallInEditor},
			};

			struct FuncStringEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedFunctionInfo&, const string& );
			};

			static void applyFuncCategory( ParsedFunctionInfo& method, const string& value ) { method._category = value; }
			static void applyFuncDisplayName( ParsedFunctionInfo& method, const string& value ) { method._displayName = value; }
			static void applyFuncTooltip( ParsedFunctionInfo& method, const string& value ) { method._tooltip = value; }
			static void applyFuncMeta( ParsedFunctionInfo& method, const string& value )
			{
				parseCustomMetaPairs( value, method._listCustomMeta );
			}

			static constexpr FuncStringEntry kFuncStrings[] = {
				{	  annotationFieldConstants::kCategory,	   applyFuncCategory},
				{annotationFieldConstants::kDisplayName, applyFuncDisplayName},
				{	  annotationFieldConstants::kTooltip,	  applyFuncTooltip},
				{		  annotationFieldConstants::kMeta,		   applyFuncMeta},
			};

			/** @brief PROPERTY 바인딩 Kind 에 따라 프로퍼티 필드를 채웁니다. */
			static void applyPropertyBinding( ParsedPropertyInfo& prop, const AnnotationBinding& binding, string_view val )
			{
				using Kind = AnnotationBinding::Kind;
				switch ( binding._kind )
				{
					case Kind::Flag:
					case Kind::Bool:
						if ( const PropBoolEntry* entry = findFieldEntry( kPropBools, binding._field ) )
							entry->_pApply( prop, binding._kind == Kind::Flag ? true : parseAnnotationBool( val ) );
						break;
					case Kind::String:
						if ( const PropStringEntry* entry = findFieldEntry( kPropStrings, binding._field ) )
							entry->_pApply( prop, string( val ) );
						break;
					case Kind::Float:
						if ( const PropFloatEntry* entry = findFieldEntry( kPropFloats, binding._field ) )
						{
							float32 fVal{ 0.0f };
							StringUtil::parseFloat( val, fVal );
							entry->_pApply( prop, fVal );
						}
						break;
					case Kind::NetRole:
						break;
					default:
						break;
				}
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	string_view AnnotationApply::annotationArgText( string_view spelling, string_view prefix )
	{
		const size_t pos = spelling.find( prefix );
		if ( pos == string_view::npos )
			return {};
		return spelling.substr( pos + prefix.size() );
	}

	vector<string> AnnotationApply::splitAnnotationArgs( string_view args )
	{
		while ( args.empty() == false && ( args.back() == ')' || args.back() == ';' ) )
			args.remove_suffix( 1 );
		while ( args.empty() == false && ( args.front() == '(' || args.front() == ';' ) )
			args.remove_prefix( 1 );

		vector<string> listToken;
		bool		   bInQuote	  = false;
		size_t		   tokenStart = 0;

		for ( size_t charIndex = 0; charIndex < args.size(); ++charIndex )
		{
			const utf8 c = args[charIndex];
			if ( c == '"' )
			{
				bInQuote = ( bInQuote == false );
				continue;
			}
			if ( c == ',' && bInQuote == false )
			{
				if ( charIndex > tokenStart )
				{
					string_view token = StringUtil::trim( args.substr( tokenStart, charIndex - tokenStart ) );
					if ( token.empty() == false )
						listToken.emplace_back( token );
				}
				tokenStart = charIndex + 1;
			}
		}
		if ( args.size() > tokenStart )
		{
			string_view token = StringUtil::trim( args.substr( tokenStart ) );
			if ( token.empty() == false )
				listToken.emplace_back( token );
		}
		return listToken;
	}

	/** @brief REFLECT(...) 토큰을 ParsedTypeInfo 플래그·별칭에 적용합니다. */
	void AnnotationApply::parseReflectAnnotation( string_view annotationSpelling, ParsedTypeInfo& typeInfo )
	{
		string_view prefix	  = annotationConstants::kReflectPrefix;
		size_t		prefixPos = annotationSpelling.find( prefix );
		if ( prefixPos == string_view::npos )
			return;

		const AnnotationMeta& meta = AnnotationMeta::instance();
		for ( const string& token :
			  splitAnnotationArgs( annotationSpelling.substr( prefixPos + prefix.size() ) ) )
		{
			if ( token.empty() )
				continue;

			const size_t eqPos = token.find( '=' );
			if ( eqPos == string::npos )
			{
				const AnnotationBinding* binding = meta.findBare( annotationConstants::kReflectScope, token );
				if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::Flag )
					continue;
				if ( const AnnotationApplyInternal::ReflectFlagEntry* entry =
						 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectFlags, binding->_field ) )
					entry->_pApply( typeInfo );
				continue;
			}

			const string_view		 key	 = StringUtil::trim( string_view( token.data(), eqPos ) );
			const string_view		 val	 = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
			const AnnotationBinding* binding = meta.findKey( annotationConstants::kReflectScope, key );
			if ( binding == nullptr )
				continue;
			if ( binding->_kind == AnnotationBinding::Kind::Bool )
			{
				if ( AnnotationApplyInternal::parseAnnotationBool( val ) )
				{
					if ( const AnnotationApplyInternal::ReflectFlagEntry* entry =
							 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectFlags, binding->_field ) )
						entry->_pApply( typeInfo );
				}
			}
			else if ( binding->_kind == AnnotationBinding::Kind::String )
			{
				if ( const AnnotationApplyInternal::ReflectStringEntry* entry =
						 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectStrings, binding->_field ) )
					entry->_pApply( typeInfo, string( val ) );
			}
		}
	}

	/** @brief ENUM(...) 토큰을 ParsedEnumInfo 에 적용합니다. */
	void AnnotationApply::parseEnumAnnotation( string_view annotationSpelling, ParsedEnumInfo& enumInfo )
	{
		const string_view args = AnnotationApply::annotationArgText( annotationSpelling, annotationConstants::kEnumPrefix );
		if ( annotationSpelling.find( annotationConstants::kEnumPrefix ) == string_view::npos )
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
						if ( const AnnotationApplyInternal::EnumFlagEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kEnumFlags, binding->_field ) )
							entry->_pApply( enumInfo );
					}
				}
				continue;
			}

			const string_view		 key	 = StringUtil::trim( string_view( token.data(), eqPos ) );
			const string_view		 val	 = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
			const AnnotationBinding* binding = meta.findKey( annotationConstants::kEnumScope, key );
			if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::String )
				continue;
			if ( const AnnotationApplyInternal::EnumStringEntry* entry =
					 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kEnumStrings, binding->_field ) )
				entry->_pApply( enumInfo, string( val ) );
		}
	}

	/** @brief PROPERTY(...) 토큰을 ParsedPropertyInfo 에 적용합니다. */
	void AnnotationApply::parsePropertyAnnotation( string_view annotationSpelling, ParsedPropertyInfo& prop )
	{
		const string_view args = AnnotationApply::annotationArgText( annotationSpelling, annotationConstants::kPropertyPrefix );
		if ( annotationSpelling.find( annotationConstants::kPropertyPrefix ) == string_view::npos )
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
					AnnotationApplyInternal::applyPropertyBinding( prop, *binding, {} );
				continue;
			}

			const string_view key = StringUtil::trim( string_view( token.data(), eqPos ) );
			const string_view val = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
			if ( const AnnotationBinding* binding = meta.findKey( annotationConstants::kPropertyScope, key ) )
				AnnotationApplyInternal::applyPropertyBinding( prop, *binding, val );
		}
	}

	/** @brief FUNCTION(...) 토큰을 ParsedFunctionInfo 에 적용합니다. */
	void AnnotationApply::parseFunctionAnnotation( string_view annotationSpelling, ParsedFunctionInfo& method )
	{
		const string_view args = AnnotationApply::annotationArgText( annotationSpelling, annotationConstants::kFunctionPrefix );
		if ( annotationSpelling.find( annotationConstants::kFunctionPrefix ) == string_view::npos )
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
						if ( const AnnotationApplyInternal::FuncFlagEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncFlags, binding->_field ) )
							entry->_pApply( method );
					}
				}
				continue;
			}
			const string_view key = StringUtil::trim( string_view( token.data(), eqPos ) );
			const string_view val = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
			if ( const AnnotationBinding* binding = meta.findKey( annotationConstants::kFunctionScope, key ) )
			{
				if ( binding->_kind == AnnotationBinding::Kind::Bool )
				{
					if ( AnnotationApplyInternal::parseAnnotationBool( val ) )
					{
						if ( const AnnotationApplyInternal::FuncFlagEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncFlags, binding->_field ) )
							entry->_pApply( method );
					}
				}
				else if ( binding->_kind == AnnotationBinding::Kind::String )
				{
					if ( const AnnotationApplyInternal::FuncStringEntry* entry =
							 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncStrings, binding->_field ) )
						entry->_pApply( method, string( val ) );
				}
			}
		}
	}

} // namespace sw
