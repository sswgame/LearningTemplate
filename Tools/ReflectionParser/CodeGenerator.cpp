
#include "CodeGenerator.h"
#include "EmitTemplateStore.h"
#include "ParserContext.h"
#include "ParserDefines.h"
#include "ParserUtil.h"
#include "TypeNameMap.h"

#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

namespace sw
{
	namespace
	{
		constexpr int32 kMaxNestedContainerDepth = 3;

		/**
		 * @brief 열거형 전체 FQN(예: "sw::EState::Idle")에서 말단 열거자 이름("Idle")을 추출합니다.
		 */
		static string enumeratorLeaf( const string& spec )
		{
			string		 name = StringUtil::trim( spec.c_str() );
			const size_t last = name.rfind( "::" );
			if ( last != string::npos && last + 2 < name.size() )
				name = name.substr( last + 2 );
			return name;
		}

		/**
		 * @brief 컨테이너 필드에 대한 래퍼 타입 문자열(예: `sw::VectorWrapper<decltype(std::declval<MyClass>().myList)>`)을 생성합니다.
		 */
		static string makeWrapperType( const string& containerType, const string& fqn, const string& fieldName )
		{
			StringBuilder<constant::kMaxBuffer1024> b;
			b.appendFormat( "sw::%#Wrapper<decltype(std::declval<%#>().%#)>", containerType, fqn, fieldName );
			return string( b.view() );
		}

		/**
		 * @brief 2중, 3중 중첩 컨테이너(예: vector<vector<int>>)에 대한 중첩 래퍼 표기를 생성합니다.
		 */
		static string makeNestedWrapperType( const string& containerType, int32 depth )
		{
			StringBuilder<constant::kMaxBuffer128> b;
			b.appendFormat( "sw::%#Wrapper<NestC%#>", containerType, depth );
			return string( b.view() );
		}

		/**
		 * @brief 생성자 검색을 위한 고유 식별자 문자열(예: `$ctor(int32,float32)`)을 구성합니다.
		 */
		static string makeCtorLookupName( const ParsedFunctionInfo& method )
		{
			StringBuilder<constant::kMaxBuffer1024> b;
			b.append( annotationConstants::kCtorLookupName );
			if ( method._paramTypeNames.empty() == false )
			{
				b.append( '(' );
				for ( size_t paramIndex = 0; paramIndex < method._paramTypeNames.size(); ++paramIndex )
				{
					if ( paramIndex > 0 )
						b.append( ',' );
					b.append( normalizeTypeName( method._paramTypeNames[paramIndex] ) );
				}
				b.append( ')' );
			}
			return string( b.view() );
		}

		/**
		 * @brief 타입 이름 목록을 C++ 배열 초기화 구문 `{ "int32", "string" }` 형태로 포맷팅합니다.
		 */
		static string makeQuotedTypeList( const vector<string>& types )
		{
			StringBuilder<constant::kMaxBuffer1024> b;
			b.append( "{ " );
			for ( size_t typeIndex = 0; typeIndex < types.size(); ++typeIndex )
			{
				if ( typeIndex > 0 )
					b.append( ", " );
				b.appendFormat( "\"%#\"", normalizeTypeName( types[typeIndex] ) );
			}
			b.append( " }" );
			return string( b.view() );
		}

		/**
		 * @brief 런타임 동적 함수 호출(Invoker)을 위한 인자 추출 구문 `args.get<T>(0), args.get<T>(1)...`을 생성합니다.
		 */
		static string makeInvokerCallArgs( const vector<string>& types )
		{
			StringBuilder<constant::kMaxBuffer1024> b;
			for ( size_t typeIndex = 0; typeIndex < types.size(); ++typeIndex )
			{
				if ( typeIndex > 0 )
					b.append( ", " );
				b.appendFormat( "args.get<%#>( %# )", normalizeTypeName( types[typeIndex] ), static_cast<uint32>( typeIndex ) );
			}
			return string( b.view() );
		}

	} // namespace

	CodeGenerator::CodeGenerator(
		const vector<ParsedTypeInfo>& types,
		const vector<ParsedEnumInfo>& enums,
		const string&				  sourceFilePath,
		const string&				  outputDir )
		: _types{ types }
		, _enums{ enums }
		, _sourceFilePath{ sourceFilePath }
		, _outputDir{ outputDir }
		, _outputFilePath{}
		, _outputHeaderPath{}
	{
	}

	void CodeGenerator::appendTemplate( CodeEmitBuffer& out, const std::string_view name,
										const unordered_map<string, string>& vars )
	{
		out.append( EmitTemplateStore::instance().render( name, vars ) );
	}

	const utf8* CodeGenerator::containerKindExpr( const ContainerKind kind )
	{
		return toCppExpr( kind );
	}

	const utf8* CodeGenerator::peelMember( const ContainerKind kind )
	{
		return containerPeelMember( kind );
	}

	bool CodeGenerator::generate()
	{
		if ( EmitTemplateStore::instance().isLoaded() == false )
		{
			SW_LOG_ERROR( "[CodeGenerator] Emit templates not loaded (pass --emit-templates <dir>)." );
			return false;
		}

		BLOCK( "Prepare Output Path" )
		{
			FileUtil::createDirectory( _outputDir );
			_outputFilePath	  = makeGeneratedPath( _outputDir, _sourceFilePath, ParserContext::getSharedConfig().emitCppExtension );
			_outputHeaderPath = makeGeneratedPath( _outputDir, _sourceFilePath, ParserContext::getSharedConfig().emitHeaderExtension );
		}

		CodeEmitBuffer buffer;

		if ( _types.empty() && _enums.empty() )
		{
			buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
		}

		if ( _types.empty() == false || _enums.empty() == false )
		{
			BLOCK( "Emit File Header" )
			{
				emitFileHeader( buffer );
				buffer.append( ParserContext::getSharedConfig().emitGeneratedNsOpen );
			}

			BLOCK( "Emit Registrars" )
			{
				for ( const ParsedTypeInfo& typeInfo : _types )
					emitTypeRegistrar( buffer, typeInfo );
				for ( const ParsedEnumInfo& enumInfo : _enums )
					emitEnumRegistrar( buffer, enumInfo );
			}

			buffer.append( ParserContext::getSharedConfig().emitGeneratedNsClose );

			BLOCK( "Emit Component Factory Registrars" )
			{
				for ( const ParsedTypeInfo& typeInfo : _types )
				{
					if ( typeInfo.wantsComponentFactory() )
						emitComponentFactoryRegistrar( buffer, typeInfo );
				}
			}

			BLOCK( "Emit Script System Registrars" )
			{
				for ( const ParsedTypeInfo& typeInfo : _types )
				{
					if ( typeInfo.wantsScriptSystem() )
						emitScriptSystemRegistrar( buffer, typeInfo );
				}
			}

			BLOCK( "Emit Type Traits & Accessors" )
			{
				for ( const ParsedTypeInfo& typeInfo : _types )
				{
					emitReflectTypeTraits( buffer, typeInfo );
					if ( typeInfo.wantsTypeApi() )
						emitTypeInfoAccessors( buffer, typeInfo );
				}
			}
		}

		const string newContent( buffer.view() );

		BLOCK( "Incremental Write" )
		{
			bool bCppUnchanged = false;
			if ( FileUtil::fileExists( _outputFilePath ) )
			{
				string existingContent;
				FileUtil::readTextFile( _outputFilePath, existingContent );
				if ( existingContent.empty() == false && existingContent == newContent )
				{
					SW_LOG_INFO( "[CodeGenerator] Incremental check: %# is up-to-date, skipping write.", _outputFilePath );
					bCppUnchanged = true;
				}
			}

			if ( bCppUnchanged == false )
			{
				if ( FileUtil::writeTextFile( _outputFilePath, newContent ) == false )
				{
					SW_LOG_ERROR( "[CodeGenerator] Failed to open output: %#", _outputFilePath );
					return false;
				}
			}
		}

		if ( emitGeneratedHeader() == false )
			return false;

		SW_LOG_INFO( "[CodeGenerator] Generated: %#", _outputFilePath );
		return true;
	}

	void CodeGenerator::emitFileHeader( CodeEmitBuffer& out ) const
	{
		appendTemplate( out, tplConstants::kFileHeader, {
															{ "SourcePath", _sourceFilePath }
		  } );
	}

	void CodeGenerator::emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		appendTemplate( out, tplConstants::kReflectTypeTraits, {
																   { "FQN", typeInfo._fullyQualifiedName }
		   } );
	}

	void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		appendTemplate( out, tplConstants::kTypeInfoAccessors, {
																   { "FQN", typeInfo._fullyQualifiedName }
		   } );
	}

	void CodeGenerator::emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		appendTemplate( out, tplConstants::kComponentFactoryRegistrar, {
																		   {		 "Id", sanitizeIdentifier( typeInfo._fullyQualifiedName )},
																		   {		 "FQN",						typeInfo._fullyQualifiedName},
																		   {		 "Name",									 typeInfo._name},
																		   {"ModuleName",									   getModuleName()},
		   } );
	}

	void CodeGenerator::emitScriptSystemRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		appendTemplate( out, tplConstants::kScriptSystemRegistrar, {
																	   {"FQN",						typeInfo._fullyQualifiedName},
																	   { "Id", sanitizeIdentifier( typeInfo._fullyQualifiedName )}
		} );
	}

	void CodeGenerator::emitPropertyMetadata( CodeEmit& e, const ParsedPropertyInfo& prop ) const
	{
		e.assignQuotedIf( prop._category.empty() == false, "p._metadata._category", prop._category );
		e.assignQuotedIf( prop._displayName.empty() == false, "p._metadata._displayName", prop._displayName );
		e.assignQuotedIf( prop._tooltip.empty() == false, "p._metadata._tooltip", prop._tooltip );
		e.assignQuotedIf( prop._defaultValue.empty() == false, "p._metadata._defaultValue", prop._defaultValue );
		e.assignQuotedIf( prop._assetType.empty() == false, "p._metadata._assetType", prop._assetType );
		e.flagIf( prop._bReadOnly, "p._metadata._bReadOnly", "true" );
		e.flagIf( prop._bXmlAttribute, "p._metadata._bXmlAttribute", "true" );
		e.flagIf( prop._bAssetPath, "p._metadata._bAssetPath", "true" );
		e.flagIf( prop._bPolymorphic, "p._metadata._bPolymorphic", "true" );
		if ( prop._bHasRange )
		{
			e.linef( "p._metadata._minRange     = %#f;", prop._minRange );
			e.linef( "p._metadata._maxRange     = %#f;", prop._maxRange );
			e.assign( "p._metadata._bHasRange", "true" );
		}
	}

	void CodeGenerator::emitNestedContainerTree( CodeEmit& e, const ParsedTypeInfo& typeInfo,
												 const ParsedPropertyInfo& prop ) const
	{
		if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == false )
			return;

		const utf8*	 outerKind	  = containerKindExpr( prop._containerKind );
		const string outerWrapper = makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

		e.line( "{" );
		e.push();
		e.line( "auto nested0 = sw::make_shared<sw::NestedContainerInfo>();" );
		e.assign( "nested0->_kind", outerKind );
		e.linef( "nested0->_elementTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
		e.linef( "nested0->_keyTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
		e.linef( "nested0->_wrapper = sw::make_shared<%#>();", outerWrapper );

		const ParsedContainerNode* node		= prop._containerTree->_elementNested.get();
		ContainerKind			   prevKind = prop._containerKind;
		int32					   depth	= 1;

		if ( node != nullptr && node->_bIsContainer )
		{
			e.linef( "using NestC0 = decltype( std::declval<%#>().%# );", typeInfo._fullyQualifiedName, prop._name );
			while ( node != nullptr && node->_bIsContainer && depth < kMaxNestedContainerDepth )
			{
				const utf8* kind = containerKindExpr( node->_containerKind );
				const utf8* peel = peelMember( prevKind );
				e.linef( "using NestC%# = typename NestC%#::%#;", depth, depth - 1, peel );

				const string wrapper = makeNestedWrapperType( node->_containerType, depth );

				e.linef( "auto nested%# = sw::make_shared<sw::NestedContainerInfo>();", depth );
				e.linef( "nested%#->_kind = %#;", depth, kind );
				e.linef( "nested%#->_elementTypeName = %#;", depth,
						 CodeEmit::hs( normalizeTypeName( node->_elementTypeName ) ) );
				e.linef( "nested%#->_keyTypeName = %#;", depth, CodeEmit::hs( normalizeTypeName( node->_keyTypeName ) ) );
				e.linef( "nested%#->_wrapper = sw::make_shared<%#>();", depth, wrapper );
				e.linef( "nested%#->_elementNested = nested%#;", depth - 1, depth );

				prevKind = node->_containerKind;
				node	 = ( node->_elementNested != nullptr ) ? node->_elementNested.get() : nullptr;
				++depth;
			}
		}

		e.assign( "p._nestedContainer", "nested0" );
		e.pop();
		e.line( "}" );
	}

	void CodeGenerator::emitPropertyInfoEntry( CodeEmit& e, const ParsedTypeInfo& typeInfo,
											   const ParsedPropertyInfo& prop ) const
	{
		e.line( "[]() {" );
		e.push();
		// 강제로 Component/GameObject 파생 클래스가 값(또는 원시 포인터)으로 들어가는 것을 막는 static_assert
		e.linef( "constexpr bool kIsInvalidPtr = std::is_base_of_v<sw::Component, std::remove_pointer_t<decltype(%#::%#)>> || std::is_base_of_v<sw::GameObject, std::remove_pointer_t<decltype(%#::%#)>>;", typeInfo._fullyQualifiedName, prop._name, typeInfo._fullyQualifiedName, prop._name );
		e.line( "static_assert(!kIsInvalidPtr, \"GameObject or Component cannot be stored by value or raw pointer inside a PROPERTY(). Use GameObjectPtr or ComponentPtr instead.\");" );
		e.line( "sw::PropertyInfo p(" );
		e.push();
		e.linef( "%#,", CodeEmit::hs( prop._name ) );
		e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._typeName ) ) );
		e.linef( "offsetof( %#, %# ),", typeInfo._fullyQualifiedName, prop._name );

		if ( prop._bIsContainer )
		{
			const utf8*	 kindStr	 = containerKindExpr( prop._containerKind );
			const string wrapperType = makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

			e.line( "true," );
			e.linef( "%#,", kindStr );
			e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
			e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
			e.linef( "sw::make_shared<%#>() );", wrapperType );

			emitNestedContainerTree( e, typeInfo, prop );
		}
		else
		{
			e.line( "false, sw::ContainerKind::None," );
			e.line( "::sw::hashed_string(), ::sw::hashed_string(), nullptr );" );
		}

		e.pop(); // 생성자 인자 들여쓰기
		if ( prop._listAliases.empty() == false )
		{
			e.line( "p._listAliases = {" );
			e.push();
			for ( const string& alias : prop._listAliases )
				e.linef( "%#,", CodeEmit::hs( alias ) );
			e.pop();
			e.line( "};" );
		}
		emitPropertyMetadata( e, prop );
		e.line( "return p;" );
		e.pop();
		e.line( "}()," );
	}

	void CodeGenerator::emitMethodInvoker( CodeEmit& e, const ParsedTypeInfo& typeInfo,
										   const ParsedFunctionInfo& method, const string& retType,
										   const string& callArgs ) const
	{
		e.line( "auto invokerCb = []( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
		e.line( "{" );
		e.push();
		if ( method._paramTypeNames.empty() )
			e.line( "(void)args;" );

		if ( method._bStatic && method._bConstructor == false )
		{
			e.line( "(void)objPtr;" );
			if ( retType == annotationConstants::kVoidTypeName )
			{
				e.linef( "%#::%#(%#);", typeInfo._fullyQualifiedName, method._name, callArgs );
				e.line( "return ::sw::TaskValue{};" );
			}
			else
			{
				e.linef( "return ::sw::TaskValue{ %#::%#(%#) };", typeInfo._fullyQualifiedName, method._name, callArgs );
			}
		}
		else
		{
			e.linef( "auto* self = static_cast<%#*>( objPtr );", typeInfo._fullyQualifiedName );
			if ( method._bConstructor )
			{
				e.linef( "new ( self ) %#(%#);", typeInfo._fullyQualifiedName, callArgs );
				e.line( "return ::sw::TaskValue{};" );
			}
			else if ( retType == annotationConstants::kVoidTypeName )
			{
				e.linef( "self->%#(%#);", method._name, callArgs );
				e.line( "return ::sw::TaskValue{};" );
			}
			else
			{
				e.linef( "return ::sw::TaskValue{ self->%#(%#) };", method._name, callArgs );
			}
		}
		e.pop();
		e.line( "};" );
		e.line( "funcInfo._invoker = SW_DELEGATE_LAMBDA( ::sw::Delegate<::sw::TaskValue( void*, const ::sw::TaskArgs& )>, invokerCb );" );
		e.line( "info._listMethods.push_back( funcInfo );" );
	}

	void CodeGenerator::emitMethodList( CodeEmit& e, const ParsedTypeInfo& typeInfo ) const
	{
		for ( const ParsedFunctionInfo& method : typeInfo._listMethods )
		{
			const string retType = normalizeTypeName( method._returnTypeName );

			const string lookupName = method._bConstructor ? makeCtorLookupName( method ) : method._name;

			e.line( "{" );
			e.push();
			e.line( "::sw::FunctionInfo funcInfo;" );
			e.assign( "funcInfo._name", CodeEmit::quoted( method._bConstructor ? annotationConstants::kCtorLookupName : method._name ) );
			e.linef( "funcInfo._hashName       = %#;", CodeEmit::hs( lookupName ) );
			e.assign( "funcInfo._returnTypeName", CodeEmit::quoted( retType ) );
			e.assign( "funcInfo._listParamTypeNames", makeQuotedTypeList( method._paramTypeNames ) );

			e.assignQuotedIf( method._category.empty() == false, "funcInfo._metadata._category", method._category );
			e.assignQuotedIf( method._displayName.empty() == false, "funcInfo._metadata._displayName", method._displayName );
			e.assignQuotedIf( method._tooltip.empty() == false, "funcInfo._metadata._tooltip", method._tooltip );

			if ( method._netRole != FunctionNetRole::Local )
				e.assign( "funcInfo._metadata._netRole", toCppExpr( method._netRole ) );

			e.flagIf( method._bReliable, "funcInfo._metadata._bReliable" );
			e.flagIf( method._bValidate, "funcInfo._metadata._bValidate" );
			e.flagIf( method._bConstructor, "funcInfo._metadata._bConstructor" );
			e.flagIf( method._bStatic, "funcInfo._metadata._bStatic" );
			e.flagIf( method._bConst, "funcInfo._metadata._bConst" );

			const string callArgs = makeInvokerCallArgs( method._paramTypeNames );

			emitMethodInvoker( e, typeInfo, method, retType, callArgs );
			e.pop();
			e.line( "}" );
		}
	}

	namespace
	{
		/** @brief registerTypeAlias / registerEnumAlias 호출 줄을 만듭니다. */
		static string emitAliasRegisterLines( const vector<string>& aliases, const string& canonical,
											  const bool bEnum )
		{
			if ( aliases.empty() )
				return {};

			CodeEmitBuffer buf;
			CodeEmit	   e( buf );
			e.push( 3 );
			const utf8* fn = bEnum ? "registry.registerEnumAlias" : "registry.registerTypeAlias";
			for ( const string& alias : aliases )
			{
				if ( alias.empty() || alias == canonical )
					continue;
				e.linef( "%#( \"%#\", \"%#\" );", fn, CodeEmit::escapeCppString( alias ),
						 CodeEmit::escapeCppString( canonical ) );
			}
			return string( buf.view() );
		}

	} // namespace

	string CodeGenerator::getModuleName() const
	{
		if ( _sourceFilePath.find( "GameFramework" ) != string::npos )
			return "GameFramework";
		if ( _sourceFilePath.find( "Games" ) != string::npos || _sourceFilePath.find( "SWGame" ) != string::npos )
			return "SWGame";
		if ( _sourceFilePath.find( "Editor" ) != string::npos )
			return "EditorModule";
		if ( _sourceFilePath.find( "App" ) != string::npos )
			return "App";
		return "Engine";
	}

	void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		const string id = sanitizeIdentifier( typeInfo._fullyQualifiedName );

		CodeEmitBuffer flagsBuf;
		{
			CodeEmit fe( flagsBuf );
			fe.push( 3 );
			fe.flagIf( typeInfo._bAbstract, "info._bAbstract" );
			fe.flagIf( typeInfo._bStatic, "info._bStatic" );
		}

		appendTemplate( out, tplConstants::kTypeRegistrarBegin, {
																	{		  "Id",							id},
																	{		  "FQN", typeInfo._fullyQualifiedName},
																	{	  "Name",				  typeInfo._name},
																	{ "ParentFQN",		   typeInfo._parentFQN},
																	{"ModuleName",				getModuleName()},
																	{	  "Flags",	   string( flagsBuf.view() )},
		} );

		CodeEmit e( out );
		e.push( 3 );

		if ( typeInfo._bComponentFactory && !typeInfo._bIsScript )
		{
			if ( typeInfo._parentFQN.empty() == false )
			{
				e.linef( "static_assert( sizeof(%#) == sizeof(%#), \"Component-derived classes with member variables MUST use REFLECT_SCRIPT() instead of REFLECT()!\" );", typeInfo._fullyQualifiedName, typeInfo._parentFQN );
			}
			else
			{
				e.linef( "static_assert( sizeof(%#) == sizeof(sw::Component), \"Component-derived classes with member variables MUST use REFLECT_SCRIPT() instead of REFLECT()!\" );", typeInfo._fullyQualifiedName );
			}
		}

		if ( typeInfo._listProperties.empty() == false )
		{
			e.line( "info._propertyList =" );
			e.line( "{" );
			e.push();
			for ( const ParsedPropertyInfo& prop : typeInfo._listProperties )
				emitPropertyInfoEntry( e, typeInfo, prop );
			e.pop();
			e.line( "};" );
		}

		if ( typeInfo._listMethods.empty() == false )
			emitMethodList( e, typeInfo );

		appendTemplate( out, tplConstants::kTypeRegistrarEnd,
						{
							{ "Id", id },
							{ "AliasRegs",
							  emitAliasRegisterLines( typeInfo._listAliases, typeInfo._fullyQualifiedName, false ) }
		 } );
	}

	void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
	{
		const string id = sanitizeIdentifier( enumInfo._fullyQualifiedName );

		const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
		const ParsedEnumeratorInfo* countEn	  = findEnumerator( enumInfo, enumInfo._countEnumerator );

		appendTemplate( out, tplConstants::kEnumRegistrarBegin, {
																	{		  "Id",																	id},
																	{		  "FQN",											 enumInfo._fullyQualifiedName},
																	{		  "Name",														  enumInfo._name},
																	{  "ModuleName",														getModuleName()},
																	{	  "IsBitFlag",							   enumInfo._bIsBitFlag ? "true" : "false"},
																	{  "HasInvalid",								invalidEn != nullptr ? "true" : "false"},
																	{"InvalidValue", invalidEn != nullptr ? to_string( invalidEn->_value ) : string( "0" )},
																	{	  "HasCount",								  countEn != nullptr ? "true" : "false"},
																	{  "CountValue",		countEn != nullptr ? to_string( countEn->_value ) : string( "0" )},
		} );

		CodeEmit e( out );
		e.push( 3 );

		if ( enumInfo._enumerators.empty() == false )
		{
			e.line( "info._mapNameToValue =" );
			e.line( "{" );
			e.push();
			for ( const ParsedEnumeratorInfo& en : enumInfo._enumerators )
				e.linef( "{ %#, %# },", CodeEmit::hs( en._name ), en._value );
			e.pop();
			e.line( "};" );

			e.line( "info._mapValueToName =" );
			e.line( "{" );
			e.push();
			for ( const ParsedEnumeratorInfo& en : enumInfo._enumerators )
				e.linef( "{ %#, %# },", en._value, CodeEmit::hs( en._name ) );
			e.pop();
			e.line( "};" );
		}

		for ( const auto& [alias, canonical] : enumInfo._valueAliases )
		{
			e.line( "{" );
			e.push();
			e.linef( "const auto it = info._mapNameToValue.find( %# );", CodeEmit::hs( canonical ) );
			e.line( "if ( it != info._mapNameToValue.end() )" );
			e.push();
			e.linef( "info._mapNameToValue.insert_or_assign( %#, it->second );", CodeEmit::hs( alias ) );
			e.pop();
			e.pop();
			e.line( "}" );
		}

		appendTemplate( out, tplConstants::kEnumRegistrarEnd,
						{
							{ "Id", id },
							{ "AliasRegs",
							  emitAliasRegisterLines( enumInfo._listAliases, enumInfo._fullyQualifiedName, true ) }
		} );
	}

	const ParsedEnumeratorInfo* CodeGenerator::findEnumerator( const ParsedEnumInfo& enumInfo, const string& spec )
	{
		if ( spec.empty() )
			return nullptr;
		const string leaf = enumeratorLeaf( spec );
		for ( const ParsedEnumeratorInfo& en : enumInfo._enumerators )
		{
			if ( en._name == spec || en._name == leaf )
				return &en;
		}
		return nullptr;
	}

	bool CodeGenerator::emitGeneratedHeader() const
	{
		CodeEmitBuffer buffer;
		CodeEmit	   e( buffer );
		e.line( ParserContext::getSharedConfig().emitAutoGeneratedBanner );
		e.line( "#pragma once" );
		e.blank();

		bool bNeedFlags = false;
		for ( const ParsedEnumInfo& enumInfo : _enums )
		{
			if ( enumInfo._bEmitFlagOps )
				bNeedFlags = true;
			if ( enumInfo._invalidEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._invalidEnumerator ) == nullptr )
				SW_LOG_WARNING( "[CodeGenerator] ENUM(Invalid=%#) not found on %#", enumInfo._invalidEnumerator,
								enumInfo._fullyQualifiedName );
			if ( enumInfo._countEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._countEnumerator ) == nullptr )
				SW_LOG_WARNING( "[CodeGenerator] ENUM(Count=%#) not found on %#", enumInfo._countEnumerator,
								enumInfo._fullyQualifiedName );
		}

		if ( bNeedFlags )
		{
			e.line( "#include \"Engine/EngineMinimal.h\"" );
			e.blank();
		}

		for ( const ParsedEnumInfo& enumInfo : _enums )
		{
			if ( enumInfo._bEmitFlagOps == 0 )
				continue;

			const string& fqn = enumInfo._fullyQualifiedName;
			e.linef( "SW_INLINE constexpr %# operator|( %# lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
			e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );", fqn );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %# operator&( %# lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
			e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );", fqn );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %# operator^( %# lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
			e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) ^ static_cast<Underlying>( rhs ) );", fqn );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %# operator~( %# val )", fqn, fqn );
			e.line( "{" );
			e.push();
			e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
			e.linef( "return static_cast<%#>( ~static_cast<Underlying>( val ) );", fqn );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %#& operator|=( %#& lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.line( "return lhs = lhs | rhs;" );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %#& operator&=( %#& lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.line( "return lhs = lhs & rhs;" );
			e.pop();
			e.line( "}" );
			e.linef( "SW_INLINE constexpr %#& operator^=( %#& lhs, %# rhs )", fqn, fqn, fqn );
			e.line( "{" );
			e.push();
			e.line( "return lhs = lhs ^ rhs;" );
			e.pop();
			e.line( "}" );
			e.blank();
		}

		const string newContent( buffer.view() );
		if ( FileUtil::fileExists( _outputHeaderPath ) )
		{
			string existingContent;
			FileUtil::readTextFile( _outputHeaderPath, existingContent );
			if ( existingContent.empty() == false && existingContent == newContent )
				return true;
		}
		if ( FileUtil::writeTextFile( _outputHeaderPath, newContent ) == false )
		{
			SW_LOG_ERROR( "[CodeGenerator] Failed to write %#", _outputHeaderPath );
			return false;
		}
		SW_LOG_INFO( "[CodeGenerator] Generated: %#", _outputHeaderPath );
		return true;
	}

	string CodeGenerator::sanitizeIdentifier( const string& fqn )
	{
		string result = fqn;
		size_t pos	  = 0;
		while ( ( pos = result.find( "::", pos ) ) != string::npos )
		{
			result.replace( pos, 2, "_" );
			pos += 1;
		}
		for ( utf8& c : result )
		{
			if ( std::isalnum( static_cast<uint8>( c ) ) == 0 && c != '_' )
				c = '_';
		}
		return result;
	}
} // namespace sw
