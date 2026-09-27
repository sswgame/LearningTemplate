#include "pch.h"

#include "ReflectionParser/CodeGenerator.h"

#include "Core/Common/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/AnnotationFields.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserConfig.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/TypeNameMap.h"

SW_LOG_CALLER( "CodeGenerator" );
namespace sw
{
    namespace
    {
        struct CodeGeneratorInternal
        {
            static constexpr int32 kMaxNestedContainerDepth = 3;

            /**
             * @brief 열거형 전체 FQN(예: "sw::EState::Idle")에서 말단 열거자 이름("Idle")을 추출합니다.
             */
            static string_view enumeratorLeaf( string_view spec )
            {
                return ParserUtil::scopeLeaf( spec );
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
             * @brief 2중, 3중 중첩 컨테이너(예: vector<vector<int32>>)에 대한 중첩 래퍼 표기를 생성합니다.
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
            static string makeCtorLookupName( const ParsedFunctionInfo& method, const ParserSession& session )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.append( annotationConstants::kCtorLookupName );
                if ( method._listParameterTypeName.empty() == false )
                {
                    b.append( '(' );
                    for ( size_t paramIndex = 0; paramIndex < method._listParameterTypeName.size(); ++paramIndex )
                    {
                        if ( paramIndex > 0 )
                            b.append( ',' );
                        b.append( session._typeNameMap.normalize( method._listParameterTypeName[paramIndex] ) );
                    }
                    b.append( ')' );
                }
                return string( b.view() );
            }

            /**
             * @brief 타입 이름 목록을 C++ 배열 초기화 구문 `{ "int32", "string" }` 형태로 포맷팅합니다.
             */
            static string makeQuotedTypeList( const vector<string>& listType, const ParserSession& session )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.append( "{ " );
                for ( size_t typeIndex = 0; typeIndex < listType.size(); ++typeIndex )
                {
                    if ( typeIndex > 0 )
                        b.append( ", " );
                    b.appendFormat( "\"%#\"", session._typeNameMap.normalize( listType[typeIndex] ) );
                }
                b.append( " }" );
                return string( b.view() );
            }

            /**
             * @brief 런타임 동적 함수 호출(Invoker)을 위한 인자 추출 구문 `args.get<T>(0), args.get<T>(1)...`을 생성합니다.
             */
            static string makeInvokerCallArgs( const vector<string>& listType, const ParserSession& session )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                for ( size_t typeIndex = 0; typeIndex < listType.size(); ++typeIndex )
                {
                    if ( typeIndex > 0 )
                        b.append( ", " );
                    b.appendFormat( "args.get<%#>( %# )", session._typeNameMap.normalize( listType[typeIndex] ),
                                    static_cast<uint32>( typeIndex ) );
                }
                return string( b.view() );
            }

            /** @brief registerTypeAlias / registerEnumAlias 호출 줄을 만듭니다. */
            static string emitAliasRegisterLines( const vector<string>& aliases, const string& canonical,
                                                  const bool bEnum )
            {
                if ( aliases.empty() )
                    return {};

                CodeEmitBuffer buf;
                CodeEmit       emit( buf );
                emit.push( 3 );
                const utf8* pRegisterFunc = bEnum ? "registry.registerEnumAlias" : "registry.registerTypeAlias";
                for ( const string& alias : aliases )
                {
                    if ( alias.empty() || alias == canonical )
                        continue;
                    emit.linef( "%#( \"%#\", \"%#\" );", pRegisterFunc, CodeEmit::escapeCppString( alias ),
                                CodeEmit::escapeCppString( canonical ) );
                }
                return string( buf.view() );
            }
        };

        /**
         * @brief 열거형 하나를 불투명(opaque) 선언으로 앞세웁니다.
         * @details `namespace sw::editor { enum class EditorPanelFlags : unsigned char; }` 형태입니다.
         *          네임스페이스가 없으면(전역 열거형) 선언만 씁니다. 기반 타입은 정본 철자를 쓰므로
         *          원본이 `uint8` 이어도 `unsigned char` 로 나오고, 같은 타입이라 재선언이 어긋나지
         *          않습니다.
         */
        void emitEnumForwardDeclaration( CodeEmit& emit, const ParsedEnumInfo& enumInfo )
        {
            const string_view fqn           = enumInfo._fullyQualifiedName;
            const size_t      lastSeparator = fqn.rfind( "::" );
            const string_view enumName      = ( lastSeparator == string_view::npos ) ? fqn : fqn.substr( lastSeparator + 2 );
            const string_view namespacePath = ( lastSeparator == string_view::npos ) ? string_view{} : fqn.substr( 0, lastSeparator );

            if ( namespacePath.empty() )
            {
                emit.linef( "enum class %# : %#;", enumName, enumInfo._underlyingType );
                return;
            }
            emit.linef( "namespace %# { enum class %# : %#; }", namespacePath, enumName, enumInfo._underlyingType );
        }
    } // namespace
} // namespace sw

namespace sw
{
    CodeGenerator::CodeGenerator( const ParsedHeader& header, const string& sourceFilePath, const ParserSession& session,
                                  const string& sourceRoot )
        : _header{ header }
        , _session{ session }
        , _sourceFilePath{ sourceFilePath }
        , _moduleName{}
    {
        _moduleName = makeModuleName( sourceRoot );
    }

    void CodeGenerator::appendTemplate( CodeEmitBuffer& out, const string_view name, const EmitTemplateStore::TemplateVars vars ) const
    {
        out.append( _session._emitTemplateStore.render( name, vars ) );
    }

    string CodeGenerator::makeSourceText() const
    {
        CodeEmitBuffer buffer;
        if ( _header._listType.empty() && _header._listEnum.empty() )
        {
            buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
            return string( buffer.view() );
        }

        emitFileHeader( buffer );
        buffer.append( _session._config._emitGeneratedNsOpen );
        for ( const ParsedTypeInfo& typeInfo : _header._listType )
            emitTypeRegistrar( buffer, typeInfo );
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
            emitEnumRegistrar( buffer, enumInfo );
        buffer.append( _session._config._emitGeneratedNsClose );

        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            if ( typeInfo.wantsComponentFactory() )
                emitComponentFactoryRegistrar( buffer, typeInfo );
        }

        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            emitReflectTypeTraits( buffer, typeInfo );
            if ( typeInfo.wantsTypeApi() )
                emitTypeInfoAccessors( buffer, typeInfo );
        }
        return string( buffer.view() );
    }

    void CodeGenerator::emitFileHeader( CodeEmitBuffer& out ) const
    {
        appendTemplate( out, tplConstants::kFileHeader, {
                                                            { templateKeyConstants::kSourcePath, _sourceFilePath }
        } );
    }

    void CodeGenerator::emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kReflectTypeTraits, {
                                                                   { templateKeyConstants::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kTypeInfoAccessors, {
                                                                   { templateKeyConstants::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kComponentFactoryRegistrar, {
                                                                           {        templateKeyConstants::kId, sanitizeIdentifier( typeInfo._fullyQualifiedName )},
                                                                           {       templateKeyConstants::kFqn,                       typeInfo._fullyQualifiedName},
                                                                           {      templateKeyConstants::kName,                                     typeInfo._name},
                                                                           {templateKeyConstants::kModuleName,                                        _moduleName},
        } );
    }

    void CodeGenerator::emitPropertyMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop ) const
    {
        AnnotationFields::emitMetadata( emit, prop, "p._metadata." );

        // 범위는 값 둘과 "있음" 표시 하나가 함께 가는 Manual 필드다.
        if ( prop._bHasRange != SW_FALSE )
        {
            // 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가
            // **생성된 파일마다** 난다. 여기서 한 번 고치면 전부 사라진다.
            emit.linef( "p._metadata._minRange     = %#f;", prop._minRange );
            emit.linef( "p._metadata._maxRange     = %#f;", prop._maxRange );
            emit.assign( "p._metadata._bHasRange", "SW_TRUE" );
        }
    }

    void CodeGenerator::emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                                 const ParsedPropertyInfo& prop ) const
    {
        if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
            return;

        const utf8*  outerKind    = toCppExpr( prop._containerKind );
        const string outerWrapper = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

        emit.line( "{" );
        emit.push();
        emit.line( "auto nested0 = sw::make_shared<sw::NestedContainerInfo>();" );
        emit.assign( "nested0->_kind", outerKind );
        emit.linef( "nested0->_typeName = %#;", CodeEmit::hs( prop._containerTree->_typeName ) );
        emit.linef( "nested0->_elementTypeName = %#;", CodeEmit::hs( _session._typeNameMap.normalize( prop._elementTypeName ) ) );
        emit.linef( "nested0->_keyTypeName = %#;", CodeEmit::hs( _session._typeNameMap.normalize( prop._keyTypeName ) ) );
        emit.linef( "nested0->_wrapper = sw::make_shared<%#>();", outerWrapper );

        const ParsedContainerNode* node     = prop._containerTree->_elementNested.get();
        ContainerKind              prevKind = prop._containerKind;
        int32                      depth    = 1;

        if ( node != nullptr && node->_bIsContainer )
        {
            emit.linef( "using NestC0 = decltype( std::declval<%#>().%# );", typeInfo._fullyQualifiedName, prop._name );
            while ( node != nullptr && node->_bIsContainer && depth < CodeGeneratorInternal::kMaxNestedContainerDepth )
            {
                const utf8* kind = toCppExpr( node->_containerKind );
                const utf8* peel = containerPeelMember( prevKind );
                emit.linef( "using NestC%# = typename NestC%#::%#;", depth, depth - 1, peel );

                const string wrapperType = CodeGeneratorInternal::makeNestedWrapperType( node->_containerType, depth );
                emit.line( "{" );
                emit.push();
                emit.linef( "auto nested%# = sw::make_shared<sw::NestedContainerInfo>();", depth );
                emit.linef( "nested%#->_kind = %#;", depth, kind );
                emit.linef( "nested%#->_typeName = %#;", depth, CodeEmit::hs( node->_typeName ) );
                emit.linef( "nested%#->_elementTypeName = %#;", depth,
                            CodeEmit::hs( _session._typeNameMap.normalize( node->_elementTypeName ) ) );
                emit.linef( "nested%#->_keyTypeName = %#;", depth, CodeEmit::hs( node->_keyTypeName ) );
                emit.linef( "nested%#->_wrapper = sw::make_shared<%#>();", depth, wrapperType );
                emit.linef( "nested%#->_elementNested = nested%#;", depth - 1, depth );
                emit.pop();
                emit.line( "}" );

                prevKind = node->_containerKind;
                node     = ( node->_elementNested != nullptr ) ? node->_elementNested.get() : nullptr;
                ++depth;
            }
        }

        emit.assign( "p._nestedContainer", "nested0" );
        emit.pop();
        emit.line( "}" );
    }

    void CodeGenerator::emitPropertyInfoEntry( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                               const ParsedPropertyInfo& prop ) const
    {
        emit.line( "[]() {" );
        emit.push();
        // PROPERTY() 에 값으로 담으면 안 되는 기반 타입을 컴파일 타임에 막는다.
        // 목록은 parser_config 의 emit.value_forbidden_base_types 에서 온다(비면 생략).
        emit.linef( "using PropDecl = decltype(%#::%#);", typeInfo._fullyQualifiedName, prop._name );
        const ParserConfig& cfg = _session._config;
        if ( cfg._listValueForbiddenBaseType.empty() == false )
        {
            string condition;
            for ( const string& baseType : cfg._listValueForbiddenBaseType )
            {
                if ( condition.empty() == false )
                    condition += " || ";
                condition += "std::is_base_of_v<";
                condition += baseType;
                condition += ", std::remove_cv_t<std::remove_reference_t<PropDecl>>>";
            }
            emit.linef( "constexpr bool kIsInvalidValue = std::is_pointer_v<std::remove_cv_t<std::remove_reference_t<PropDecl>>> == false && (%#);",
                        condition );
            emit.linef( "static_assert(!kIsInvalidValue, \"%#\");", cfg._valueForbiddenMessage );
        }
        emit.line( "sw::PropertyInfo p(" );
        emit.push();
        emit.linef( "%#,", CodeEmit::hs( prop._name ) );
        emit.linef( "%#,", CodeEmit::hs( _session._typeNameMap.normalize( prop._typeName ) ) );
        if ( prop._bIsBitField == SW_TRUE )
            emit.linef( "%#u,", prop._byteOffset );
        else
            emit.linef( "offsetof(%#, %#),", typeInfo._fullyQualifiedName, prop._name );

        if ( prop._bIsContainer )
        {
            const utf8*  kindStr     = toCppExpr( prop._containerKind );
            const string wrapperType = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

            emit.line( "true," );
            emit.linef( "%#,", kindStr );
            emit.linef( "%#,", CodeEmit::hs( _session._typeNameMap.normalize( prop._elementTypeName ) ) );
            emit.linef( "%#,", CodeEmit::hs( _session._typeNameMap.normalize( prop._keyTypeName ) ) );
            emit.linef( "sw::make_shared<%#>() );", wrapperType );

            emitNestedContainerTree( emit, typeInfo, prop );
        }
        else
        {
            emit.line( "false, sw::ContainerKind::None," );
            emit.line( "::sw::hashed_string(), ::sw::hashed_string(), nullptr );" );
        }

        emit.pop(); // 생성자 인자 들여쓰기
        if ( prop._bIsBitField == SW_TRUE )
        {
            emit.line( "p._bIsBitField = SW_TRUE;" );
            emit.linef( "p._bitOffset = %#;", prop._bitOffset );
            emit.linef( "p._bitMask = %#;", prop._bitMask );
        }
        if ( prop._listAlias.empty() == false )
        {
            emit.line( "p._listAlias = {" );
            emit.push();
            for ( const string& alias : prop._listAlias )
                emit.linef( "%#,", CodeEmit::hs( alias ) );
            emit.pop();
            emit.line( "};" );
        }
        emitPropertyMetadata( emit, prop );
        emit.line( "return p;" );
        emit.pop();
        emit.line( "}()," );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& retType,
                                           const string& callArgs ) const
    {
        emit.line( "auto invokerCb = []( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
        emit.line( "{" );
        emit.push();
        if ( method._listParameterTypeName.empty() )
            emit.line( "(void)args;" );

        if ( method._bStatic != SW_FALSE && method._bConstructor == SW_FALSE )
        {
            emit.line( "(void)objPtr;" );
            if ( retType == annotationConstants::kVoidTypeName )
            {
                emit.linef( "%#::%#(%#);", typeInfo._fullyQualifiedName, method._name, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                emit.linef( "return ::sw::TaskValue{ %#::%#(%#) };", typeInfo._fullyQualifiedName, method._name, callArgs );
            }
        }
        else
        {
            emit.linef( "auto* self = static_cast<%#*>( objPtr );", typeInfo._fullyQualifiedName );
            if ( method._bConstructor != SW_FALSE )
            {
                emit.linef( "new ( self ) %#(%#);", typeInfo._fullyQualifiedName, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else if ( retType == annotationConstants::kVoidTypeName )
            {
                emit.linef( "self->%#(%#);", method._name, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                emit.linef( "return ::sw::TaskValue{ self->%#(%#) };", method._name, callArgs );
            }
        }
        emit.pop();
        emit.line( "};" );
        emit.line( "funcInfo._invoker = SW_DELEGATE_LAMBDA( ::sw::Delegate<::sw::TaskValue( void*, const ::sw::TaskArgs& )>, invokerCb );" );
        emit.line( "info._listMethod.push_back( funcInfo );" );
    }

    void CodeGenerator::emitMethodList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            const string retType = _session._typeNameMap.normalize( method._returnTypeName );

            const string lookupName = ( method._bConstructor != SW_FALSE ) ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;

            emit.line( "{" );
            emit.push();
            emit.line( "::sw::FunctionInfo funcInfo;" );
            emit.assign( "funcInfo._name", CodeEmit::quoted( ( method._bConstructor != SW_FALSE ) ? annotationConstants::kCtorLookupName : method._name ) );
            emit.linef( "funcInfo._hashName       = %#;", CodeEmit::hs( lookupName ) );
            emit.assign( "funcInfo._returnTypeName", CodeEmit::quoted( retType ) );
            emit.assign( "funcInfo._listParameterTypeName", CodeGeneratorInternal::makeQuotedTypeList( method._listParameterTypeName, _session ) );

            AnnotationFields::emitMetadata( emit, method, "funcInfo._metadata." );

            // 애노테이션이 아니라 선언에서 온 사실들이다.
            emit.flagIf( method._bConstructor != SW_FALSE, "funcInfo._metadata._bConstructor", "SW_TRUE" );
            emit.flagIf( method._bStatic != SW_FALSE, "funcInfo._metadata._bStatic", "SW_TRUE" );
            emit.flagIf( method._bConst != SW_FALSE, "funcInfo._metadata._bConst", "SW_TRUE" );

            const string callArgs = CodeGeneratorInternal::makeInvokerCallArgs( method._listParameterTypeName, _session );

            emitMethodInvoker( emit, typeInfo, method, retType, callArgs );
            emit.pop();
            emit.line( "}" );
        }
    }

    string CodeGenerator::makeModuleName( const string& sourceRoot ) const
    {
        const ParserConfig& config = _session._config;

        // 절대 경로로 매칭하면 리포지토리를 담은 상위 폴더 이름(예: .../AppData/..., D:/Games/...)이
        // 규칙에 걸려 모든 타입이 엉뚱한 모듈로 등록된다. 소스 루트 기준 상대 경로로만 본다.
        string relativePath = _sourceFilePath;
        if ( sourceRoot.empty() == false )
        {
            const size_t rootPos = _sourceFilePath.find( sourceRoot );
            if ( rootPos != string::npos )
                relativePath = _sourceFilePath.substr( rootPos + sourceRoot.size() );
        }

        for ( const ParserConfig::ModuleRule& rule : config._listModuleRule )
        {
            if ( rule._pathContains.empty() == false && relativePath.find( rule._pathContains ) != string::npos )
                return rule._module;
        }
        return config._defaultModule;
    }

    void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        const string registrarName = sanitizeIdentifier( typeInfo._fullyQualifiedName );

        CodeEmitBuffer flagsBuf;
        {
            CodeEmit fe( flagsBuf );
            fe.push( 3 );
            fe.flagIf( typeInfo._bAbstract, "info._bAbstract" );
            fe.flagIf( typeInfo._bStatic, "info._bStatic" );
        }

        appendTemplate( out, tplConstants::kTypeRegistrarBegin, {
                                                                    {        templateKeyConstants::kId,                registrarName},
                                                                    {       templateKeyConstants::kFqn, typeInfo._fullyQualifiedName},
                                                                    {      templateKeyConstants::kName,               typeInfo._name},
                                                                    { templateKeyConstants::kParentFqn,          typeInfo._parentFQN},
                                                                    {templateKeyConstants::kModuleName,                  _moduleName},
                                                                    {     templateKeyConstants::kFlags,    string( flagsBuf.view() )},
        } );

        CodeEmit emit( out );
        emit.push( 3 );

        AnnotationFields::emitMetadata( emit, typeInfo, "info._metadata." );

        if ( typeInfo._listProperty.empty() == false )
        {
            emit.line( "info._listProperty =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
                emitPropertyInfoEntry( emit, typeInfo, prop );
            emit.pop();
            emit.line( "};" );
        }

        if ( typeInfo._listMethod.empty() == false )
            emitMethodList( emit, typeInfo );

        appendTemplate( out, tplConstants::kTypeRegistrarEnd,
                        {
                            { templateKeyConstants::kId, registrarName },
                            { templateKeyConstants::kFqn, typeInfo._fullyQualifiedName },
                            { templateKeyConstants::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( typeInfo._listAlias, typeInfo._fullyQualifiedName, false ) }
        } );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
    {
        const string registrarName = sanitizeIdentifier( enumInfo._fullyQualifiedName );

        const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = findEnumerator( enumInfo, enumInfo._countEnumerator );

        appendTemplate( out, tplConstants::kEnumRegistrarBegin, {
                                                                    {          templateKeyConstants::kId,                                                         registrarName},
                                                                    {         templateKeyConstants::kFqn,                                          enumInfo._fullyQualifiedName},
                                                                    {        templateKeyConstants::kName,                                                        enumInfo._name},
                                                                    {  templateKeyConstants::kModuleName,                                                           _moduleName},
                                                                    {   templateKeyConstants::kIsBitFlag,                               enumInfo._bIsBitFlag ? "true" : "false"},
                                                                    {  templateKeyConstants::kHasInvalid,                               invalidEn != nullptr ? "true" : "false"},
                                                                    {templateKeyConstants::kInvalidValue, invalidEn != nullptr ? to_string( invalidEn->_value ) : string( "0" )},
                                                                    {    templateKeyConstants::kHasCount,                                 countEn != nullptr ? "true" : "false"},
                                                                    {  templateKeyConstants::kCountValue,     countEn != nullptr ? to_string( countEn->_value ) : string( "0" )},
        } );

        CodeEmit emit( out );
        emit.push( 3 );

        // EnumInfo 는 메타데이터 블록 없이 `_mapCustomMeta` 를 직접 든다.
        AnnotationFields::emitMetadata( emit, enumInfo, "info." );

        if ( enumInfo._listEnumerator.empty() == false )
        {
            emit.line( "info._mapNameToValue =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                emit.linef( "{ %#, %# },", CodeEmit::hs( en._name ), en._value );
            emit.pop();
            emit.line( "};" );

            emit.line( "info._mapValueToName =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                emit.linef( "{ %#, %# },", en._value, CodeEmit::hs( en._name ) );
            emit.pop();
            emit.line( "};" );
        }

        for ( const auto& [alias, canonical] : enumInfo._listValueAlias )
        {
            emit.line( "{" );
            emit.push();
            emit.linef( "const auto it = info._mapNameToValue.find( %# );", CodeEmit::hs( canonical ) );
            emit.line( "if ( it != info._mapNameToValue.end() )" );
            emit.push();
            emit.linef( "info._mapNameToValue.insert_or_assign( %#, it->second );", CodeEmit::hs( alias ) );
            emit.pop();
            emit.pop();
            emit.line( "}" );
        }

        appendTemplate( out, tplConstants::kEnumRegistrarEnd,
                        {
                            { templateKeyConstants::kId, registrarName },
                            { templateKeyConstants::kFqn, enumInfo._fullyQualifiedName },
                            { templateKeyConstants::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( enumInfo._listAlias, enumInfo._fullyQualifiedName, true ) }
        } );
    }

    const ParsedEnumeratorInfo* CodeGenerator::findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec )
    {
        if ( spec.empty() )
            return nullptr;
        const string_view leaf = CodeGeneratorInternal::enumeratorLeaf( spec );
        for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
        {
            if ( en._name == spec || en._name == leaf )
                return &en;
        }
        return nullptr;
    }

    bool CodeGenerator::makeHeaderText( string& outText ) const
    {
        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( _session._config._emitAutoGeneratedBanner );
        emit.line( "#pragma once" );
        emit.blank();

        bool bNeedFlags = false;
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            if ( enumInfo._bIsBitFlag == SW_TRUE )
                bNeedFlags = true;
            if ( enumInfo._invalidEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._invalidEnumerator ) == nullptr )
                SW_LOG_WARNING( "ENUM(Invalid=%#) not found on %#", enumInfo._invalidEnumerator,
                                enumInfo._fullyQualifiedName );
            if ( enumInfo._countEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._countEnumerator ) == nullptr )
                SW_LOG_WARNING( "ENUM(Count=%#) not found on %#", enumInfo._countEnumerator,
                                enumInfo._fullyQualifiedName );
        }

        if ( bNeedFlags )
        {
            // 비트 연산자(|, &, ^, ~, |=, &=, ^=)와 hasFlag/hasAnyFlag/setFlag/clearFlag 는 enum 마다
            // 코드젠하지 않고, Core/Common/EnumUtil.h 의 제네릭 sw::IsBitFlagEnum<E> 트레이트와 전역
            // 스코프 SFINAE 연산자로 통일한다. 로직이 모든 enum 에서 같아 타입별 코드젠이 필요
            // 없다. 여기서는 그 트레이트를 켜는(opt-in) 한 줄짜리 명시적 특수화만 생성한다.
            //
            // **열거형을 전방 선언한 뒤 특수화한다.** 원본 헤더를 include 하지 않는다. 이 파일을
            // 모으는 우산(`FlagOps.gen.h`)이 타깃 전 TU 에 `/FI` 로 들어가므로, 여기서 원본 헤더를
            // 들이면 그 헤더가 끌어오는 것 전부가 **모든 TU 에 이미 있는 이름**이 되어 다른 헤더들의
            // include 누락을 통째로 가린다(2026-09-18 에 Engine 헤더 230 개 중 5 개가 그렇게 숨어
            // 있었다). 불투명 열거형 선언은 완전한 타입이라 특수화에 이것으로 충분하다.
            emit.line( "#include \"Core/Common/EnumUtil.h\"" );
            emit.blank();
            for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
            {
                if ( enumInfo._bIsBitFlag == SW_FALSE )
                    continue;

                if ( enumInfo._bNestedInType != SW_FALSE )
                {
                    // 클래스 안의 열거형은 밖에서 전방 선언할 수 없어 **비트 연산자 트레이트만**
                    // 코드젠하지 못한다. 등록부의 비트플래그 표시(`EnumInfo::_bIsBitFlag`)는 그와
                    // 무관하게 유효하다. 인스펙터와 문자열 변환은 연산자를 쓰지 않는다. 그래서
                    // 여기서 멈추지 않고 연산자만 건너뛴다.
                    //
                    // 예전에는 여기서 코드젠을 실패시켰고, 그 탓에 중첩 열거형은 `ENUM( Flags )`
                    // 를 **쓸 수가 없어** 값 모양 자동 감지에 기대야 했다. 그 자동 감지가 평범한
                    // 연속 열거형까지 플래그로 만들던 장본인이다. `|` · `&` 를 쓰면 그 자리에서
                    // 컴파일이 막히므로 조용히 잘못될 여지는 없다.
                    SW_LOG_WARNING( "ENUM(Flags) 가 클래스 안에 있어 비트 연산자는 코드젠하지 않습니다 "
                                    "(등록부의 비트플래그 표시는 유지): %#",
                                    enumInfo._fullyQualifiedName );
                    continue;
                }
                if ( enumInfo._underlyingType.empty() )
                {
                    SW_LOG_ERROR( "ENUM(Flags) 의 기반 정수 타입을 알아내지 못했습니다: %#", enumInfo._fullyQualifiedName );
                    return false;
                }

                emitEnumForwardDeclaration( emit, enumInfo );
                emit.linef( "template <> struct sw::IsBitFlagEnum<%#> : std::true_type {};", enumInfo._fullyQualifiedName );
                emit.blank();
            }
        }

        outText = string( buffer.view() );
        return true;
    }

    string CodeGenerator::sanitizeIdentifier( string_view fqn )
    {
        string result( fqn );
        size_t pos = 0;
        while ( ( pos = result.find( "::", pos ) ) != string::npos )
        {
            result.replace( pos, 2, "_" );
            pos += 1;
        }
        for ( utf8& c : result )
        {
            const bool bIsAlphaNum = ( 'a' <= c && c <= 'z' ) || ( 'A' <= c && c <= 'Z' ) || ( '0' <= c && c <= '9' );
            if ( bIsAlphaNum == false && c != '_' )
                c = '_';
        }
        return result;
    }
} // namespace sw
