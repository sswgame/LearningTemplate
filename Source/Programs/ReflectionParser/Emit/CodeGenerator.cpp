#include "pch.h"

#include "Programs/ReflectionParser/Emit/CodeGenerator.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/Config/ParserConfig.h"
#include "Programs/ReflectionParser/Emit/EmitTemplateStore.h"
#include "Programs/ReflectionParser/Parse/AnnotationFields.h"
#include "Programs/ReflectionParser/Parse/ReflectAnnotation.h"
#include "Programs/ReflectionParser/TypeMap/ContainerTypeMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeNameMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeSpelling.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        struct CodeGeneratorInternal
        {
            /** @brief 인자 타입의 변환 표(`ReflectTypeOpsOf<T>::kOps`) 주소 식입니다. 인자 · 반환 타입이 같은 철자를 씁니다(호출기의 `args.get<T>` 와도). */
            static string makeTypeOpsExpr( const string& normalizedType )
            {
                StringBuilder<constant::buffer::kMax1024> b;
                b.appendFormat( "&::sw::ReflectTypeOpsOf<%#>::kOps", normalizedType );
                return string( b.view() );
            }

            /**
             * @brief 인자 서술자 표 `static constexpr FunctionParameterDesc <tableName>[]` 를 씁니다 — 이름 · 정규 타입 · 기본 인자 글 · 변환 표 · 타입 핸들 대상.
             * @param bWithTypeOps 이벤트는 변환 표를 `EventInfo::setOps` 가 템플릿 인자에서 채우므로 nullptr 를 씁니다.
             * @return 표를 썼으면 그 이름, 인자가 없으면 "nullptr".
             */
            static string emitParameterTable( CodeEmit& emit, const string& tableName, const vector<ParsedParameterInfo>& listParameter, const ParserSession& session,
                                              const bool bWithTypeOps )
            {
                if ( listParameter.empty() )
                    return string( "nullptr" );
                emit.linef( "static constexpr FunctionParameterDesc %#[] =", tableName );
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

            /** @brief 등록할 옛 이름들입니다 — 빈 이름과 지금 이름(@p canonical)은 뺍니다. */
            static vector<string> makeAliasList( const vector<string>& listAlias, const string& canonical )
            {
                vector<string> listResult;
                for ( const string& alias : listAlias )
                {
                    if ( alias.empty() == false && alias != canonical )
                        listResult.push_back( alias );
                }
                return listResult;
            }

            /** @brief 서술자에 잇는 조각 `.<함수>( <인자> )` 입니다(`TypeDesc::withX`). */
            static string makeCall( const utf8* pFunction, const string_view argument )
            {
                StringBuilder<constant::buffer::kMax256> b;
                b.appendFormat( ".%#( %# )", pFunction, argument );
                return string( b.view() );
            }

            /**
             * @brief 등록기 특수화(`Registrar<열쇠>`)의 열쇠입니다. 보통 FQN 이고, 숨은 중첩 형식이면 `RegistrarKey_<ID>` 를 @p outKeyDeclaration 의 전방 선언과 함께 돌려줍니다.
             * @details 이름공간 범위의 특수화 머리 · 등록 변수는 바깥 형식의 friend 가 아니라 private 이름을 쓰면 C++17 에서 접근 오류다. 열쇠 구조체로 특수화하면 숨은 이름은
             *          `Registrar` 본문 안(바깥 형식의 `REFLECT_BODY()` 가 주는 template friend)에서만 쓰인다.
             */
            static string makeRegistrarKey( const bool bHidden, const string& registrarName, const string& fqn, string& outKeyDeclaration )
            {
                outKeyDeclaration.clear();
                if ( bHidden == false )
                    return fqn;
                StringBuilder<constant::buffer::kMax512> builder;
                builder.appendFormat( "\t// %# 는 숨은 중첩 형식이다 — 특수화 열쇠로 이름을 대신한다(본문은 바깥 형식의 REFLECT_BODY friend 안이다)\n\tstruct ", fqn );
                const uint32 keyStart = builder.size();
                builder.appendFormat( "RegistrarKey_%#", registrarName );
                const uint32 keyLength = builder.size() - keyStart;
                builder.append( ";\n" );
                // 열쇠 이름은 선언 글 안의 한 토막이다 — 버퍼가 다 자란 뒤에 잘라 낸다.
                outKeyDeclaration = string( builder.view() );
                return string( builder.view().substr( keyStart, keyLength ) );
            }

            /** @brief 이름 또는 FQN으로 enumerator 를 찾습니다. */
            static const ParsedEnumeratorInfo* findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec )
            {
                if ( spec.empty() )
                    return nullptr;
                const string_view leaf = TypeSpelling::scopeLeaf( spec );
                for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                {
                    if ( en._name == spec || en._name == leaf )
                        return &en;
                }
                return nullptr;
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
    CodeGenerator::CodeGenerator( const ParsedHeader& header, const LayoutSchemaGenerator& layoutSchema, const string& sourceFilePath,
                                  const ParserSession& session, const string& moduleName )
        : _header{ header }
        , _layoutSchema{ layoutSchema }
        , _session{ session }
        , _sourceFilePath{ sourceFilePath }
        , _moduleName{ moduleName }
        , _emitBytes{}
    {
    }

    void CodeGenerator::appendTemplate( CodeEmitBuffer& outBuffer, const EmitTemplate kind, const EmitTemplateStore::TemplateVars vars ) const
    {
        outBuffer.append( _session._emitTemplateStore.render( kind, vars ) );
    }

    string CodeGenerator::makeSourceText() const
    {
        _emitBytes = CodeEmitByteReport{};
        CodeEmitBuffer buffer;
        if ( _header._listType.empty() && _header._listEnum.empty() )
        {
            buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
            return string( buffer.view() );
        }

        emitFileHeader( buffer );
        emitRegistrationBody( buffer );
        return string( buffer.view() );
    }

    string CodeGenerator::makeInlineText() const
    {
        _emitBytes = CodeEmitByteReport{};
        CodeEmitBuffer buffer;
        if ( _header._listType.empty() && _header._listEnum.empty() )
        {
            buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
            return string( buffer.view() );
        }

        {
            const CodeEmitByteScope byteScope{ _emitBytes, buffer, CodeEmitSection::FileHeader };
            // 원본을 include 하지 않는다. 이 파일은 원본(.cpp)의 끝에서 그 .cpp 안으로 들어간다.
            buffer.appendFormat( "%#\n// Source: %#\n// Include this file at the end of the source, at global scope.\n\n", sw::config::kTextReflectionBanner, _sourceFilePath );
            // 파서가 그 .cpp 를 다시 읽을 때 지난 생성 글을 보지 않게 한다. 지운 형식을 가리키는 옛 등록 코드가 파싱을 깨뜨린다.
            buffer.appendFormat( "%#\n", emitdirective::kIfndefParser );
            buffer.append( "#include \"Engine/Object/ReflectGenerated.h\"\n\n" );
        }
        emitRegistrationBody( buffer );
        // 에디터 메타데이터는 헤더 입력이면 따로 짓는 개발 전용 단위다. 여기서는 따로 지을 수 없어 배포 구성에서 전처리로 뺀다.
        buffer.append( "#if !defined( SW_SHIPPING )\n" );
        emitEditorMetadata( buffer );
        buffer.appendFormat( "#endif // !defined( SW_SHIPPING )\n%#\n", emitdirective::kEndif );
        return string( buffer.view() );
    }

    void CodeGenerator::emitRegistrationBody( CodeEmitBuffer& buffer ) const
    {
        // 타입 핸들 인자의 확인(`ComponentHandleArgument`)은 씬 매니저를 보므로, 그런 인자가 있는 산출물만 그 헤더를 넣는다(모든 .gen.cpp 가 매니저를 끌어오지 않는다).
        if ( hasHandleArgument() )
            buffer.append( "#include \"Engine/Object/GameObject/ComponentHandleArgument.h\"\n\n" );
        // 일괄 틱 등록도 같은 규칙이다 — 그런 타입이 있는 산출물만 레지스트리 헤더를 넣는다.
        const bool bTickBatch = VectorUtil::containsIf( _header._listType, []( const ParsedTypeInfo& typeInfo )
        { return typeInfo.requiresTickRegistration(); } );
        if ( bTickBatch )
            buffer.append( "#include \"Engine/Object/GameObject/Tick/TickBatchRegistry.h\"\n\n" );
        buffer.append( emitdirective::kGeneratedNsOpen );
        for ( size_t typeIndex = 0; typeIndex < _header._listType.size(); ++typeIndex )
        {
            emitTypeRegistrar( buffer, _header._listType[typeIndex], typeIndex );
            emitTickBatchRegistration( buffer, _header._listType[typeIndex] );
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            emitEnumRegistrar( buffer, enumInfo );
        }
        buffer.append( emitdirective::kGeneratedNsClose );

        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            if ( typeInfo.requiresTypeAPI() )
                emitTypeInfoAccessors( buffer, typeInfo );
        }
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
        const CodeEmitByteScope byteScope{ _emitBytes, outBuffer, CodeEmitSection::FileHeader };
        appendTemplate( outBuffer, EmitTemplate::FileHeader, {
                                                                 {templatekey::kSourcePath,                   _sourceFilePath},
                                                                 {    templatekey::kBanner, sw::config::kTextReflectionBanner}
        } );
    }

    void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, outBuffer, CodeEmitSection::TypeTraits };
        appendTemplate( outBuffer, EmitTemplate::TypeInfoAccessors, {
                                                                        { templatekey::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& returnType,
                                           const string& callArgs ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::MethodInvoker };
        // 표 칸에 들어가는 상태 없는 람다다 — 함수 포인터(`FunctionDesc::InvokeFunction`)로 바뀐다.
        emit.line( "[]( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
        emit.line( "{" );
        emit.push();
        if ( method._listParameter.empty() )
            emit.line( "(void)args;" );

        if ( method._bStatic != SW_FALSE )
        {
            emit.line( "(void)objPtr;" );
            if ( returnType == annotation::kVoidTypeName )
            {
                emit.linef( "Self::%#(%#);", method._name, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                emit.linef( "return ::sw::TaskValue{ Self::%#(%#) };", method._name, callArgs );
            }
        }
        else
        {
            emit.line( "auto* self = static_cast<Self*>( objPtr );" );
            // 타입 핸들 인자는 이 컴포넌트의 씬에서 대상 타입을 확인해 만든다. 틀리면 확인 쪽이 오류를 남기고 메서드를 부르지 않는다(파서가 컴포넌트의 비정적 메서드만 받는다).
            for ( size_t paramIndex = 0; paramIndex < method._listParameter.size(); ++paramIndex )
            {
                const ParsedParameterInfo& parameter = method._listParameter[paramIndex];
                if ( parameter._handleTargetTypeName.empty() )
                    continue;
                emit.linef( "::%# typedArg%#{};", parameter._canonicalTypeName, static_cast<uint32>( paramIndex ) );
                StringBuilder<constant::buffer::kMax256> methodName;
                methodName.appendFormat( "%#::%#", typeInfo._fullyQualifiedName, method._name );
                emit.linef( "if ( ::sw::ComponentHandleArgument::cast( *self, args.get<::sw::ComponentHandle>( %# ), typedArg%#, %# ) == false )", static_cast<uint32>( paramIndex ),
                            static_cast<uint32>( paramIndex ), CodeEmit::quoted( methodName.view() ) );
                emit.line( "    return ::sw::TaskValue{};" );
            }
            if ( returnType == annotation::kVoidTypeName )
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
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::MethodList };
        // 메서드는 상수 초기화되는 서술자 표 하나다(프로퍼티의 `kProperties` 와 같은 모양). 인자 표를 먼저 쓴다.
        vector<string> listParamTable;
        for ( size_t methodIndex = 0; methodIndex < typeInfo._listMethod.size(); ++methodIndex )
        {
            const ParsedFunctionInfo&               method = typeInfo._listMethod[methodIndex];
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kMethodParams%#", static_cast<uint32>( methodIndex ) );
            listParamTable.push_back( CodeGeneratorInternal::emitParameterTable( emit, string( tableName.view() ), method._listParameter, _session, true ) );
            // 상수가 0 이면 "함수 상한 없음" 과 구별되지 않는다 — 컴파일에서 멈춘다.
            if ( method._maxCallsPerSecondName.empty() == false )
                emit.linef( "static_assert( ( %# ) >= 1, \"MaxCallsPerSecond must be at least 1\" );", method._maxCallsPerSecondName );
        }
        emit.line( "static constexpr FunctionDesc kMethods[] =" );
        emit.line( "{" );
        emit.push();
        for ( size_t methodIndex = 0; methodIndex < typeInfo._listMethod.size(); ++methodIndex )
        {
            const ParsedFunctionInfo& method       = typeInfo._listMethod[methodIndex];
            const bool                bConstructor = method._bConstructor != SW_FALSE;
            const string              returnType   = _session._typeNameMap.normalize( method._returnTypeName );
            // 파서가 더한 기본 생성자(거의 모든 타입)는 줄 전체가 같다 — 기본 생성할 수 있는지는 템플릿이 가른다(참조 멤버 · 기본 생성자 없는 기반은 등록에서 빠진다).
            if ( bConstructor && method._bSynthesized != SW_FALSE )
            {
                emit.linef( "FunctionDesc::defaultConstructor<Self>( %#, %# ),", CodeEmit::quoted( annotation::kCtorLookupName ), CodeEmit::quoted( returnType ) );
                continue;
            }
            const string lookupName = bConstructor ? makeCtorLookupName( method, _session ) : method._name;
            const string returnOps  = ( returnType != annotation::kVoidTypeName && bConstructor == false ) ? CodeGeneratorInternal::makeTypeOpsExpr( returnType ) : string( "nullptr" );
            const string callArgs   = CodeGeneratorInternal::makeInvokerCallArgs( method._listParameter, _session );

            emit.line( "{" );
            emit.push();
            emit.linef( "%#, %#, %#, %#,", CodeEmit::quoted( bConstructor ? string( annotation::kCtorLookupName ) : method._name ), CodeEmit::quoted( lookupName ),
                        CodeEmit::quoted( returnType ), returnOps );
            emit.linef( "%#, %#u,", listParamTable[methodIndex], static_cast<uint32>( method._listParameter.size() ) );
            // 생성자 줄은 호출 칸이 비고 생성 칸(`_pConstruct`)을 쓴다 — 생성 TU 에는 placement new 한 줄만 남긴다(`Style/PlacementNew` 와 같은 모양).
            if ( bConstructor )
            {
                emit.line( "nullptr," );
                emit.linef( "[]( void* pObject, const ::sw::TaskArgs&%# ) { sw_placement_new( pObject ) Self(%#); },", method._listParameter.empty() ? "" : " args", callArgs );
            }
            else
            {
                emitMethodInvoker( emit, typeInfo, method, returnType, callArgs );
                emit.line( "nullptr," );
            }

            // 메타데이터와 선언 사실(생성자 · 정적 · const)이다. 아무것도 없으면 등록이 공유 기본값을 쓴다(`FunctionDesc::hasMetadata`).
            AnnotationOutput output = AnnotationOutput::collectRuntime( method );
            if ( bConstructor )
                output._listFlag.emplace_back( "Constructor" );
            if ( method._bStatic != SW_FALSE )
                output._listFlag.emplace_back( "Static" );
            if ( method._bConst != SW_FALSE )
                output._listFlag.emplace_back( "Const" );
            // 함수별 초당 상한 — 상수 이름이면 그 이름을 그대로 쓴다(생성 코드에서 보이지 않으면 컴파일 오류로 멈춘다, 조용히 0 이 되지 않게).
            StringBuilder<constant::buffer::kMax256> maxCalls;
            if ( method._maxCallsPerSecondName.empty() )
                maxCalls.appendFormat( "%#u", method._maxCallsPerSecond );
            else
                maxCalls.appendFormat( "static_cast<::uint32>( %# )", method._maxCallsPerSecondName );
            emit.linef( "%#, %#, %#", CodeEmit::flagExpr( output._listFlag, "FunctionMetadataFlag" ), toCppExpr( output._netRole ), maxCalls.view() );
            emit.pop();
            emit.line( "}," );
        }
        emit.pop();
        emit.line( "};" );
    }

    void CodeGenerator::emitEventList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::EventList };
        vector<string>          listParamTable;
        for ( size_t eventIndex = 0; eventIndex < typeInfo._listEvent.size(); ++eventIndex )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kEventParams%#", static_cast<uint32>( eventIndex ) );
            listParamTable.push_back( CodeGeneratorInternal::emitParameterTable( emit, string( tableName.view() ), typeInfo._listEvent[eventIndex]._listParameter, _session, false ) );
        }
        emit.line( "static constexpr EventDesc kEvents[] =" );
        emit.line( "{" );
        emit.push();
        for ( size_t eventIndex = 0; eventIndex < typeInfo._listEvent.size(); ++eventIndex )
        {
            const ParsedEventInfo& event = typeInfo._listEvent[eventIndex];
            // 묶기 · 부르기 · 인자 변환 표는 필드 타입(`MulticastDelegate<void( Args... )>`)에서 템플릿이 만든다. 이벤트가 받는 애노테이션은 표시 메타뿐이라(개발 전용 단위) 런타임 칸이 없다.
            emit.linef( "{ %#, offsetof( Self, %# ), %#, %#u, &::sw::ReflectEventOpsOf<decltype( Self::%# )>::kOps },", CodeEmit::quoted( event._annotation._name ), event._memberName,
                        listParamTable[eventIndex], static_cast<uint32>( event._listParameter.size() ), event._memberName );
        }
        emit.pop();
        emit.line( "};" );
    }

    void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo, const size_t typeIndex ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, outBuffer, CodeEmitSection::TypeRegistrar };
        const string            registrarName = sanitizeIdentifier( typeInfo._fullyQualifiedName );
        string                  keyDeclaration;
        const string            registrarKey = CodeGeneratorInternal::makeRegistrarKey( typeInfo._bHidden == SW_TRUE, registrarName, typeInfo._fullyQualifiedName, keyDeclaration );
        outBuffer.append( keyDeclaration );
        appendTemplate( outBuffer, EmitTemplate::TypeRegistrarBegin, {
                                                                         {templatekey::kFqn, typeInfo._fullyQualifiedName},
                                                                         {templatekey::kKey,                 registrarKey},
        } );

        CodeEmit emit( outBuffer );
        emit.push( 3 );

        // 서술자에 이을 조각이며 적은 것만 잇는다(`TypeDesc::withX`). 부모는 리플렉션된 형식일 때만 적는다. 리플렉션 밖 기반(`Subsystem` 같은 Core 인터페이스)은
        // 등록부 사슬에 없어 끊긴 고리가 된다(언리얼 UCLASS 사슬도 UObject 계열 기반만 잇는다).
        vector<string> listPart;
        {
            StringBuilder<constant::buffer::kMax512> head;
            head.appendFormat( "makeTypeDesc<Self>( %#, %#, %#, %# )", CodeEmit::quoted( typeInfo._name ), CodeEmit::quoted( typeInfo._fullyQualifiedName ),
                               CodeEmit::quoted( typeInfo._bParentReflected == SW_TRUE ? typeInfo._parentFQN : string{} ), CodeEmit::quoted( _moduleName ) );
            listPart.push_back( string( head.view() ) );
        }
        vector<string> listFlag;
        if ( typeInfo._bAbstract )
            listFlag.emplace_back( "Abstract" );
        if ( typeInfo._bStatic )
            listFlag.emplace_back( "Static" );
        if ( typeInfo._bInterface == SW_TRUE )
        {
            listFlag.emplace_back( "Interface" );
            // 리플렉션 인터페이스는 상태 없는 계약이다 — 칸이 붙으면(크기가 vptr 하나를 넘으면) 컴파일이 멈춘다(파서의 데이터 멤버 검사가 놓친 기반 · 정렬까지).
            emit.linef( "static_assert( std::is_abstract_v<Self> && std::is_polymorphic_v<Self> && sizeof( Self ) == sizeof( void* ), "
                        "\"REFLECT( Interface ) %# holds state - an interface is pure virtual functions only; move the state to the implementing component\" );",
                        typeInfo._name );
        }
        if ( listFlag.empty() == false )
            listPart.push_back( CodeGeneratorInternal::makeCall( "withFlags", CodeEmit::flagExpr( listFlag, "TypeDescFlag" ) ) );
        // 타입의 저장 판(`REFLECT( Version = … )`) — 상수 이름이면 그 이름을 그대로 쓴다(값은 컴파일러가 푼다)
        if ( typeInfo._versionName.empty() == false || typeInfo._version != 0 )
        {
            StringBuilder<constant::buffer::kMax256> version;
            if ( typeInfo._versionName.empty() == false )
                version.appendFormat( "static_cast<::uint32>( %# )", typeInfo._versionName );
            else
                version.appendFormat( "%#u", typeInfo._version );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withVersion", version.view() ) );
        }
        // 내보낸 인터페이스 한 줄 = 표 한 칸(이름, 포트 오프셋). 포트가 그 인터페이스를 잇는지는 컴파일러가 본다 — 오프셋으로 되돌리는 캐스트의 전제다.
        if ( typeInfo._listInterfaceExport.empty() == false )
        {
            for ( const ParsedInterfaceExport& entry : typeInfo._listInterfaceExport )
            {
                emit.linef( "static_assert( std::is_base_of_v<::%#, decltype( Self::%# )>, \"port %# must derive from %#\" );", entry._interfaceFQN, entry._portMember,
                            entry._portMember, entry._interfaceSpelling );
            }
            emit.line( "static constexpr TypeInterfaceDesc kInterfaces[] =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedInterfaceExport& entry : typeInfo._listInterfaceExport )
            {
                emit.linef( "{ %#, static_cast<::uint32>( offsetof( Self, %# ) ) },", CodeEmit::quoted( entry._interfaceFQN ), entry._portMember );
            }
            emit.pop();
            emit.line( "};" );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withInterfaces", "kInterfaces" ) );
        }
        // 컴포넌트 생성 함수는 타입 줄의 칸이다(언리얼 `UClass` 의 생성자 칸과 같은 자리). 이름으로 만드는 길은 이 칸만 본다.
        // 람다로 감싼다 — 내보낸 클래스(`GameObject`)의 함수 주소를 상수 식에 바로 쓰면 가져오는 DLL 에서 상수가 아닐 수 있다.
        if ( typeInfo.requiresComponentFactory() )
            listPart.push_back( CodeGeneratorInternal::makeCall( "withAddComponent",
                                                                 "[]( void* pOwner ) -> void* { return ::sw::GameObject::addComponentForReflection<Self>( pOwner ); }" ) );
        // 타입 검증(`Validate`)은 에디터 데이터 검증이라 배포에는 등록하지 않는다(프로퍼티 검증과 같다). 읽기 검사(`ReadValidate`)는 배포에서도 돌고 고정 배치 코덱이 같은 함수를 부른다.
        if ( typeInfo._validate.empty() == false )
        {
            emit.linef( "static constexpr ::sw::ReflectValidateFunction kValidate = SW_DEV_ONLY_FUNCTION( ( %# ) );", makeValidateLambda( typeInfo._validate ) );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withValidate", "kValidate" ) );
        }
        if ( typeInfo._readValidate.empty() == false )
        {
            emit.linef( "static constexpr ::sw::ReflectValidateFunction kReadValidate = %#;", makeValidateLambda( typeInfo._readValidate ) );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withReadValidate", "kReadValidate" ) );
        }
        const auto addTextTable = [&emit, &listPart]( const utf8* pTableName, const utf8* pWith, const vector<string>& listText )
        {
            if ( listText.empty() )
                return;
            emit.textTable( pTableName, listText );
            listPart.push_back( CodeGeneratorInternal::makeCall( pWith, pTableName ) );
        };
        addTextTable( "kRemovedFieldNames", "withRemovedFieldNames", typeInfo._listRemovedField );
        addTextTable( "kTypeAliases", "withAliases", CodeGeneratorInternal::makeAliasList( typeInfo._listAlias, typeInfo._fullyQualifiedName ) );
        addTextTable( "kRetiredTypeNames", "withRetiredTypeNames", typeInfo._listRemovedType );

        if ( typeInfo._listProperty.empty() == false )
        {
            emitPropertyTable( emit, typeInfo );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withProperties", "kProperties" ) );
        }
        if ( typeInfo._listEvent.empty() == false )
        {
            emitEventList( emit, typeInfo );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withEvents", "kEvents" ) );
        }
        if ( typeInfo._listMethod.empty() == false )
        {
            emitMethodList( emit, typeInfo );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withMethods", "kMethods" ) );
        }
        {
            const CodeEmitByteScope codecScope{ _emitBytes, outBuffer, CodeEmitSection::LayoutSchema };
            _layoutSchema.emitRegistration( emit, typeIndex );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withLayoutSchema", "&s_layoutSchema" ) );
        }

        emit.line( "static constexpr TypeDesc kType =" );
        emit.push();
        for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
        {
            emit.linef( "%#%#", listPart[partIndex], partIndex + 1 == listPart.size() ? ";" : "" );
        }
        emit.pop();
        appendTemplate( outBuffer, EmitTemplate::TypeRegistrarEnd,
                        {
                            { templatekey::kID, registrarName},
                            {templatekey::kKey,  registrarKey},
        } );
    }

    void CodeGenerator::emitTickBatchRegistration( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, outBuffer, CodeEmitSection::TickBatch };
        // 등록 줄 모양은 틱 런타임(5-3)이 정한 `SW_REGISTER_TICK_TYPE( 타입, 모드 )` 하나다 — 손 등록과 같은 줄. 모드를 적지 않은 형식은 `makeDeclaredTickEntry` 줄이고,
        // 틱 함수를 직접 선언했는지는 그 줄이 컴파일에서 정한다(생성기는 메서드 이름 · 인자 수를 보지 않는다).
        if ( typeInfo.requiresTickRegistration() == false )
            return;
        CodeEmit emit( outBuffer );
        emit.push( 1 );
        const string id = sanitizeIdentifier( typeInfo._fullyQualifiedName );
        if ( typeInfo._tickMode.empty() )
        {
            emit.linef( "static constexpr utf8 kTickBatchScopeName_%#[] = \"GT.Scene.tickBatch.%#\";", id, typeInfo._name );
            emit.linef( "static ::sw::TickBatchRegistrar s_%#_tickBatch{ ::sw::makeDeclaredTickEntry<%#, kTickBatchScopeName_%#>() };", id, typeInfo._fullyQualifiedName, id );
            emit.line( "" );
            return;
        }
        // 생성 이름공간(`sw::generated`)은 `sw` 안이라 `sw` 바로 아래 타입은 짧은 이름으로 닿는다. 다른 이름공간의 타입은 매크로가 이름을 붙여 쓰지 못해(토큰 붙이기) 같은 줄을 펼쳐 쓴다.
        const string_view qualifiedName = typeInfo._fullyQualifiedName;
        const string_view swPrefix      = "sw::";
        if ( StringUtil::startsWith( qualifiedName, swPrefix ) && qualifiedName.substr( swPrefix.size() ) == string_view( typeInfo._name ) )
        {
            emit.linef( "SW_REGISTER_TICK_TYPE( %#, %# );", typeInfo._name, typeInfo._tickMode );
        }
        else
        {
            emit.linef( "static constexpr utf8 kTickBatchScopeName_%#[] = \"GT.Scene.tickBatch.%#\";", id, typeInfo._name );
            emit.linef( "static ::sw::TickBatchRegistrar s_%#_tickBatch{ ::sw::TickBatchRegistry::Entry{ &%#::StaticType, &::sw::TickBatchOf<%#, kTickBatchScopeName_%#>::tickRange, ::sw::TickMode::%# } };",
                        id, typeInfo._fullyQualifiedName, typeInfo._fullyQualifiedName, id, typeInfo._tickMode );
        }
        emit.line( "" );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& outBuffer, const ParsedEnumInfo& enumInfo ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, outBuffer, CodeEmitSection::EnumRegistrar };
        const string            registrarName = sanitizeIdentifier( enumInfo._fullyQualifiedName );

        const ParsedEnumeratorInfo* invalidEn = CodeGeneratorInternal::findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = CodeGeneratorInternal::findEnumerator( enumInfo, enumInfo._countEnumerator );

        // 열거형마다 맵 초기화를 펼치지 않고 항목 표(이름 · 값 배열) 하나만 낸다. 이름 표 · 값 표 · 인덱스 목록은 런타임이 이 표에서 짓는다(`EnumInfo::createFromDesc`).
        // 값은 숫자로 박지 않고 컴파일러가 계산하게 한다. libclang 의 값은 부호 있는 64 비트라 `uint8` 의 0x80 이 -128 로 적혔고,
        // 런타임은 그것을 128 로 읽어 이름을 잃었다(`EnumInfo::readValueFromMemory`). 열거자 자체를 넓히면 밑바탕 타입의 부호를 따른다.
        // 인덱스 목록도 그 값으로 지으므로 libclang 의 값과 대조하는 `static_assert` 가 필요 없다.
        CodeEmitBuffer tableBuffer;
        CodeEmit       tableEmit( tableBuffer );
        tableEmit.push( 3 );
        const auto emitTable = [&tableEmit]( const utf8* pTableName, const auto& emitRows )
        {
            tableEmit.linef( "static constexpr ::sw::NamedValue<int64> %#[] = {", pTableName );
            tableEmit.push();
            emitRows();
            tableEmit.pop();
            tableEmit.line( "};" );
        };
        if ( enumInfo._listEnumerator.empty() == false )
        {
            emitTable( "kArrEntry", [&]()
            {
                for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                {
                    tableEmit.linef( "{ %#, static_cast<int64>( E::%# ) },", CodeEmit::quoted( en._name ), en._name );
                }
            } );
        }

        // 옛 열거자 이름(이름 변경 · 지운 열거자의 대체)은 지금 이름과 따로 둔다. 읽기는 지금 이름에 없을 때만 이 표를 본다. 대상은 `AstVisitor` 가 이미 확인했다.
        const bool bHasRedirect = enumInfo._listValueAlias.empty() == false || enumInfo._listRemovedEnumerator.empty() == false;
        if ( bHasRedirect )
        {
            emitTable( "kArrRedirect", [&]()
            {
                for ( const vector<pair<string, string>>* pListRedirect : { &enumInfo._listValueAlias, &enumInfo._listRemovedEnumerator } )
                {
                    for ( const auto& [oldName, targetName] : *pListRedirect )
                    {
                        const ParsedEnumeratorInfo* pTarget = CodeGeneratorInternal::findEnumerator( enumInfo, targetName );
                        tableEmit.linef( "{ %#, static_cast<int64>( E::%# ) },", CodeEmit::quoted( oldName ), pTarget->_name );
                    }
                }
            } );
        }

        StringBuilder<constant::buffer::kMax64> aliases;
        const vector<string>                    listAlias = CodeGeneratorInternal::makeAliasList( enumInfo._listAlias, enumInfo._fullyQualifiedName );
        if ( listAlias.empty() == false )
        {
            tableEmit.textTable( "kEnumAliases", listAlias );
            aliases.appendFormat( ", kEnumAliases, %#u", static_cast<uint32>( listAlias.size() ) );
        }

        // 열거형의 커스텀 메타는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`makeEditorMetadataText`).
        string       keyDeclaration;
        const string registrarKey = CodeGeneratorInternal::makeRegistrarKey( enumInfo._bHidden == SW_TRUE, registrarName, enumInfo._fullyQualifiedName, keyDeclaration );
        outBuffer.append( keyDeclaration );
        appendTemplate( outBuffer, EmitTemplate::EnumRegistrar, {
                                                                    {           templatekey::kID,                                                                     registrarName},
                                                                    {          templatekey::kFqn,                                                      enumInfo._fullyQualifiedName},
                                                                    {          templatekey::kKey,                                                                      registrarKey},
                                                                    {         templatekey::kName,                                                                    enumInfo._name},
                                                                    {   templatekey::kModuleName,                                                                       _moduleName},
                                                                    {    templatekey::kIsBitFlag,                                           enumInfo._bIsBitFlag ? "true" : "false"},
                                                                    {  templatekey::kInvalidName, invalidEn != nullptr ? CodeEmit::quoted( invalidEn->_name ) : string( "nullptr" )},
                                                                    {    templatekey::kCountName,     countEn != nullptr ? CodeEmit::quoted( countEn->_name ) : string( "nullptr" )},
                                                                    {   templatekey::kEntryTable,                             enumInfo._listEnumerator.empty() ? "{}" : "kArrEntry"},
                                                                    {templatekey::kRedirectTable,                                              bHasRedirect ? "kArrRedirect" : "{}"},
                                                                    {      templatekey::kAliases,                                                                    aliases.view()},
                                                                    {       templatekey::kTables,                                                                tableBuffer.view()},
        } );
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
            if ( enumInfo._invalidEnumerator.empty() == false && CodeGeneratorInternal::findEnumerator( enumInfo, enumInfo._invalidEnumerator ) == nullptr )
            {
                SW_LOG_ERROR_ID( "CodeGenerator.ENUMInvalidNotFound", "ENUM(Invalid=%#) not found on %#", enumInfo._invalidEnumerator, enumInfo._fullyQualifiedName );
                return false;
            }
            if ( enumInfo._countEnumerator.empty() == false && CodeGeneratorInternal::findEnumerator( enumInfo, enumInfo._countEnumerator ) == nullptr )
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
            // 열거형을 전방 선언한 뒤 특수화한다. 원본 헤더를 include 하지 않는다. 이 파일을
            // 모으는 우산(`FlagOps.gen.h`)이 타깃 전 TU 에 `/FI` 로 들어가므로, 여기서 원본 헤더를
            // 들이면 그 헤더가 끌어오는 것 전부가 모든 TU 에 이미 있는 이름이 되어 다른 헤더들의
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
                    // 클래스 안의 열거형은 밖에서 전방 선언할 수 없어 비트 연산자 트레이트만
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

    string CodeGenerator::makeCtorLookupName( const ParsedFunctionInfo& method, const ParserSession& session )
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

    string CodeGenerator::makeValidateLambda( const string& name )
    {
        StringBuilder<constant::buffer::kMax256> b;
        b.appendFormat( "[]( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<Self*>( const_cast<void*>( pInstance ) )->%#( context ); }", name );
        return string( b.view() );
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
