#include "pch.h"

#include "Programs/ReflectionParser/CodeGenerator.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/AnnotationFields.h"
#include "Programs/ReflectionParser/EmitTemplateStore.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserUtil.h"
#include "Programs/ReflectionParser/TypeNameMap.h"

namespace sw
{
    namespace
    {
        struct CodeGeneratorInternal
        {
            static constexpr int32 kMaxNestedContainerDepth = 3;

            /**
             * @brief 범위 경계 하나를 float32 글(`1.5f`)로 냅니다. 등록 단위의 허용 범위와 개발 전용 단위의 슬라이더 범위가 함께 쓴다.
             * @details 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가 **생성된 파일마다** 난다.
             *          경계는 float32 가 되돌아오는 자릿수(9)로 낸다. 6 자리로 자르면 `Max = 6.2831853` 이 6.283185f 가 되어 2π 를 담은 값이 범위 밖이 된다.
             */
            static string formatRangeLiteral( const float32 value )
            {
                utf8 arrBuffer[constant::buffer::kMax64]{};
                formatstring( arrBuffer, sizeof( arrBuffer ), "%.9g", static_cast<float64>( value ) );
                string literal( arrBuffer );
                if ( literal.find_first_of( ".eE" ) == string::npos )
                    literal += ".0";
                return literal + "f";
            }

            /** @brief 프로퍼티에 개발 전용 단위로 낼 에디터 메타데이터(표시 · 편집 필드, 슬라이더 범위)가 있으면 true 입니다. */
            static bool hasPropertyEditorMetadata( const ParsedPropertyInfo& prop )
            {
                return AnnotationFields::hasEditorMetadata( prop ) || prop._bHasUIMinRange != SW_FALSE || prop._bHasUIMaxRange != SW_FALSE;
            }

            /**
             * @brief `Min` · `Max` 글이 이 타입의 필드 하나(`_halfSize`, `-_halfSize`)이면 그 필드와 부호를 찾습니다.
             * @return 그 필드입니다. 숫자 · 성분 목록 · 이 타입에 없는 이름(바깥 타입의 필드)이면 nullptr 입니다. 그 꼴은 고정 배치형 코덱만 씁니다.
             */
            static const ParsedPropertyInfo* findOwnRangeField( const ParsedTypeInfo& typeInfo, const string& text, bool& outbNegate )
            {
                const string_view term = StringUtil::trim( text );
                if ( term.empty() || term.find( ',' ) != string_view::npos )
                    return nullptr;
                const bool        bNegate = term.front() == '-';
                const string_view name    = bNegate ? StringUtil::trim( term.substr( 1 ) ) : term;
                if ( name.empty() )
                    return nullptr;
                const utf8 first            = name.front();
                const bool bIdentifierStart = first == '_' || ( 'a' <= first && first <= 'z' ) || ( 'A' <= first && first <= 'Z' );
                if ( bIdentifierStart == false )
                    return nullptr;
                for ( const ParsedPropertyInfo& candidate : typeInfo._listProperty )
                {
                    if ( string_view( candidate._memberName ) != name )
                        continue;
                    outbNegate = bNegate;
                    return &candidate;
                }
                return nullptr;
            }

            /** @brief 필드 이름 꼴 경계를 인스턴스에서 읽는 함수(`PropertyMetadata::_pReadMinRange` 등)를 냅니다. 읽기 공통 끝도 지키므로 Shipping 에도 냅니다. */
            static void emitRangeReader( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const string& text, const utf8* pMemberName )
            {
                bool                      bNegate = false;
                const ParsedPropertyInfo* pField  = findOwnRangeField( typeInfo, text, bNegate );
                if ( pField == nullptr )
                    return;
                // 접근자 프로퍼티는 메서드를 부른다(`_pValueAccessor` 와 같은 규칙). 이름이 틀리거나 숫자가 아닌 필드면 생성된 코드가 컴파일되지 않는다.
                const utf8* pCall = pField->_bIsAccessor == SW_TRUE ? "()" : "";
                emit.linef( "m.%# = []( const void* pInstance ) -> ::float64 { return %#static_cast<::float64>( static_cast<const %#*>( pInstance )->%#%# ); };",
                            pMemberName, bNegate ? "-" : "", typeInfo._fullyQualifiedName, pField->_memberName, pCall );
            }

            /** @brief 이 타입이 선언한 프로퍼티 중 C++ 이름이 @p name 인 것입니다. 없으면(기반의 이름 포함) nullptr 입니다. */
            static const ParsedPropertyInfo* findOwnProperty( const ParsedTypeInfo& typeInfo, const string_view name )
            {
                for ( const ParsedPropertyInfo& candidate : typeInfo._listProperty )
                {
                    if ( string_view( candidate._memberName ) == name )
                        return &candidate;
                }
                return nullptr;
            }

            /** @brief 인스턴스 `o` 에서 그 이름의 값을 읽는 C++ 식입니다. 이 타입의 접근자 프로퍼티면 메서드를 부르고, 기반의 이름은 필드로 읽는다(틀리면 컴파일 오류). */
            static string makeMemberRead( const ParsedTypeInfo& typeInfo, const string_view name )
            {
                const ParsedPropertyInfo* pOwn  = findOwnProperty( typeInfo, name );
                const bool                bCall = pOwn != nullptr && pOwn->_bIsAccessor == SW_TRUE;
                return "o." + string( name ) + ( bCall ? "()" : "" );
            }

            /**
             * @brief `Default = "…"` 를 그 프로퍼티 타입의 C++ 리터럴로 바꿉니다 — 문자열은 따옴표, 열거는 `::열거형::이름`, 나머지는 적은 식 그대로.
             * @details 대입문으로 나가므로 오타 · 타입이 맞지 않는 기본값은 생성된 코드의 컴파일 오류다(글을 로드 때 읽다 경고하던 길을 없앤다).
             */
            static string makeDefaultLiteral( const ParsedPropertyInfo& prop )
            {
                const string_view    text = StringUtil::trim( prop._defaultValue );
                const FixedValueKind kind = prop._fixedType != nullptr ? prop._fixedType->_kind : FixedValueKind::Ineligible;
                if ( kind == FixedValueKind::String )
                    return CodeEmit::quoted( text );
                const bool bBareEnumerator = kind == FixedValueKind::Enum && prop._fixedType->_enum != nullptr && text.find( "::" ) == string_view::npos &&
                                             ( text.empty() == false && ( text.front() < '0' || text.front() > '9' ) && text.front() != '-' );
                if ( bBareEnumerator )
                    return "::" + prop._fixedType->_enum->_fullyQualifiedName + "::" + string( text );
                return string( text );
            }

            /**
             * @brief `EditCondition` 식(꼴은 `DeclarationValidator::isEditConditionWellFormed` 가 봤다)을 인스턴스 `o` 에 대한 C++ bool 식으로 바꿉니다.
             * @details 열거자는 그 칸의 타입으로 한정한다(`decltype`) — 없는 이름 · 없는 열거자는 생성된 코드의 컴파일 오류다.
             *          언리얼 `EditCondition` 은 글을 에디터가 런타임에 풀어 틀린 이름이 조용히 무시된다.
             */
            static string makeEditConditionExpr( const ParsedTypeInfo& typeInfo, const string_view rawText )
            {
                const string_view text  = StringUtil::trim( rawText );
                const size_t      eqPos = text.find( "==" );
                const size_t      nePos = text.find( "!=" );
                if ( eqPos == string_view::npos && nePos == string_view::npos )
                {
                    const bool        bNegate = text.front() == '!';
                    const string_view name    = StringUtil::trim( bNegate ? text.substr( 1 ) : text );
                    return string( bNegate ? "!" : "" ) + "static_cast<bool>( " + makeMemberRead( typeInfo, name ) + " )";
                }
                const size_t      opPos = eqPos != string_view::npos ? eqPos : nePos;
                const string      read  = makeMemberRead( typeInfo, StringUtil::trim( text.substr( 0, opPos ) ) );
                const string_view op    = text.substr( opPos, 2 );
                string_view       value = StringUtil::trim( text.substr( opPos + 2 ) );
                int64             number{ 0 };
                if ( StringUtil::parseInt64( value, number ) )
                    return "static_cast<::sw::int64>( " + read + " ) " + string( op ) + " " + string( value );
                // 한정해 적어도(`Mode::Orbit`) 끝 이름을 칸의 타입으로 다시 한정한다 — 적은 한정자가 다른 열거형이어도 칸과 맞춘다.
                value = ParserUtil::scopeLeaf( value );
                return read + " " + string( op ) + " ::std::decay_t<decltype( " + read + " )>::" + string( value );
            }

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
                StringBuilder<constant::buffer::kMax1024> b;
                b.appendFormat( "sw::%#Wrapper<decltype(std::declval<%#>().%#)>", containerType, fqn, fieldName );
                return string( b.view() );
            }

            /**
             * @brief 2중, 3중 중첩 컨테이너(예: vector<vector<int32>>)에 대한 중첩 래퍼 표기를 생성합니다.
             */
            static string makeNestedWrapperType( const string& containerType, int32 depth )
            {
                StringBuilder<constant::buffer::kMax128> b;
                b.appendFormat( "sw::%#Wrapper<NestC%#>", containerType, depth );
                return string( b.view() );
            }

            /**
             * @brief 생성자 검색을 위한 고유 식별자 문자열(예: `$ctor(int32,float32)`)을 구성합니다.
             */
            static string makeCtorLookupName( const ParsedFunctionInfo& method, const ParserSession& session )
            {
                StringBuilder<constant::buffer::kMax1024> b;
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
                StringBuilder<constant::buffer::kMax1024> b;
                b.appendFormat( "&::sw::ReflectTypeOpsOf<%#>::kOps", normalizedType );
                return string( b.view() );
            }

            /**
             * @brief 인자 서술자 표 `static constexpr ::sw::FunctionParameterDesc <tableName>[]` 를 씁니다 — 이름 · 정규 타입 · 기본 인자 글 · 변환 표 · 타입 핸들 대상.
             * @param bWithTypeOps 이벤트는 변환 표를 `EventInfo::setOps` 가 템플릿 인자에서 채우므로 nullptr 를 씁니다.
             * @return 표를 썼으면 그 이름, 인자가 없으면 "nullptr".
             */
            static string emitParameterTable( CodeEmit& emit, const string& tableName, const vector<ParsedParameterInfo>& listParameter, const ParserSession& session,
                                              const bool bWithTypeOps )
            {
                if ( listParameter.empty() )
                    return string( "nullptr" );
                emit.linef( "static constexpr ::sw::FunctionParameterDesc %#[] =", tableName );
                emit.line( "{" );
                emit.push();
                for ( const ParsedParameterInfo& parameter : listParameter )
                {
                    const string typeName = session._typeNameMap.normalize( parameter._typeName );
                    const string typeOps  = bWithTypeOps ? makeTypeOpsExpr( typeName ) : string( "nullptr" );
                    const string target   = parameter._handleTargetTypeName.empty() ? string( "nullptr" ) : CodeEmit::quoted( parameter._handleTargetTypeName );
                    emit.linef( "{ %#, %#, %#, %#, %# },", CodeEmit::quoted( parameter._name ), CodeEmit::quoted( typeName ), CodeEmit::quoted( parameter._defaultValue ), typeOps,
                                target );
                }
                emit.pop();
                emit.line( "};" );
                return tableName;
            }

            /**
             * @brief 런타임 동적 함수 호출(Invoker)을 위한 인자 추출 구문 `args.get<T>(0), args.get<T>(1)...`을 생성합니다.
             */
            static string makeInvokerCallArgs( const vector<ParsedParameterInfo>& listParameter, const ParserSession& session )
            {
                StringBuilder<constant::buffer::kMax1024> b;
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    if ( paramIndex > 0 )
                        b.append( ", " );
                    // 타입 핸들 인자는 썽크가 앞에서 확인해 만든 지역 변수(`emitMethodInvoker`)다.
                    if ( listParameter[paramIndex]._handleTargetTypeName.empty() == false )
                        b.appendFormat( "typedArg%#", static_cast<uint32>( paramIndex ) );
                    else
                        b.appendFormat( "args.get<%#>( %# )", session._typeNameMap.normalize( listParameter[paramIndex]._typeName ), static_cast<uint32>( paramIndex ) );
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

            /** @brief 지운 타입 이름을 등록하는 줄(`registry.registerRetiredTypeName`)을 만듭니다. */
            static string emitRetiredTypeLines( const vector<string>& listRemovedType )
            {
                if ( listRemovedType.empty() )
                    return {};
                CodeEmitBuffer buf;
                CodeEmit       emit( buf );
                emit.push( 3 );
                for ( const string& removedType : listRemovedType )
                {
                    emit.linef( "registry.registerRetiredTypeName( \"%#\" );", CodeEmit::escapeCppString( removedType ) );
                }
                return string( buf.view() );
            }

            /**
             * @brief 표식(`Invalid`, `Count`)을 뺀 열거자의 값이 `0..N-1` 로 빈틈없고 겹치지 않으면, 값 순서로 늘어놓은 열거자를 @p outListDense 에 담습니다.
             * @details 그 밖(비트 플래그, 음수, 빈틈, 같은 값의 별명)은 비워 둡니다. 그러면 이름 배열을 만들지 않고 런타임은 이름 맵을 씁니다.
             *          여기 값은 libclang 이 읽은 값이라, 배열을 쓰는 쪽이 열거자마다 `static_assert` 로 컴파일러의 값과 대조합니다.
             */
            static void collectDenseEnumerator( const ParsedEnumInfo& enumInfo, const ParsedEnumeratorInfo* pInvalid, const ParsedEnumeratorInfo* pCount,
                                                vector<const ParsedEnumeratorInfo*>& outListDense )
            {
                outListDense.clear();
                if ( enumInfo._bIsBitFlag == SW_TRUE )
                    return;
                for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                {
                    if ( &en != pInvalid && &en != pCount )
                        outListDense.push_back( &en );
                }
                const int64                         count = static_cast<int64>( outListDense.size() );
                vector<const ParsedEnumeratorInfo*> listByValue( outListDense.size(), nullptr );
                for ( const ParsedEnumeratorInfo* pEn : outListDense )
                {
                    const bool bInRange = 0 <= pEn->_value && pEn->_value < count;
                    if ( bInRange == false || listByValue[static_cast<size_t>( pEn->_value )] != nullptr )
                    {
                        outListDense.clear();
                        return;
                    }
                    listByValue[static_cast<size_t>( pEn->_value )] = pEn;
                }
                outListDense = std::move( listByValue );
            }
        };

        /**
         * @brief 열거형 하나를 불투명(opaque) 선언으로 앞세웁니다.
         * @details `namespace sw::editor { enum class EditorPanelFlags : unsigned char; }` 형태입니다.
         *          네임스페이스가 없으면(전역 열거형) 선언만 씁니다. 기반 타입은 원본 철자를 쓰므로
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
    CodeGenerator::CodeGenerator( const ParsedHeader& header, const FixedCodecGenerator& fixedCodec, const string& sourceFilePath,
                                  const ParserSession& session, const string& moduleName )
        : _header{ header }
        , _fixedCodec{ fixedCodec }
        , _session{ session }
        , _sourceFilePath{ sourceFilePath }
        , _moduleName{ moduleName }
    {
    }

    void CodeGenerator::appendTemplate( CodeEmitBuffer& outBuffer, const EmitTemplate kind, const EmitTemplateStore::TemplateVars vars ) const
    {
        outBuffer.append( _session._emitTemplateStore.render( kind, vars ) );
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
        emitPropertyValueCheck( buffer );
        // 타입 핸들 인자의 확인(`ComponentHandleArgument`)은 씬 매니저를 보므로, 그런 인자가 있는 산출물만 그 헤더를 넣는다(모든 .gen.cpp 가 매니저를 끌어오지 않는다).
        if ( hasHandleArgument() )
            buffer.append( "#include \"Engine/Object/GameObject/ComponentHandleArgument.h\"\n\n" );
        // 일괄 틱 등록도 같은 규칙이다 — 그런 타입이 있는 산출물만 레지스트리 헤더를 넣는다.
        const bool bTickBatch = VectorUtil::containsIf( _header._listType, []( const ParsedTypeInfo& typeInfo )
        { return typeInfo.requiresTickRegistration(); } );
        if ( bTickBatch )
            buffer.append( "#include \"Engine/Object/GameObject/Tick/TickBatchRegistry.h\"\n\n" );
        const bool bPhysicsHooks = VectorUtil::containsIf( _header._listType, []( const ParsedTypeInfo& typeInfo )
        { return typeInfo.requiresPhysicsHookRegistration(); } );
        if ( bPhysicsHooks )
            buffer.append( "#include \"Engine/Physics/Scene/PhysicsHookRegistry.h\"\n\n" );
        buffer.append( emitdirective::kGeneratedNsOpen );
        for ( size_t typeIndex = 0; typeIndex < _header._listType.size(); ++typeIndex )
        {
            emitTypeRegistrar( buffer, _header._listType[typeIndex], typeIndex );
            emitTickBatchRegistration( buffer, _header._listType[typeIndex] );
            emitPhysicsHookRegistration( buffer, _header._listType[typeIndex] );
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            emitEnumRegistrar( buffer, enumInfo );
        }
        buffer.append( emitdirective::kGeneratedNsClose );

        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            emitReflectTypeTraits( buffer, typeInfo );
            if ( typeInfo.requiresTypeAPI() )
                emitTypeInfoAccessors( buffer, typeInfo );
        }
        return string( buffer.view() );
    }

    void CodeGenerator::emitPropertyValueCheck( CodeEmitBuffer& outBuffer ) const
    {
        // 프로퍼티마다 긴 static_assert 를 내지 않고 검사 템플릿 하나를 둔다. 같은 단위에 생성 파일이 여럿 들어와도(묶음 TU) 한 번만 정의한다.
        const ParserConfig& config = _session._config;
        if ( config._listValueForbiddenBaseType.empty() )
            return;
        string condition;
        for ( const string& baseType : config._listValueForbiddenBaseType )
        {
            if ( condition.empty() == false )
                condition += " || ";
            condition += "std::is_base_of_v<" + baseType + ", TValue>";
        }
        CodeEmit emit( outBuffer );
        emit.line( "#ifndef SW_GENERATED_PROPERTY_VALUE_CHECK" );
        emit.line( "#define SW_GENERATED_PROPERTY_VALUE_CHECK" );
        emit.line( "namespace sw::generated" );
        emit.line( "{" );
        emit.push();
        emit.line( "template <typename TDecl>" );
        emit.line( "constexpr bool checkPropertyValue()" );
        emit.line( "{" );
        emit.push();
        emit.line( "using TValue = std::remove_cv_t<std::remove_reference_t<TDecl>>;" );
        emit.linef( "static_assert( std::is_pointer_v<TValue> || ( %# ) == false, \"%#\" );", condition, config._valueForbiddenMessage );
        emit.line( "return true;" );
        emit.pop();
        emit.line( "}" );
        emit.pop();
        emit.line( "} // namespace sw::generated" );
        emit.line( "#endif" );
        emit.blank();
    }

    bool CodeGenerator::hasHandleArgument() const
    {
        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
            {
                for ( const ParsedParameterInfo& parameter : method._listParameter )
                {
                    if ( parameter._handleTargetTypeName.empty() == false )
                        return true;
                }
            }
        }
        return false;
    }

    void CodeGenerator::emitFileHeader( CodeEmitBuffer& outBuffer ) const
    {
        appendTemplate( outBuffer, EmitTemplate::FileHeader, {
                                                                 {templatekey::kSourcePath,                   _sourceFilePath},
                                                                 {    templatekey::kBanner, sw::config::kTextReflectionBanner}
        } );
    }

    void CodeGenerator::emitReflectTypeTraits( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( outBuffer, EmitTemplate::ReflectTypeTraits, {
                                                                        { templatekey::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( outBuffer, EmitTemplate::TypeInfoAccessors, {
                                                                        { templatekey::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitPropertyMetadata( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const
    {
        AnnotationFields::emitRuntimeMetadata( emit, prop, "m." );

        // 범위는 경계마다 값과 "있음" 표시가 함께 가는 Manual 필드다. 적힌 쪽만 낸다 — 표시가 하나면 `Min` 만 적어도
        // 위 경계(기본 1)까지 나가 인스펙터가 그 값을 1 에서 막는다.
        if ( prop._bHasMinRange != SW_FALSE )
        {
            emit.linef( "m._minRange     = %#;", CodeGeneratorInternal::formatRangeLiteral( prop._minRange ).c_str() );
            emit.assign( "m._bHasMinRange", "SW_TRUE" );
        }
        if ( prop._bHasMaxRange != SW_FALSE )
        {
            emit.linef( "m._maxRange     = %#;", CodeGeneratorInternal::formatRangeLiteral( prop._maxRange ).c_str() );
            emit.assign( "m._bHasMaxRange", "SW_TRUE" );
        }
        // 상한은 코덱이 지키는 값 그대로다(상수 이름으로 적었으면 `PropertyResolver` 가 푼 값).
        if ( prop._bHasMaxCount != SW_FALSE )
            emit.linef( "m._maxCount     = %#u;", prop._maxCount );
        // 단위는 상수 문맥에서 철자를 열거로 바꾼다 — 표에서 빠진 철자는 이 줄이 컴파일 오류다(검증은 `applyDisplayMeta` 가 먼저 했다).
        if ( prop._units.empty() == false )
        {
            emit.line( "{" );
            emit.push();
            emit.linef( "constexpr ::sw::ReflectUnitKind kUnit = ::sw::ReflectUnit::getKindOf( %# );", CodeEmit::quoted( prop._units ) );
            emit.line( "static_assert( kUnit != ::sw::ReflectUnitKind::None, \"unknown PROPERTY( Units )\" );" );
            emit.line( "m._unit = kUnit;" );
            emit.pop();
            emit.line( "}" );
        }
        // 숫자가 아닌 경계 중 이 타입의 필드 하나를 가리키는 것은 인스턴스에서 읽는 함수로 낸다(인스펙터와 읽기 공통 끝이 그 값으로 막는다).
        CodeGeneratorInternal::emitRangeReader( emit, typeInfo, prop._minRangeExpr._text, "_pReadMinRange" );
        CodeGeneratorInternal::emitRangeReader( emit, typeInfo, prop._maxRangeExpr._text, "_pReadMaxRange" );
        // 기본값 · 편집 조건은 글이 아니라 컴파일된 대입 · 판정이다 — 이름 · 철자가 틀리면 이 줄이 컴파일되지 않는다.
        // 이 함수는 `Registrar<T>`(모든 REFLECT 타입의 친구) 안에서 나가므로 기반의 비공개 칸도 읽는다.
        if ( prop._defaultValue.empty() == false )
            emit.linef( "m._pApplyDefault = []( void* pInstance ) { static_cast<%#*>( pInstance )->%#%# = %#; };", typeInfo._fullyQualifiedName, prop._memberName,
                        prop._bIsAccessor == SW_TRUE ? "()" : "", CodeGeneratorInternal::makeDefaultLiteral( prop ) );
        if ( prop._editCondition.empty() == false )
            emit.linef( "m._pEditCondition = []( const void* pInstance ) -> bool { const %#& o = *static_cast<const %#*>( pInstance ); return %#; };",
                        typeInfo._fullyQualifiedName, typeInfo._fullyQualifiedName, CodeGeneratorInternal::makeEditConditionExpr( typeInfo, prop._editCondition ) );
        // 슬라이더 범위는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`emitPropertyEditorMetadata`).
    }

    void CodeGenerator::emitPropertyEditorMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop, const utf8* pAddFunction ) const
    {
        emit.line( "{" );
        emit.push();
        emit.linef( "::sw::PropertyEditorMetadata& p = set.%#( %# );", pAddFunction, CodeEmit::hs( prop._name ) );
        AnnotationFields::emitEditorMetadata( emit, prop, "p." );
        if ( prop._bHasUIMinRange != SW_FALSE )
        {
            emit.linef( "p._uiMinRange     = %#;", CodeGeneratorInternal::formatRangeLiteral( prop._uiMinRange ).c_str() );
            emit.assign( "p._bHasUIMinRange", "SW_TRUE" );
        }
        if ( prop._bHasUIMaxRange != SW_FALSE )
        {
            emit.linef( "p._uiMaxRange     = %#;", CodeGeneratorInternal::formatRangeLiteral( prop._uiMaxRange ).c_str() );
            emit.assign( "p._bHasUIMaxRange", "SW_TRUE" );
        }
        emit.pop();
        emit.line( "}" );
    }

    void CodeGenerator::emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
            return;

        // 단계마다 정적 `const` 객체 하나다 — 안쪽이 먼저 있어야 바깥이 그 주소를 든다(아래에서 거꾸로 낸다).
        struct Level
        {
            string _kind;
            string _typeName;
            string _elementTypeName;
            string _keyTypeName;
            string _wrapperType;
        };
        vector<Level> listLevel;
        listLevel.push_back( Level{ toCppExpr( prop._containerKind ), prop._containerTree->_typeName, _session._typeNameMap.normalize( prop._elementTypeName ),
                                    _session._typeNameMap.normalize( prop._keyTypeName ),
                                    CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._memberName ) } );

        const ParsedContainerNode* node     = prop._containerTree->_elementNested.get();
        ContainerKind              prevKind = prop._containerKind;
        int32                      depth    = 1;
        if ( node != nullptr && node->_bIsContainer )
            emit.linef( "using NestC%#_0 = decltype( std::declval<%#>().%# );", propIndex, typeInfo._fullyQualifiedName, prop._memberName );
        while ( node != nullptr && node->_bIsContainer && depth < CodeGeneratorInternal::kMaxNestedContainerDepth )
        {
            emit.linef( "using NestC%#_%# = typename NestC%#_%#::%#;", propIndex, depth, propIndex, depth - 1, containerElementTypeMember( prevKind ) );
            StringBuilder<constant::buffer::kMax128> wrapperType;
            wrapperType.appendFormat( "sw::%#Wrapper<NestC%#_%#>", node->_containerType, propIndex, depth );
            // 안쪽 단계의 키 이름은 정규화하지 않는다(예전 출력과 같다).
            listLevel.push_back( Level{ toCppExpr( node->_containerKind ), node->_typeName, _session._typeNameMap.normalize( node->_elementTypeName ), node->_keyTypeName,
                                        string( wrapperType.view() ) } );
            prevKind = node->_containerKind;
            node     = node->_elementNested.get();
            ++depth;
        }

        for ( size_t level = listLevel.size(); level-- > 0; )
        {
            const Level&                             entry = listLevel[level];
            StringBuilder<constant::buffer::kMax128> next;
            if ( level + 1 < listLevel.size() )
                next.appendFormat( "&s_nested%#_%#", propIndex, static_cast<uint32>( level + 1 ) );
            else
                next.append( "nullptr" );
            emit.linef( "static const ::sw::NestedContainerInfo s_nested%#_%#{ %#, %#, %#, %#, &::sw::ContainerWrapperOf<%#>::kInstance, %# };", propIndex,
                        static_cast<uint32>( level ), entry._kind, CodeEmit::hs( entry._typeName ), CodeEmit::hs( entry._elementTypeName ),
                        CodeEmit::hs( entry._keyTypeName ), entry._wrapperType, next.view() );
        }
    }

    string CodeGenerator::makePropertyDeclType( const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop )
    {
        // 접근자 프로퍼티(값이 객체 밖)는 필드가 없다. 선언 타입은 메서드가 돌려주는 참조의 대상이다.
        if ( prop._bIsAccessor == SW_TRUE )
            return "std::remove_reference_t<decltype( std::declval<" + typeInfo._fullyQualifiedName + "&>()." + prop._memberName + "() )>";
        return "decltype( " + typeInfo._fullyQualifiedName + "::" + prop._memberName + " )";
    }

    void CodeGenerator::emitPropertyStatics( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        // PROPERTY() 에 값으로 담으면 안 되는 기반 타입은 파일 머리의 검사 템플릿(`emitPropertyValueCheck`)이 막는다.
        if ( _session._config._listValueForbiddenBaseType.empty() == false )
            emit.linef( "static_assert( ::sw::generated::checkPropertyValue<%#>() );", makePropertyDeclType( typeInfo, prop ) );
        if ( prop._listAlias.empty() == false )
        {
            string aliases;
            for ( const string& alias : prop._listAlias )
            {
                if ( aliases.empty() == false )
                    aliases += ", ";
                aliases += CodeEmit::quoted( alias );
            }
            emit.linef( "static constexpr const ::utf8* kAlias%#[] = { %# };", propIndex, aliases );
        }
        emitNestedContainerTree( emit, typeInfo, prop, propIndex );
    }

    void CodeGenerator::emitPropertyDesc( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        // 칸 차례는 `PropertyDesc`(엔진 `PropertyInfo.h`)와 같다.
        const string& fqn        = typeInfo._fullyQualifiedName;
        const auto    textOrNull = []( const string& text )
        { return text.empty() ? string( "nullptr" ) : CodeEmit::quoted( text ); };
        const bool   bNoOffset    = prop._bIsBitField == SW_TRUE || prop._bIsAccessor == SW_TRUE;
        const string offsetExpr   = bNoOffset ? string( "0u" ) : "offsetof( " + fqn + ", " + prop._memberName + " )";
        const bool   bNestedShape = prop._containerTree != nullptr && prop._containerTree->_bIsContainer != SW_FALSE;
        const string declType     = makePropertyDeclType( typeInfo, prop );

        emit.line( "{" );
        emit.push();
        // 비트필드는 `offsetof` 를 쓸 수 없다. 자리는 `resolveBitField` 가 이 구성의 실제 레이아웃에서 찾는다(아래 `_pSetBit`).
        emit.linef( "%#, %#, %#,", CodeEmit::quoted( prop._name ), CodeEmit::quoted( _session._typeNameMap.normalize( prop._typeName ) ), offsetExpr );
        if ( prop._bIsContainer )
        {
            emit.linef( "%#, %#, %#,", toCppExpr( prop._containerKind ), textOrNull( _session._typeNameMap.normalize( prop._elementTypeName ) ),
                        textOrNull( _session._typeNameMap.normalize( prop._keyTypeName ) ) );
            StringBuilder<constant::buffer::kMax64> nested;
            if ( bNestedShape )
                nested.appendFormat( "&s_nested%#_0", propIndex );
            else
                nested.append( "nullptr" );
            emit.linef( "&::sw::ContainerWrapperOf<%#>::kInstance, %#,", CodeGeneratorInternal::makeWrapperType( prop._containerType, fqn, prop._memberName ),
                        nested.view() );
        }
        else
        {
            emit.line( "::sw::ContainerKind::None, nullptr, nullptr, nullptr, nullptr," );
        }
        StringBuilder<constant::buffer::kMax64> alias;
        if ( prop._listAlias.empty() )
            alias.append( "nullptr" );
        else
            alias.appendFormat( "kAlias%#", propIndex );
        emit.linef( "%#, %#, %#u,", textOrNull( prop._handleTargetTypeName ), alias.view(), static_cast<uint32>( prop._listAlias.size() ) );
        // 값 자리는 메서드가 안다. 인자는 오프셋이 기준으로 삼는 객체 주소다(`PropertyInfo::getRawPtr`).
        if ( prop._bIsAccessor == SW_TRUE )
            emit.linef( "[]( void* pInstance ) -> void* { return std::addressof( static_cast<%#*>( pInstance )->%#() ); },", fqn, prop._memberName );
        else
            emit.line( "nullptr," );
        // RepNotify · Validate 는 파서가 모양을 확인했다(`validateMemberFunctions`) — 이전 값을 받는 함수면 그 타입 그대로 넘긴다.
        if ( prop._repNotify.empty() )
            emit.line( "nullptr," );
        else if ( prop._bRepNotifyTakesOldValue == SW_TRUE )
            emit.linef( "[]( void* pInstance, const void* pOldValue ) { static_cast<%#*>( pInstance )->%#( *static_cast<const %#*>( pOldValue ) ); },", fqn,
                        prop._repNotify, declType );
        else
            emit.linef( "[]( void* pInstance, const void* ) { static_cast<%#*>( pInstance )->%#(); },", fqn, prop._repNotify );
        // const 든 아니든 부를 수 있게 인스턴스를 그 타입으로 돌려 부른다.
        if ( prop._validate.empty() )
            emit.line( "nullptr," );
        else
            emit.linef( "[]( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<%#*>( const_cast<void*>( pInstance ) )->%#( context ); },", fqn,
                        prop._validate );
        // 파서가 측정한 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 측정한다(`PropertyInfo::resolveBitField` 설명).
        if ( prop._bIsBitField == SW_TRUE )
            emit.linef( "[]( void* pInstance ) { static_cast<%#*>( pInstance )->%# = static_cast<%#>( 1 ); }, sizeof( %# ),", fqn, prop._memberName, declType, fqn );
        else
            emit.line( "nullptr, 0u," );

        // 적은 메타데이터가 없으면 채우기 함수도 없다 — 그 프로퍼티는 공유 기본값을 쓴다(힙 없음, `ReflectMetadataBox`).
        CodeEmitBuffer fillBuffer;
        CodeEmit       fillEmit( fillBuffer );
        fillEmit.push( emit.getIndent() + 1 );
        emitPropertyMetadata( fillEmit, typeInfo, prop );
        if ( fillBuffer.view().empty() )
        {
            emit.line( "nullptr" );
        }
        else
        {
            emit.line( "[]( ::sw::PropertyMetadata& m ) {" );
            emit.raw( fillBuffer.view() );
            emit.line( "}" );
        }
        emit.pop();
        emit.line( "}," );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& returnType,
                                           const string& callArgs ) const
    {
        // 표 칸에 들어가는 상태 없는 람다다 — 함수 포인터(`FunctionDesc::InvokeFunction`)로 바뀐다.
        emit.line( "[]( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
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
            // 타입 핸들 인자는 이 컴포넌트의 씬에서 대상 타입을 확인해 만든다. 틀리면 확인 쪽이 오류를 남기고 메서드를 부르지 않는다(파서가 컴포넌트의 비정적 메서드만 받는다).
            for ( size_t paramIndex = 0; paramIndex < method._listParameter.size(); ++paramIndex )
            {
                const ParsedParameterInfo& parameter = method._listParameter[paramIndex];
                if ( parameter._handleTargetTypeName.empty() )
                    continue;
                emit.linef( "::%# typedArg%#{};", parameter._canonicalTypeName, static_cast<uint32>( paramIndex ) );
                emit.linef( "if ( ::sw::ComponentHandleArgument::cast( *self, args.get<::sw::ComponentHandle>( %# ), typedArg%#, %# ) == false )", static_cast<uint32>( paramIndex ),
                            static_cast<uint32>( paramIndex ), CodeEmit::quoted( typeInfo._fullyQualifiedName + "::" + method._name ) );
                emit.line( "    return ::sw::TaskValue{};" );
            }
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
        emit.line( "}," );
    }

    void CodeGenerator::emitMethodList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        // 메서드는 상수 초기화되는 서술자 표 하나다(프로퍼티의 `kProperties` 와 같은 모양). 인자 표를 먼저 쓴다.
        vector<string> listParamTable;
        for ( size_t methodIndex = 0; methodIndex < typeInfo._listMethod.size(); ++methodIndex )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kMethodParams%#", static_cast<uint32>( methodIndex ) );
            listParamTable.push_back( CodeGeneratorInternal::emitParameterTable( emit, string( tableName.view() ), typeInfo._listMethod[methodIndex]._listParameter, _session, true ) );
        }
        emit.line( "static constexpr ::sw::FunctionDesc kMethods[] =" );
        emit.line( "{" );
        emit.push();
        for ( size_t methodIndex = 0; methodIndex < typeInfo._listMethod.size(); ++methodIndex )
        {
            const ParsedFunctionInfo& method       = typeInfo._listMethod[methodIndex];
            const bool                bConstructor = method._bConstructor != SW_FALSE;
            const string              returnType   = _session._typeNameMap.normalize( method._returnTypeName );
            const string              lookupName   = bConstructor ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;
            const string              returnOps    = ( returnType != annotation::kVoidTypeName && bConstructor == false ) ? CodeGeneratorInternal::makeTypeOpsExpr( returnType ) : string( "nullptr" );

            emit.line( "{" );
            emit.push();
            emit.linef( "%#, %#, %#, %#,", CodeEmit::quoted( bConstructor ? string( annotation::kCtorLookupName ) : method._name ), CodeEmit::quoted( lookupName ),
                        CodeEmit::quoted( returnType ), returnOps );
            emit.linef( "%#, %#u,", listParamTable[methodIndex], static_cast<uint32>( method._listParameter.size() ) );
            // 파서가 더한 기본 생성자는 그 타입이 기본 생성 가능할 때만 등록한다(참조 멤버 · 기본 생성자 없는 베이스는 가린다 — 호출 함수가 nullptr 면 등록에서 빠진다).
            if ( method._bSynthesized != SW_FALSE )
                emit.linef( "::sw::ReflectDefaultCtorOf<%#>::kInvoke,", typeInfo._fullyQualifiedName );
            else
                emitMethodInvoker( emit, typeInfo, method, returnType, CodeGeneratorInternal::makeInvokerCallArgs( method._listParameter, _session ) );

            // 적은 메타데이터와 선언 사실(생성자 · 정적 · const)이 없으면 채우기 함수도 없다 — 공유 기본값을 쓴다.
            CodeEmitBuffer fillBuffer;
            CodeEmit       fillEmit( fillBuffer );
            fillEmit.push( emit.getIndent() + 1 );
            AnnotationFields::emitRuntimeMetadata( fillEmit, method, "m." );
            fillEmit.flagIf( bConstructor, "m._bConstructor", "SW_TRUE" );
            fillEmit.flagIf( method._bStatic != SW_FALSE, "m._bStatic", "SW_TRUE" );
            fillEmit.flagIf( method._bConst != SW_FALSE, "m._bConst", "SW_TRUE" );
            if ( fillBuffer.view().empty() )
            {
                emit.line( "nullptr" );
            }
            else
            {
                emit.line( "[]( ::sw::FunctionMetadata& m ) {" );
                emit.raw( fillBuffer.view() );
                emit.line( "}" );
            }
            emit.pop();
            emit.line( "}," );
        }
        emit.pop();
        emit.line( "};" );
        emit.line( "info.setMethodTable( kMethods, ::std::size( kMethods ) );" );
    }

    void CodeGenerator::emitEventList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        vector<string> listParamTable;
        for ( size_t eventIndex = 0; eventIndex < typeInfo._listEvent.size(); ++eventIndex )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kEventParams%#", static_cast<uint32>( eventIndex ) );
            listParamTable.push_back( CodeGeneratorInternal::emitParameterTable( emit, string( tableName.view() ), typeInfo._listEvent[eventIndex]._listParameter, _session, false ) );
        }
        emit.line( "static constexpr ::sw::EventDesc kEvents[] =" );
        emit.line( "{" );
        emit.push();
        for ( size_t eventIndex = 0; eventIndex < typeInfo._listEvent.size(); ++eventIndex )
        {
            const ParsedEventInfo& event = typeInfo._listEvent[eventIndex];
            emit.line( "{" );
            emit.push();
            emit.linef( "%#, offsetof( %#, %# ),", CodeEmit::quoted( event._annotation._name ), typeInfo._fullyQualifiedName, event._memberName );
            emit.linef( "%#, %#u,", listParamTable[eventIndex], static_cast<uint32>( event._listParameter.size() ) );
            // 묶기 · 부르기 · 인자 변환 표는 필드 타입(`MulticastDelegate<void( Args... )>`)에서 템플릿이 만든다.
            emit.linef( "&::sw::ReflectEventOpsOf<decltype( %#::%# )>::kOps,", typeInfo._fullyQualifiedName, event._memberName );
            CodeEmitBuffer fillBuffer;
            CodeEmit       fillEmit( fillBuffer );
            fillEmit.push( emit.getIndent() + 1 );
            AnnotationFields::emitRuntimeMetadata( fillEmit, event._annotation, "m." );
            if ( fillBuffer.view().empty() )
            {
                emit.line( "nullptr" );
            }
            else
            {
                emit.line( "[]( ::sw::PropertyMetadata& m ) {" );
                emit.raw( fillBuffer.view() );
                emit.line( "}" );
            }
            emit.pop();
            emit.line( "}," );
        }
        emit.pop();
        emit.line( "};" );
        emit.line( "info.setEventTable( kEvents, ::std::size( kEvents ) );" );
    }

    void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo, const size_t typeIndex ) const
    {
        const string registrarName = sanitizeIdentifier( typeInfo._fullyQualifiedName );

        CodeEmitBuffer flagsBuf;
        {
            CodeEmit fe( flagsBuf );
            fe.push( 3 );
            fe.flagIf( typeInfo._bAbstract, "info._bAbstract" );
            fe.flagIf( typeInfo._bStatic, "info._bStatic" );
            // 컴포넌트 생성 함수는 타입 줄의 칸이다(언리얼 `UClass` 의 생성자 칸과 같은 자리). 이름으로 만드는 길은 이 칸만 본다.
            fe.flagIf( typeInfo.requiresComponentFactory(), "info._addComponent", "&::sw::GameObject::addComponentForReflection<" + typeInfo._fullyQualifiedName + ">" );
            if ( typeInfo._validate.empty() == false )
            {
                fe.linef( "info._pValidate = []( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<%#*>( const_cast<void*>( pInstance ) )->%#( context ); };",
                          typeInfo._fullyQualifiedName, typeInfo._validate );
            }
        }

        appendTemplate( outBuffer, EmitTemplate::TypeRegistrarBegin, {
                                                                         {        templatekey::kID,                registrarName},
                                                                         {       templatekey::kFqn, typeInfo._fullyQualifiedName},
                                                                         {      templatekey::kName,               typeInfo._name},
                                                                         { templatekey::kParentFqn,          typeInfo._parentFQN},
                                                                         {templatekey::kModuleName,                  _moduleName},
                                                                         {     templatekey::kFlags,    string( flagsBuf.view() )},
        } );

        CodeEmit emit( outBuffer );
        emit.push( 3 );

        AnnotationFields::emitRuntimeMetadata( emit, typeInfo, "info._metadata.edit()." );
        for ( const string& removedField : typeInfo._listRemovedField )
        {
            emit.linef( "info._listRemovedField.push_back( ::sw::hashed_string( \"%#\" ) );", CodeEmit::escapeCppString( removedField ) );
        }
        emit.flagIf( typeInfo._bOverridesTick, "info._metadata.edit()._bOverridesTick" );
        emit.flagIf( typeInfo._bOverridesSubTick, "info._metadata.edit()._bOverridesSubTick" );
        emit.flagIf( typeInfo._bOverridesSerializeState, "info._metadata.edit()._bOverridesSerializeState" );

        if ( typeInfo._listProperty.empty() == false )
        {
            // 프로퍼티는 상수 초기화되는 서술자 표 하나다(언리얼 UHT `PropPointers`). 등록은 표를 한 번 돌아 목록을 짓는다(`TypeInfo::setPropertyTable`).
            for ( size_t propIndex = 0; propIndex < typeInfo._listProperty.size(); ++propIndex )
            {
                emitPropertyStatics( emit, typeInfo, typeInfo._listProperty[propIndex], static_cast<uint32>( propIndex ) );
            }
            emit.line( "static constexpr ::sw::PropertyDesc kProperties[] =" );
            emit.line( "{" );
            emit.push();
            for ( size_t propIndex = 0; propIndex < typeInfo._listProperty.size(); ++propIndex )
            {
                emitPropertyDesc( emit, typeInfo, typeInfo._listProperty[propIndex], static_cast<uint32>( propIndex ) );
            }
            emit.pop();
            emit.line( "};" );
            emit.line( "info.setPropertyTable( kProperties, ::std::size( kProperties ) );" );
        }

        if ( typeInfo._listEvent.empty() == false )
            emitEventList( emit, typeInfo );

        if ( typeInfo._listMethod.empty() == false )
            emitMethodList( emit, typeInfo );

        _fixedCodec.emitRegistration( emit, typeIndex );

        appendTemplate( outBuffer, EmitTemplate::TypeRegistrarEnd,
                        {
                            { templatekey::kID, registrarName },
                            { templatekey::kFqn, typeInfo._fullyQualifiedName },
                            { templatekey::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( typeInfo._listAlias, typeInfo._fullyQualifiedName, false ) +
                                  CodeGeneratorInternal::emitRetiredTypeLines( typeInfo._listRemovedType ) },
                            { templatekey::kFixedCodec, _fixedCodec.getMemberText( typeIndex ) },
        } );
    }

    void CodeGenerator::emitTickBatchRegistration( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        // 등록 줄 모양은 틱 런타임(5-3)이 정한 `SW_REGISTER_TICK_TYPE( 타입, 모드 )` 하나다 — 손 등록과 같은 줄. 메타데이터가 없으면 Batch.
        if ( typeInfo.requiresTickRegistration() == false )
            return;
        const string mode = typeInfo._tickMode.empty() ? string( "Batch" ) : typeInfo._tickMode;
        CodeEmit     emit( outBuffer );
        emit.push( 1 );
        // 생성 이름공간(`sw::generated`)은 `sw` 안이라 `sw` 바로 아래 타입은 짧은 이름으로 닿는다. 다른 이름공간의 타입은 매크로가 이름을 붙여 쓰지 못해(토큰 붙이기) 같은 줄을 펼쳐 쓴다.
        const string_view qualifiedName = typeInfo._fullyQualifiedName;
        const string_view swPrefix      = "sw::";
        if ( StringUtil::startsWith( qualifiedName, swPrefix ) && qualifiedName.substr( swPrefix.size() ) == string_view( typeInfo._name ) )
        {
            emit.linef( "SW_REGISTER_TICK_TYPE( %#, %# );", typeInfo._name, mode );
        }
        else
        {
            const string id = sanitizeIdentifier( typeInfo._fullyQualifiedName );
            emit.linef( "static constexpr utf8 kTickBatchScopeName_%#[] = \"GT.Scene.tickBatch.%#\";", id, typeInfo._name );
            emit.linef( "static ::sw::TickBatchRegistrar s_%#_tickBatch{ ::sw::TickBatchRegistry::Entry{ &%#::StaticType, &::sw::TickBatchOf<%#, kTickBatchScopeName_%#>::tickRange, ::sw::TickMode::%# } };",
                        id, typeInfo._fullyQualifiedName, typeInfo._fullyQualifiedName, id, mode );
        }
        emit.line( "" );
    }

    void CodeGenerator::emitPhysicsHookRegistration( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        // 등록 줄 모양은 받는 쪽(`PhysicsHookRegistry.h`)이 정한 `SW_REGISTER_PHYSICS_HOOKS( 타입 )` 하나다. 재정의 여부는 그 줄이 컴파일에서 정한다.
        if ( typeInfo.requiresPhysicsHookRegistration() == false )
            return;
        CodeEmit emit( outBuffer );
        emit.push( 1 );
        // 틱 등록과 같은 규칙 — `sw` 바로 아래 타입은 짧은 이름, 다른 이름공간의 타입은 매크로가 이름을 붙여 쓰지 못해 같은 줄을 펼쳐 쓴다.
        const string_view qualifiedName = typeInfo._fullyQualifiedName;
        const string_view swPrefix      = "sw::";
        if ( StringUtil::startsWith( qualifiedName, swPrefix ) && qualifiedName.substr( swPrefix.size() ) == string_view( typeInfo._name ) )
            emit.linef( "SW_REGISTER_PHYSICS_HOOKS( %# );", typeInfo._name );
        else
            emit.linef( "static ::sw::PhysicsHookRegistrar s_%#_physicsHooks{ ::sw::PhysicsHooksOf<%#>::makeEntry() };", sanitizeIdentifier( typeInfo._fullyQualifiedName ),
                        typeInfo._fullyQualifiedName );
        emit.line( "" );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& outBuffer, const ParsedEnumInfo& enumInfo ) const
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

        appendTemplate( outBuffer, EmitTemplate::EnumRegistrarBegin, {
                                                                         {          templatekey::kID,                                                  registrarName},
                                                                         {         templatekey::kFqn,                                   enumInfo._fullyQualifiedName},
                                                                         {        templatekey::kName,                                                 enumInfo._name},
                                                                         {  templatekey::kModuleName,                                                    _moduleName},
                                                                         {   templatekey::kIsBitFlag,                        enumInfo._bIsBitFlag ? "true" : "false"},
                                                                         {  templatekey::kHasInvalid,                        invalidEn != nullptr ? "true" : "false"},
                                                                         {templatekey::kInvalidValue, invalidEn != nullptr ? valueExpr( *invalidEn ) : string( "0" )},
                                                                         {    templatekey::kHasCount,                          countEn != nullptr ? "true" : "false"},
                                                                         {  templatekey::kCountValue,     countEn != nullptr ? valueExpr( *countEn ) : string( "0" )},
        } );

        CodeEmit emit( outBuffer );
        emit.push( 3 );

        // 열거형의 커스텀 메타는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`makeEditorMetadataText`).
        AnnotationFields::emitRuntimeMetadata( emit, enumInfo, "info." );

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

        // 값이 곧 인덱스인 이름 목록이다. `getEnumName` 이 해시 맵 대신 인덱스로 찾는다. 순서는 libclang 의 값이므로 컴파일러의 값과 열거자마다 대조한다
        // (생성기는 SW_DEBUG 없이 헤더를 읽으므로, 빌드 설정에 따라 열거자가 달라지면 여기서 컴파일이 멈춘다).
        // 목록은 `EnumInfo` 가 값으로 가진다. 모듈의 정적 배열을 가리키면 핫 리로드 해제 뒤 다른 스레드의 이름 조회가 해제된 저장소를 읽는다.
        vector<const ParsedEnumeratorInfo*> listDense;
        CodeGeneratorInternal::collectDenseEnumerator( enumInfo, invalidEn, countEn, listDense );
        if ( listDense.empty() == false )
        {
            emit.line( "info._listDenseName =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo* pEn : listDense )
            {
                emit.linef( "%#,", CodeEmit::hs( pEn->_name ) );
            }
            emit.pop();
            emit.line( "};" );
            for ( size_t index = 0; index < listDense.size(); ++index )
            {
                emit.linef( "static_assert( %# == %#, \"enumerator value differs from the one the reflection parser read\" );", valueExpr( *listDense[index] ),
                            static_cast<uint32>( index ) );
            }
        }

        // 옛 열거자 이름(이름 변경 · 지운 열거자의 대체)은 지금 이름과 따로 둔다. 읽기는 지금 이름에 없을 때만 이 표를 본다. 대상은 `AstVisitor` 가 이미 확인했다.
        const bool bHasRedirect = enumInfo._listValueAlias.empty() == false || enumInfo._listRemovedEnumerator.empty() == false;
        if ( bHasRedirect )
        {
            emit.line( "info._mapRedirectNameToValue =" );
            emit.line( "{" );
            emit.push();
            for ( const vector<pair<string, string>>* pListRedirect : { &enumInfo._listValueAlias, &enumInfo._listRemovedEnumerator } )
            {
                for ( const auto& [oldName, targetName] : *pListRedirect )
                {
                    const ParsedEnumeratorInfo* pTarget = findEnumerator( enumInfo, targetName );
                    emit.linef( "{ %#, %# },", CodeEmit::hs( oldName ), valueExpr( *pTarget ) );
                }
            }
            emit.pop();
            emit.line( "};" );
        }

        appendTemplate( outBuffer, EmitTemplate::EnumRegistrarEnd,
                        {
                            { templatekey::kID, registrarName },
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

    string CodeGenerator::makeEditorMetadataText() const
    {
        CodeEmitBuffer buffer;
        appendTemplate( buffer, EmitTemplate::EditorMetadataFileHeader, {
                                                                            {templatekey::kSourcePath,                   _sourceFilePath},
                                                                            {    templatekey::kBanner, sw::config::kTextReflectionBanner}
        } );
        CodeEmit emit( buffer );
        bool     bAny = false;
        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            bAny = emitTypeEditorMetadata( emit, typeInfo ) || bAny;
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            if ( AnnotationFields::hasEditorMetadata( enumInfo ) == false )
                continue;
            const string id = sanitizeIdentifier( enumInfo._fullyQualifiedName );
            emit.linef( "static void fillEnum_%#( ::sw::EnumEditorMetadata& meta )", id );
            emit.line( "{" );
            emit.push();
            AnnotationFields::emitEditorMetadata( emit, enumInfo, "meta." );
            emit.pop();
            emit.line( "}" );
            emit.linef( "static const ::sw::EnumEditorMetadataSource    s_%#_editorMetadata{ %#, &fillEnum_%# };", id, CodeEmit::quoted( enumInfo._fullyQualifiedName ), id );
            emit.linef( "static const ::sw::EnumEditorMetadataRegistrar s_%#_editorMetadataRegistrar{ &s_%#_editorMetadata };", id, id );
            emit.blank();
            bAny = true;
        }
        if ( bAny == false )
            emit.linef( "// No editor metadata in %#", _sourceFilePath );
        buffer.append( "} // namespace sw::generated::editormetadata\n" );
        return string( buffer.view() );
    }

    bool CodeGenerator::emitTypeEditorMetadata( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        bool bAny = AnnotationFields::hasEditorMetadata( typeInfo );
        for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
        {
            bAny = bAny || CodeGeneratorInternal::hasPropertyEditorMetadata( prop );
        }
        for ( const ParsedEventInfo& event : typeInfo._listEvent )
        {
            bAny = bAny || AnnotationFields::hasEditorMetadata( event._annotation );
        }
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            bAny = bAny || AnnotationFields::hasEditorMetadata( method );
        }
        if ( bAny == false )
            return false;

        // 열쇠는 등록 단위와 같다 — 프로퍼티 · 이벤트는 이름, 메서드는 찾는 이름(`FunctionInfo::_hashName`, 생성자는 `makeCtorLookupName`).
        const string id = sanitizeIdentifier( typeInfo._fullyQualifiedName );
        emit.linef( "// ── %# ──────────────────────────────", typeInfo._fullyQualifiedName );
        emit.linef( "static void fillType_%#( ::sw::TypeEditorMetadataSet& set )", id );
        emit.line( "{" );
        emit.push();
        AnnotationFields::emitEditorMetadata( emit, typeInfo, "set._type." );
        for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
        {
            if ( CodeGeneratorInternal::hasPropertyEditorMetadata( prop ) )
                emitPropertyEditorMetadata( emit, prop, "addProperty" );
        }
        for ( const ParsedEventInfo& event : typeInfo._listEvent )
        {
            if ( AnnotationFields::hasEditorMetadata( event._annotation ) )
                emitPropertyEditorMetadata( emit, event._annotation, "addEvent" );
        }
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            if ( AnnotationFields::hasEditorMetadata( method ) == false )
                continue;
            const string lookupName = ( method._bConstructor != SW_FALSE ) ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;
            emit.line( "{" );
            emit.push();
            emit.linef( "::sw::FunctionEditorMetadata& f = set.addFunction( %# );", CodeEmit::hs( lookupName ) );
            AnnotationFields::emitEditorMetadata( emit, method, "f." );
            emit.pop();
            emit.line( "}" );
        }
        emit.pop();
        emit.line( "}" );
        emit.linef( "static const ::sw::TypeEditorMetadataSource    s_%#_editorMetadata{ %#, &fillType_%# };", id, CodeEmit::quoted( typeInfo._fullyQualifiedName ), id );
        emit.linef( "static const ::sw::TypeEditorMetadataRegistrar s_%#_editorMetadataRegistrar{ &s_%#_editorMetadata };", id, id );
        emit.blank();
        return true;
    }

    bool CodeGenerator::makeHeaderText( string& outText ) const
    {
        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( sw::config::kTextReflectionBanner );
        emit.line( "#pragma once" );
        emit.blank();

        bool bNeedFlags = false;
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            if ( enumInfo._bIsBitFlag == SW_TRUE )
                bNeedFlags = true;
            // 없는 센티널은 생성 오류다 — 경고로 두면 Invalid · Count 가 없는 열거형으로 조용히 등록된다(W-R3 d).
            if ( enumInfo._invalidEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._invalidEnumerator ) == nullptr )
            {
                SW_LOG_ERROR_ID( "CodeGenerator.ENUMInvalidNotFound", "ENUM(Invalid=%#) not found on %#", enumInfo._invalidEnumerator, enumInfo._fullyQualifiedName );
                return false;
            }
            if ( enumInfo._countEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._countEnumerator ) == nullptr )
            {
                SW_LOG_ERROR_ID( "CodeGenerator.ENUMCountNotFound", "ENUM(Count=%#) not found on %#", enumInfo._countEnumerator, enumInfo._fullyQualifiedName );
                return false;
            }
        }

        if ( bNeedFlags )
        {
            // 비트 연산자(|, &, ^, ~, |=, &=, ^=)와 hasFlag/hasAnyFlag/setFlag/clearFlag 는 enum 마다
            // 코드젠하지 않고, Core/Foundation/BitFlagTrait.h 의 제네릭 sw::IsBitFlagEnum<E> 트레이트와 EnumUtil.h 의 전역
            // 스코프 SFINAE 연산자로 통일한다. 로직이 모든 enum 에서 같아 타입별 코드젠이 필요
            // 없다. 여기서는 그 트레이트를 켜는(opt-in) 한 줄짜리 명시적 특수화만 생성한다.
            //
            // **열거형을 전방 선언한 뒤 특수화한다.** 원본 헤더를 include 하지 않는다. 이 파일을
            // 모으는 우산(`FlagOps.gen.h`)이 타깃 전 TU 에 `/FI` 로 들어가므로, 여기서 원본 헤더를
            // 들이면 그 헤더가 끌어오는 것 전부가 **모든 TU 에 이미 있는 이름**이 되어 다른 헤더들의
            // include 누락을 통째로 가린다. 불투명 열거형 선언은 완전한 타입이라 특수화에 이것으로 충분하다.
            // 같은 이유로 트레이트도 `<type_traits>` 만 드는 `BitFlagTrait.h` 에서 받는다 — `EnumUtil.h` 는 `Macros.h` 를 끌어온다.
            emit.line( "#include \"Core/Foundation/BitFlagTrait.h\"" );
            emit.blank();
            for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
            {
                if ( enumInfo._bIsBitFlag == SW_FALSE )
                    continue;

                if ( enumInfo._bNestedInType != SW_FALSE )
                {
                    // 클래스 안의 열거형은 밖에서 전방 선언할 수 없어 **비트 연산자 트레이트만**
                    // 코드젠하지 못한다. 레지스트리의 비트플래그 표시(`EnumInfo::_bIsBitFlag`)는 그와
                    // 무관하게 유효하다. 인스펙터와 문자열 변환은 연산자를 쓰지 않는다. 그래서
                    // 여기서 멈추지 않고 연산자만 건너뛴다.
                    //
                    // 여기서 코드젠을 실패시키면 중첩 열거형이 `ENUM( Flags )` 를 쓸 수 없다. `|` · `&` 를 쓰면 그 자리에서
                    // 컴파일이 막히므로 조용히 잘못될 여지는 없다 — 조치할 것이 없으니 Warning 이 아니라 Info 다.
                    SW_LOG_INFO_ID( "CodeGenerator.ENUMFlagsInsideClass", "ENUM(Flags) is inside a class, so bit operators are not generated (the bit-flag marking in the registry is kept): %#", enumInfo._fullyQualifiedName );
                    continue;
                }
                if ( enumInfo._underlyingType.empty() )
                {
                    SW_LOG_ERROR_ID( "CodeGenerator.CouldNotDetermineUnderlying", "Could not determine the underlying integer type of ENUM(Flags): %#", enumInfo._fullyQualifiedName );
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
