#include "pch.h"

#include "ReflectionParser/CodeGenerator.h"

#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

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
                b.append( annotation::kCtorLookupName );
                if ( method._listParameter.empty() == false )
                {
                    b.append( '(' );
                    for ( size_t paramIndex = 0; paramIndex < method._listParameter.size(); ++paramIndex )
                    {
                        if ( paramIndex > 0 )
                            b.append( ',' );
                        b.append( session._typeNameMap.normalize( method._listParameter[paramIndex]._typeName ) );
                    }
                    b.append( ')' );
                }
                return string( b.view() );
            }

            /** @brief 인자 타입의 변환 표(`ReflectTypeOpsOf<T>::kOps`) 주소 식입니다. 인자 · 반환 타입이 같은 철자를 씁니다(호출기의 `args.get<T>` 와도). */
            static string makeTypeOpsExpr( const string& normalizedType )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.appendFormat( "&::sw::ReflectTypeOpsOf<%#>::kOps", normalizedType );
                return string( b.view() );
            }

            /**
             * @brief `FunctionParameterInfo` 목록 대입을 씁니다 — 이름 · 정규 타입 · 기본 인자 글 · 변환 표.
             * @param bWithTypeOps 이벤트는 변환 표를 `EventInfo::setOps` 가 템플릿 인자에서 채우므로 nullptr 를 씁니다.
             */
            static void emitParameterList( CodeEmit& emit, const string_view target, const vector<ParsedParameterInfo>& listParameter,
                                           const ParserSession& session, const bool bWithTypeOps )
            {
                if ( listParameter.empty() )
                    return;
                emit.linef( "%# =", target );
                emit.line( "{" );
                emit.push();
                for ( const ParsedParameterInfo& parameter : listParameter )
                {
                    const string typeName = session._typeNameMap.normalize( parameter._typeName );
                    emit.linef( "::sw::FunctionParameterInfo( %#, %#, %#, %# ),", CodeEmit::quoted( parameter._name ), CodeEmit::quoted( typeName ),
                                CodeEmit::quoted( parameter._defaultValue ), bWithTypeOps ? makeTypeOpsExpr( typeName ) : string( "nullptr" ) );
                }
                emit.pop();
                emit.line( "};" );
            }

            /**
             * @brief 런타임 동적 함수 호출(Invoker)을 위한 인자 추출 구문 `args.get<T>(0), args.get<T>(1)...`을 생성합니다.
             */
            static string makeInvokerCallArgs( const vector<ParsedParameterInfo>& listParameter, const ParserSession& session )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    if ( paramIndex > 0 )
                        b.append( ", " );
                    b.appendFormat( "args.get<%#>( %# )", session._typeNameMap.normalize( listParameter[paramIndex]._typeName ),
                                    static_cast<uint32>( paramIndex ) );
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
        {
            emitTypeRegistrar( buffer, typeInfo );
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            emitEnumRegistrar( buffer, enumInfo );
        }
        buffer.append( _session._config._emitGeneratedNsClose );

        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            emitReflectTypeTraits( buffer, typeInfo );
            if ( typeInfo.requiresTypeAPI() )
                emitTypeInfoAccessors( buffer, typeInfo );
        }
        return string( buffer.view() );
    }

    void CodeGenerator::emitFileHeader( CodeEmitBuffer& out ) const
    {
        appendTemplate( out, templatefile::kFileHeader, {
                                                            { templatekey::kSourcePath, _sourceFilePath }
        } );
    }

    void CodeGenerator::emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, templatefile::kReflectTypeTraits, {
                                                                   { templatekey::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, templatefile::kTypeInfoAccessors, {
                                                                   { templatekey::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitPropertyMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop ) const
    {
        AnnotationFields::emitMetadata( emit, prop, "p._metadata." );

        // 범위는 경계마다 값과 "있음" 표시가 함께 가는 Manual 필드다. 적힌 쪽만 낸다 — 표시가 하나면 `Min` 만 적어도
        // 위 경계(기본 1)까지 나가 인스펙터가 그 값을 1 에서 막는다.
        // 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가 **생성된 파일마다** 난다.
        if ( prop._bHasMinRange != SW_FALSE )
        {
            emit.linef( "p._metadata._minRange     = %#f;", prop._minRange );
            emit.assign( "p._metadata._bHasMinRange", "SW_TRUE" );
        }
        if ( prop._bHasMaxRange != SW_FALSE )
        {
            emit.linef( "p._metadata._maxRange     = %#f;", prop._maxRange );
            emit.assign( "p._metadata._bHasMaxRange", "SW_TRUE" );
        }
        // 슬라이더 범위는 에디터 메타다(Shipping 에 멤버가 없다).
        if ( prop._bHasUiMinRange != SW_FALSE || prop._bHasUiMaxRange != SW_FALSE )
        {
            emit.line( "#if !defined( SW_SHIPPING )" );
            if ( prop._bHasUiMinRange != SW_FALSE )
            {
                emit.linef( "p._metadata._uiMinRange   = %#f;", prop._uiMinRange );
                emit.assign( "p._metadata._bHasUiMinRange", "SW_TRUE" );
            }
            if ( prop._bHasUiMaxRange != SW_FALSE )
            {
                emit.linef( "p._metadata._uiMaxRange   = %#f;", prop._uiMaxRange );
                emit.assign( "p._metadata._bHasUiMaxRange", "SW_TRUE" );
            }
            emit.line( "#endif" );
        }
    }

    void CodeGenerator::emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                                 const ParsedPropertyInfo& prop ) const
    {
        if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
            return;

        const utf8*  outerKind    = toCppExpr( prop._containerKind );
        const string outerWrapper = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._memberName );

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
            emit.linef( "using NestC0 = decltype( std::declval<%#>().%# );", typeInfo._fullyQualifiedName, prop._memberName );
            while ( node != nullptr && node->_bIsContainer && depth < CodeGeneratorInternal::kMaxNestedContainerDepth )
            {
                const utf8* kind              = toCppExpr( node->_containerKind );
                const utf8* elementTypeMember = containerElementTypeMember( prevKind );
                emit.linef( "using NestC%# = typename NestC%#::%#;", depth, depth - 1, elementTypeMember );

                // 단계마다 블록으로 감싸지 않는다 — 다음 단계가 앞 단계의 `nested<n>` 에 자기를 잇는다(세 겹이면 블록 밖의 이름을 본다).
                const string wrapperType = CodeGeneratorInternal::makeNestedWrapperType( node->_containerType, depth );
                emit.linef( "auto nested%# = sw::make_shared<sw::NestedContainerInfo>();", depth );
                emit.linef( "nested%#->_kind = %#;", depth, kind );
                emit.linef( "nested%#->_typeName = %#;", depth, CodeEmit::hs( node->_typeName ) );
                emit.linef( "nested%#->_elementTypeName = %#;", depth,
                            CodeEmit::hs( _session._typeNameMap.normalize( node->_elementTypeName ) ) );
                emit.linef( "nested%#->_keyTypeName = %#;", depth, CodeEmit::hs( node->_keyTypeName ) );
                emit.linef( "nested%#->_wrapper = sw::make_shared<%#>();", depth, wrapperType );
                emit.linef( "nested%#->_elementNested = nested%#;", depth - 1, depth );

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
        // 접근자 프로퍼티(값이 객체 밖)는 필드가 없다. 선언 타입은 메서드가 돌려주는 참조의 대상이다.
        if ( prop._bIsAccessor == SW_TRUE )
            emit.linef( "using PropDecl = std::remove_reference_t<decltype( std::declval<%#&>().%#() )>;", typeInfo._fullyQualifiedName, prop._memberName );
        else
            emit.linef( "using PropDecl = decltype(%#::%#);", typeInfo._fullyQualifiedName, prop._memberName );
        const ParserConfig& config = _session._config;
        if ( config._listValueForbiddenBaseType.empty() == false )
        {
            string condition;
            for ( const string& baseType : config._listValueForbiddenBaseType )
            {
                if ( condition.empty() == false )
                    condition += " || ";
                condition += "std::is_base_of_v<";
                condition += baseType;
                condition += ", std::remove_cv_t<std::remove_reference_t<PropDecl>>>";
            }
            emit.linef( "constexpr bool kIsInvalidValue = std::is_pointer_v<std::remove_cv_t<std::remove_reference_t<PropDecl>>> == false && (%#);",
                        condition );
            emit.linef( "static_assert(!kIsInvalidValue, \"%#\");", config._valueForbiddenMessage );
        }
        emit.line( "sw::PropertyInfo p(" );
        emit.push();
        emit.linef( "%#,", CodeEmit::hs( prop._name ) );
        emit.linef( "%#,", CodeEmit::hs( _session._typeNameMap.normalize( prop._typeName ) ) );
        // 비트필드는 `offsetof` 를 쓸 수 없다. 자리는 아래 `resolveBitField` 가 이 구성의 실제 레이아웃에서 찾는다.
        if ( prop._bIsBitField == SW_TRUE || prop._bIsAccessor == SW_TRUE )
            emit.line( "0u," );
        else
            emit.linef( "offsetof(%#, %#),", typeInfo._fullyQualifiedName, prop._memberName );

        if ( prop._bIsContainer )
        {
            const utf8*  kindStr     = toCppExpr( prop._containerKind );
            const string wrapperType = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._memberName );

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
        if ( prop._bIsAccessor == SW_TRUE )
        {
            // 값 자리는 메서드가 안다. 인자는 오프셋이 기준으로 삼는 객체 주소다(`PropertyInfo::getRawPtr`).
            emit.linef( "p._pValueAccessor = []( void* pInstance ) -> void* { return std::addressof( static_cast<%#*>( pInstance )->%#() ); };",
                        typeInfo._fullyQualifiedName, prop._memberName );
        }
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 파서가 잰 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 잰다(`PropertyInfo::resolveBitField` 설명).
            emit.linef( "p.resolveBitField( sizeof( %# ), []( void* pInstance ) { static_cast<%#*>( pInstance )->%# = static_cast<PropDecl>( 1 ); } );",
                        typeInfo._fullyQualifiedName, typeInfo._fullyQualifiedName, prop._memberName );
        }
        if ( prop._repNotify.empty() == false )
        {
            // 파서가 모양을 확인했다(`validateMemberFunctions`) — 이전 값을 받는 함수면 그 타입 그대로 넘긴다.
            if ( prop._bRepNotifyTakesOldValue == SW_TRUE )
                emit.linef( "p._pRepNotify = []( void* pInstance, const void* pOldValue ) { static_cast<%#*>( pInstance )->%#( *static_cast<const PropDecl*>( pOldValue ) ); };",
                            typeInfo._fullyQualifiedName, prop._repNotify );
            else
                emit.linef( "p._pRepNotify = []( void* pInstance, const void* ) { static_cast<%#*>( pInstance )->%#(); };", typeInfo._fullyQualifiedName,
                            prop._repNotify );
        }
        if ( prop._validate.empty() == false )
        {
            // 파서가 모양을 확인했다 — const 든 아니든 부를 수 있게 인스턴스를 그 타입으로 돌려 부른다.
            emit.linef( "p._pValidate = []( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<%#*>( const_cast<void*>( pInstance ) )->%#( context ); };",
                        typeInfo._fullyQualifiedName, prop._validate );
        }
        if ( prop._listAlias.empty() == false )
        {
            emit.line( "p._listAlias = {" );
            emit.push();
            for ( const string& alias : prop._listAlias )
            {
                emit.linef( "%#,", CodeEmit::hs( alias ) );
            }
            emit.pop();
            emit.line( "};" );
        }
        emitPropertyMetadata( emit, prop );
        emit.line( "return p;" );
        emit.pop();
        emit.line( "}()," );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& returnType,
                                           const string& callArgs ) const
    {
        emit.line( "auto invokerCb = []( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
        emit.line( "{" );
        emit.push();
        if ( method._listParameter.empty() )
            emit.line( "(void)args;" );

        if ( method._bStatic != SW_FALSE && method._bConstructor == SW_FALSE )
        {
            emit.line( "(void)objPtr;" );
            if ( returnType == annotation::kVoidTypeName )
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
                emit.linef( "sw_placement_new( self ) %#(%#);", typeInfo._fullyQualifiedName, callArgs ); // `Style/PlacementNew` 와 같은 모양
                emit.line( "return ::sw::TaskValue{};" );
            }
            else if ( returnType == annotation::kVoidTypeName )
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
            const string returnType = _session._typeNameMap.normalize( method._returnTypeName );

            const string lookupName = ( method._bConstructor != SW_FALSE ) ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;

            emit.line( "{" );
            emit.push();
            emit.line( "::sw::FunctionInfo funcInfo;" );
            emit.assign( "funcInfo._name", CodeEmit::quoted( ( method._bConstructor != SW_FALSE ) ? annotation::kCtorLookupName : method._name ) );
            emit.linef( "funcInfo._hashName       = %#;", CodeEmit::hs( lookupName ) );
            emit.assign( "funcInfo._returnTypeName", CodeEmit::quoted( returnType ) );
            if ( returnType != annotation::kVoidTypeName && method._bConstructor == SW_FALSE )
                emit.assign( "funcInfo._pReturnType", CodeGeneratorInternal::makeTypeOpsExpr( returnType ) );
            CodeGeneratorInternal::emitParameterList( emit, "funcInfo._listParameter", method._listParameter, _session, true );

            AnnotationFields::emitMetadata( emit, method, "funcInfo._metadata." );

            // 애노테이션이 아니라 선언에서 온 사실들이다.
            emit.flagIf( method._bConstructor != SW_FALSE, "funcInfo._metadata._bConstructor", "SW_TRUE" );
            emit.flagIf( method._bStatic != SW_FALSE, "funcInfo._metadata._bStatic", "SW_TRUE" );
            emit.flagIf( method._bConst != SW_FALSE, "funcInfo._metadata._bConst", "SW_TRUE" );

            const string callArgs = CodeGeneratorInternal::makeInvokerCallArgs( method._listParameter, _session );

            emitMethodInvoker( emit, typeInfo, method, returnType, callArgs );
            emit.pop();
            emit.line( "}" );
        }
    }

    void CodeGenerator::emitEventList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        emit.line( "info._listEvent =" );
        emit.line( "{" );
        emit.push();
        for ( const ParsedEventInfo& event : typeInfo._listEvent )
        {
            emit.line( "[]() {" );
            emit.push();
            emit.line( "::sw::EventInfo e;" );
            emit.linef( "e._name   = %#;", CodeEmit::hs( event._annotation._name ) );
            emit.linef( "e._offset = offsetof(%#, %#);", typeInfo._fullyQualifiedName, event._memberName );
            CodeGeneratorInternal::emitParameterList( emit, "e._listParameter", event._listParameter, _session, false );
            // 묶기 · 부르기 · 인자 변환 표는 필드 타입(`MulticastDelegate<void( Args... )>`)에서 템플릿이 만든다.
            emit.linef( "e.setOps( &::sw::ReflectEventOpsOf<decltype(%#::%#)>::kOps );", typeInfo._fullyQualifiedName, event._memberName );
            AnnotationFields::emitMetadata( emit, event._annotation, "e._metadata." );
            emit.line( "return e;" );
            emit.pop();
            emit.line( "}()," );
        }
        emit.pop();
        emit.line( "};" );
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
            // 컴포넌트 생성 함수는 타입 줄의 칸이다(언리얼 `UClass` 의 생성자 칸과 같은 자리). 이름으로 만드는 길은 이 칸만 본다.
            fe.flagIf( typeInfo.requiresComponentFactory(), "info._addComponent", "&::sw::GameObject::addComponentTo<" + typeInfo._fullyQualifiedName + ">" );
            if ( typeInfo._validate.empty() == false )
            {
                fe.linef( "info._pValidate = []( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<%#*>( const_cast<void*>( pInstance ) )->%#( context ); };",
                          typeInfo._fullyQualifiedName, typeInfo._validate );
            }
        }

        appendTemplate( out, templatefile::kTypeRegistrarBegin, {
                                                                    {        templatekey::kId,                registrarName},
                                                                    {       templatekey::kFqn, typeInfo._fullyQualifiedName},
                                                                    {      templatekey::kName,               typeInfo._name},
                                                                    { templatekey::kParentFqn,          typeInfo._parentFQN},
                                                                    {templatekey::kModuleName,                  _moduleName},
                                                                    {     templatekey::kFlags,    string( flagsBuf.view() )},
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
            {
                emitPropertyInfoEntry( emit, typeInfo, prop );
            }
            emit.pop();
            emit.line( "};" );
        }

        if ( typeInfo._listEvent.empty() == false )
            emitEventList( emit, typeInfo );

        if ( typeInfo._listMethod.empty() == false )
            emitMethodList( emit, typeInfo );

        appendTemplate( out, templatefile::kTypeRegistrarEnd,
                        {
                            { templatekey::kId, registrarName },
                            { templatekey::kFqn, typeInfo._fullyQualifiedName },
                            { templatekey::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( typeInfo._listAlias, typeInfo._fullyQualifiedName, false ) }
        } );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
    {
        const string registrarName = sanitizeIdentifier( enumInfo._fullyQualifiedName );

        const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = findEnumerator( enumInfo, enumInfo._countEnumerator );

        // 값은 숫자로 박지 않고 **컴파일러가 계산하게** 한다. libclang 의 값은 부호 있는 64 비트라 `uint8` 의 0x80 이 -128 로 적혔고,
        // 런타임은 그것을 128 로 읽어 이름을 잃었다(`EnumInfo::readValueFromMemory`). 열거자 자체를 넓히면 밑바탕 타입의 부호를 따른다.
        const auto valueExpr = [&enumInfo]( const ParsedEnumeratorInfo& enumerator )
        {
            return "static_cast<int64>( ::" + enumInfo._fullyQualifiedName + "::" + enumerator._name + " )";
        };

        appendTemplate( out, templatefile::kEnumRegistrarBegin, {
                                                                    {          templatekey::kId,                                                  registrarName},
                                                                    {         templatekey::kFqn,                                   enumInfo._fullyQualifiedName},
                                                                    {        templatekey::kName,                                                 enumInfo._name},
                                                                    {  templatekey::kModuleName,                                                    _moduleName},
                                                                    {   templatekey::kIsBitFlag,                        enumInfo._bIsBitFlag ? "true" : "false"},
                                                                    {  templatekey::kHasInvalid,                        invalidEn != nullptr ? "true" : "false"},
                                                                    {templatekey::kInvalidValue, invalidEn != nullptr ? valueExpr( *invalidEn ) : string( "0" )},
                                                                    {    templatekey::kHasCount,                          countEn != nullptr ? "true" : "false"},
                                                                    {  templatekey::kCountValue,     countEn != nullptr ? valueExpr( *countEn ) : string( "0" )},
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
            {
                emit.linef( "{ %#, %# },", CodeEmit::hs( en._name ), valueExpr( en ) );
            }
            emit.pop();
            emit.line( "};" );

            emit.line( "info._mapValueToName =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
            {
                emit.linef( "{ %#, %# },", valueExpr( en ), CodeEmit::hs( en._name ) );
            }
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

        appendTemplate( out, templatefile::kEnumRegistrarEnd,
                        {
                            { templatekey::kId, registrarName },
                            { templatekey::kFqn, enumInfo._fullyQualifiedName },
                            { templatekey::kAliasRegs,
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
            // 코드젠하지 않고, Core/Common/BitFlagTrait.h 의 제네릭 sw::IsBitFlagEnum<E> 트레이트와 EnumUtil.h 의 전역
            // 스코프 SFINAE 연산자로 통일한다. 로직이 모든 enum 에서 같아 타입별 코드젠이 필요
            // 없다. 여기서는 그 트레이트를 켜는(opt-in) 한 줄짜리 명시적 특수화만 생성한다.
            //
            // **열거형을 전방 선언한 뒤 특수화한다.** 원본 헤더를 include 하지 않는다. 이 파일을
            // 모으는 우산(`FlagOps.gen.h`)이 타깃 전 TU 에 `/FI` 로 들어가므로, 여기서 원본 헤더를
            // 들이면 그 헤더가 끌어오는 것 전부가 **모든 TU 에 이미 있는 이름**이 되어 다른 헤더들의
            // include 누락을 통째로 가린다. 불투명 열거형 선언은 완전한 타입이라 특수화에 이것으로 충분하다.
            // 같은 이유로 트레이트도 `<type_traits>` 만 드는 `BitFlagTrait.h` 에서 받는다 — `EnumUtil.h` 는 `Macros.h` 를 끌어온다.
            emit.line( "#include \"Core/Common/BitFlagTrait.h\"" );
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
                    // 여기서 코드젠을 실패시키면 중첩 열거형이 `ENUM( Flags )` 를 쓸 수 없다. `|` · `&` 를 쓰면 그 자리에서
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
