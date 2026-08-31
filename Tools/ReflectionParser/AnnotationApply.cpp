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

			static void applyReflectAlias( ParsedTypeInfo& typeInfo, const string& value )
			{
				appendTypeAliases( typeInfo._listAlias, value );
			}

			static void applyReflectMeta( ParsedTypeInfo& typeInfo, const string& value )
			{
				parseCustomMetaPairs( value, typeInfo._listCustomMeta );
			}

			static void applyEnumAlias( ParsedEnumInfo& enumInfo, const string& value )
			{
				appendTypeAliases( enumInfo._listAlias, value );
			}

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

			static void applyEnumValueAlias( ParsedEnumInfo& enumInfo, const string& value )
			{
				appendEnumValueAliases( enumInfo, value );
			}

			static void applyEnumMeta( ParsedEnumInfo& enumInfo, const string& value )
			{
				parseCustomMetaPairs( value, enumInfo._listCustomMeta );
			}

			static void applyEnumFlags( ParsedEnumInfo& enumInfo )
			{
				enumInfo._bIsBitFlag   = 1;
				enumInfo._bEmitFlagOps = 1;
			}

			static void applyPropAlias( ParsedPropertyInfo& prop, const string& value )
			{
				appendTypeAliases( prop._listAlias, value );
			}

			static void applyPropAssetType( ParsedPropertyInfo& prop, const string& value )
			{
				prop._assetType	 = value;
				prop._bAssetPath = SW_TRUE;
			}

			static void applyPropMeta( ParsedPropertyInfo& prop, const string& value )
			{
				parseCustomMetaPairs( value, prop._listCustomMeta );
			}

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

			static void applyFuncMeta( ParsedFunctionInfo& method, const string& value )
			{
				parseCustomMetaPairs( value, method._listCustomMeta );
			}

// ------------------------------------------------------------------------------
// 애노테이션 필드 테이블 — PredefinedAnnotationField.xxx 한 곳에서 전개합니다.
// 플래그 멤버가 uint8 : 1 비트필드라 멤버 포인터 바인딩이 불가능해, 대입 람다를 씁니다.
// 각 Scope별로 1회의 #include 전개로 모든 Kind 항목을 통합 테이블에 등록합니다.
// ------------------------------------------------------------------------------
#define REGISTER_ANNOTATION_FIELD( Scope, Kind, Id, Member ) SW_ANN_##Scope( Kind, Id, Member )

			// ── REFLECT ──────────────────────────────────────────────────
			struct ReflectEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedTypeInfo&, const AnnotationBinding&, string_view );
			};

#define SW_ANN_Enum( ... )
#define SW_ANN_Property( ... )
#define SW_ANN_Function( ... )
#define SW_ANN_Reflect( Kind, Id, Member ) SW_ANN_Reflect_##Kind( Id, Member )

#define SW_ANN_Reflect_Flag( Id, Member )	  { #Id, []( ParsedTypeInfo& target, const AnnotationBinding&, string_view ) { target.Member = SW_TRUE; } },
#define SW_ANN_Reflect_String( Id, Member )	  { #Id, []( ParsedTypeInfo& target, const AnnotationBinding&, string_view val ) { target.Member = string( val ); } },
#define SW_ANN_Reflect_StringFn( Id, Member ) { #Id, []( ParsedTypeInfo& target, const AnnotationBinding&, string_view val ) { Member( target, string( val ) ); } },

			static constexpr ReflectEntry kReflectEntries[] = {
#include "PredefinedAnnotationField.xxx"
			};

#undef SW_ANN_Reflect_Flag
#undef SW_ANN_Reflect_String
#undef SW_ANN_Reflect_StringFn
#undef SW_ANN_Reflect

			// ── ENUM ─────────────────────────────────────────────────────
			struct EnumEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedEnumInfo&, string_view );
			};

#undef SW_ANN_Enum
#define SW_ANN_Reflect( ... )
#define SW_ANN_Enum( Kind, Id, Member ) SW_ANN_Enum_##Kind( Id, Member )

#define SW_ANN_Enum_FlagFn( Id, Member )   { #Id, []( ParsedEnumInfo& target, string_view ) { Member( target ); } },
#define SW_ANN_Enum_String( Id, Member )   { #Id, []( ParsedEnumInfo& target, string_view val ) { target.Member = string( val ); } },
#define SW_ANN_Enum_StringFn( Id, Member ) { #Id, []( ParsedEnumInfo& target, string_view val ) { Member( target, string( val ) ); } },

			static constexpr EnumEntry kEnumEntries[] = {
#include "PredefinedAnnotationField.xxx"
			};

#undef SW_ANN_Enum_FlagFn
#undef SW_ANN_Enum_String
#undef SW_ANN_Enum_StringFn
#undef SW_ANN_Enum

			// ── PROPERTY ─────────────────────────────────────────────────
			struct PropEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedPropertyInfo&, const AnnotationBinding&, string_view );
			};

#undef SW_ANN_Property
#define SW_ANN_Enum( ... )
#define SW_ANN_Property( Kind, Id, Member ) SW_ANN_Property_##Kind( Id, Member )

#define SW_ANN_Property_Bool( Id, Member )	   { #Id, []( ParsedPropertyInfo& target, const AnnotationBinding& b, string_view val ) { target.Member = ( b._kind == AnnotationBinding::Kind::Flag ? true : parseAnnotationBool( val ) ); } },
#define SW_ANN_Property_String( Id, Member )   { #Id, []( ParsedPropertyInfo& target, const AnnotationBinding&, string_view val ) { target.Member = string( val ); } },
#define SW_ANN_Property_StringFn( Id, Member ) { #Id, []( ParsedPropertyInfo& target, const AnnotationBinding&, string_view val ) { Member( target, string( val ) ); } },
#define SW_ANN_Property_FloatFn( Id, Member ) \
	{ #Id, []( ParsedPropertyInfo& target, const AnnotationBinding&, string_view val ) {   \
		 float32 fVal{ 0.0f };                                                             \
		 StringUtil::parseFloat( val, fVal );                                              \
		 Member( target, fVal ); } },

			static constexpr PropEntry kPropEntries[] = {
#include "PredefinedAnnotationField.xxx"
			};

#undef SW_ANN_Property_Bool
#undef SW_ANN_Property_String
#undef SW_ANN_Property_StringFn
#undef SW_ANN_Property_FloatFn
#undef SW_ANN_Property

			// ── FUNCTION ─────────────────────────────────────────────────
			struct FuncEntry
			{
				string_view _field;
				void ( *_pApply )( ParsedFunctionInfo&, string_view );
			};

#undef SW_ANN_Function
#define SW_ANN_Property( ... )
#define SW_ANN_Function( Kind, Id, Member ) SW_ANN_Function_##Kind( Id, Member )

#define SW_ANN_Function_Flag( Id, Member )	   { #Id, []( ParsedFunctionInfo& target, string_view ) { target.Member = SW_TRUE; } },
#define SW_ANN_Function_String( Id, Member )   { #Id, []( ParsedFunctionInfo& target, string_view val ) { target.Member = string( val ); } },
#define SW_ANN_Function_StringFn( Id, Member ) { #Id, []( ParsedFunctionInfo& target, string_view val ) { Member( target, string( val ) ); } },

			static constexpr FuncEntry kFuncEntries[] = {
#include "PredefinedAnnotationField.xxx"
			};

#undef SW_ANN_Function_Flag
#undef SW_ANN_Function_String
#undef SW_ANN_Function_StringFn
#undef SW_ANN_Function

#undef SW_ANN_Reflect
#undef SW_ANN_Enum
#undef SW_ANN_Property
#undef REGISTER_ANNOTATION_FIELD

			/** @brief PROPERTY 바인딩에 따라 프로퍼티 필드를 채웁니다. */
			static void applyPropertyBinding( ParsedPropertyInfo& prop, const AnnotationBinding& binding, string_view val )
			{
				if ( const PropEntry* entry = findFieldEntry( kPropEntries, binding._field ) )
					entry->_pApply( prop, binding, val );
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
				if ( const AnnotationApplyInternal::ReflectEntry* entry =
						 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectEntries, binding->_field ) )
					entry->_pApply( typeInfo, *binding, {} );
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
					if ( const AnnotationApplyInternal::ReflectEntry* entry =
							 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectEntries, binding->_field ) )
						entry->_pApply( typeInfo, *binding, val );
				}
			}
			else if ( binding->_kind == AnnotationBinding::Kind::String )
			{
				if ( const AnnotationApplyInternal::ReflectEntry* entry =
						 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kReflectEntries, binding->_field ) )
					entry->_pApply( typeInfo, *binding, val );
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
						if ( const AnnotationApplyInternal::EnumEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kEnumEntries, binding->_field ) )
							entry->_pApply( enumInfo, {} );
					}
				}
				continue;
			}

			const string_view		 key	 = StringUtil::trim( string_view( token.data(), eqPos ) );
			const string_view		 val	 = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
			const AnnotationBinding* binding = meta.findKey( annotationConstants::kEnumScope, key );
			if ( binding == nullptr || binding->_kind != AnnotationBinding::Kind::String )
				continue;
			if ( const AnnotationApplyInternal::EnumEntry* entry =
					 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kEnumEntries, binding->_field ) )
				entry->_pApply( enumInfo, val );
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
						if ( const AnnotationApplyInternal::FuncEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncEntries, binding->_field ) )
							entry->_pApply( method, {} );
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
						if ( const AnnotationApplyInternal::FuncEntry* entry =
								 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncEntries, binding->_field ) )
							entry->_pApply( method, val );
					}
				}
				else if ( binding->_kind == AnnotationBinding::Kind::String )
				{
					if ( const AnnotationApplyInternal::FuncEntry* entry =
							 AnnotationApplyInternal::findFieldEntry( AnnotationApplyInternal::kFuncEntries, binding->_field ) )
						entry->_pApply( method, val );
				}
			}
		}
	}
} // namespace sw
