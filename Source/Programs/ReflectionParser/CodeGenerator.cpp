#include "pch.h"

#include "Programs/ReflectionParser/CodeGenerator.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/AnnotationFields.h"
#include "Programs/ReflectionParser/ContainerTypeMap.h"
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
            /**
             * @brief 범위 경계 하나를 float32 글(`1.5f`)로 냅니다. 등록 단위의 허용 범위와 개발 전용 단위의 슬라이더 범위가 함께 쓴다.
             * @details 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가 생성된 파일마다 난다.
             *          경계는 float32 가 되돌아오는 자릿수(9)로 낸다. 6 자리로 자르면 `Max = 6.2831853` 이 6.283185f 가 되어 2π 를 담은 값이 범위 밖이 된다.
             */
            static string formatRangeLiteral( const float32 value )
            {
                utf8 arrBuffer[constant::buffer::kMax64]{};
                formatstring( arrBuffer, sizeof( arrBuffer ), "%.9g", static_cast<float64>( value ) );
                StringBuilder<constant::buffer::kMax64> literal;
                literal.append( arrBuffer );
                if ( literal.view().find_first_of( ".eE" ) == string_view::npos )
                    literal.append( ".0" );
                literal.append( 'f' );
                return string( literal.view() );
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

            /** @brief 필드 이름 꼴 경계를 인스턴스에서 읽는 함수(`PropertyMetadata::_pReadMinRange` 등)의 람다입니다. 그 꼴이 아니면 `nullptr` 입니다. 읽기 공통 끝도 지키므로 Shipping 에도 냅니다. */
            static string makeRangeReader( const ParsedTypeInfo& typeInfo, const string& text )
            {
                bool                      bNegate = false;
                const ParsedPropertyInfo* pField  = findOwnRangeField( typeInfo, text, bNegate );
                if ( pField == nullptr )
                    return string( "nullptr" );
                // 접근자 프로퍼티는 메서드를 부른다(`_pValueAccessor` 와 같은 규칙). 이름이 틀리거나 숫자가 아닌 필드면 생성된 코드가 컴파일되지 않는다.
                StringBuilder<constant::buffer::kMax512> b;
                b.appendFormat( "[]( const void* pInstance ) -> ::float64 { return %#PropertyMetadata::toRangeNumber( static_cast<const Self*>( pInstance )->%#%# ); }",
                                bNegate ? "-" : "", pField->_memberName, pField->_bIsAccessor == SW_TRUE ? "()" : "" );
                return string( b.view() );
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
                const ParsedPropertyInfo*                pOwn  = findOwnProperty( typeInfo, name );
                const bool                               bCall = pOwn != nullptr && pOwn->_bIsAccessor == SW_TRUE;
                StringBuilder<constant::buffer::kMax128> b;
                b.appendFormat( "o.%#%#", name, bCall ? "()" : "" );
                return string( b.view() );
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
                {
                    StringBuilder<constant::buffer::kMax256> b;
                    b.appendFormat( "::%#::%#", prop._fixedType->_enum->_fullyQualifiedName, text );
                    return string( b.view() );
                }
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
                    const bool                               bNegate = text.front() == '!';
                    const string_view                        name    = StringUtil::trim( bNegate ? text.substr( 1 ) : text );
                    StringBuilder<constant::buffer::kMax256> b;
                    b.appendFormat( "%#static_cast<bool>( %# )", bNegate ? "!" : "", makeMemberRead( typeInfo, name ) );
                    return string( b.view() );
                }
                const size_t                             opPos = eqPos != string_view::npos ? eqPos : nePos;
                const string                             read  = makeMemberRead( typeInfo, StringUtil::trim( text.substr( 0, opPos ) ) );
                const string_view                        op    = text.substr( opPos, 2 );
                string_view                              value = StringUtil::trim( text.substr( opPos + 2 ) );
                int64                                    number{ 0 };
                StringBuilder<constant::buffer::kMax256> b;
                if ( StringUtil::parseInt64( value, number ) )
                {
                    b.appendFormat( "static_cast<::int64>( %# ) %# %#", read, op, value );
                    return string( b.view() );
                }
                // 한정해 적어도(`Mode::Orbit`) 끝 이름을 칸의 타입으로 다시 한정한다 — 적은 한정자가 다른 열거형이어도 칸과 맞춘다.
                value = ParserUtil::scopeLeaf( value );
                b.appendFormat( "%# %# ::std::decay_t<decltype( %# )>::%#", read, op, read, value );
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

            /** @brief 이름들로 문자열 리터럴 표 `static constexpr const ::utf8* <이름>[]` 을 씁니다(옛 이름 · 지운 이름). */
            static void emitTextTable( CodeEmit& emit, const string_view tableName, const vector<string>& listText )
            {
                StringBuilder<constant::buffer::kMax256> texts;
                for ( const string& text : listText )
                {
                    texts.append( texts.size() > 0 ? ", " : "" );
                    texts.append( CodeEmit::quoted( text ) );
                }
                emit.linef( "static constexpr const ::utf8* %#[] = { %# };", tableName, texts.view() );
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

            /** @brief 플래그 이름들을 엔진 상수의 OR 식(`PropertyMetadataFlag::kReadOnly | …`)으로 만듭니다. 없으면 `0u` 입니다. */
            static string makeFlagExpr( const vector<string>& listFlag, const utf8* pFlagStruct )
            {
                if ( listFlag.empty() )
                    return string( "0u" );
                StringBuilder<constant::buffer::kMax256> b;
                for ( const string& flag : listFlag )
                {
                    b.appendFormat( "%#%#::k%#", b.size() > 0 ? " | " : "", pFlagStruct, flag );
                }
                return string( b.view() );
            }

            /** @brief 집합체 칸(식, 엔진 쪽 기본값) 목록을 초기화 목록 글로 잇습니다. 뒤에서부터 기본값과 같은 칸은 뺀다 — 모두 기본값이면 빈 글입니다. */
            static string makeCellList( std::initializer_list<pair<string, string_view>> listCell )
            {
                size_t count = listCell.size();
                while ( count > 0 && string_view( ( listCell.begin() + ( count - 1 ) )->first ) == ( listCell.begin() + ( count - 1 ) )->second )
                {
                    --count;
                }
                StringBuilder<constant::buffer::kMax512> b;
                for ( size_t index = 0; index < count; ++index )
                {
                    b.append( index > 0 ? ", " : "" );
                    b.append( ( listCell.begin() + index )->first );
                }
                return string( b.view() );
            }

            /** @brief 서술자에 잇는 조각 `.<함수>( <인자> )` 입니다(`TypeDesc::withX`). */
            static string makeCall( const utf8* pFunction, const string_view argument )
            {
                StringBuilder<constant::buffer::kMax256> b;
                b.appendFormat( ".%#( %# )", pFunction, argument );
                return string( b.view() );
            }

            /** @brief 검증 함수 @p name 을 부르는 `ReflectValidateFunction` 람다입니다. const 든 아니든 부를 수 있게 인스턴스를 그 타입으로 돌려 부른다. */
            static string makeValidateLambda( const string& name )
            {
                StringBuilder<constant::buffer::kMax256> b;
                b.appendFormat( "[]( const void* pInstance, ::sw::ValidationContext& context ) { static_cast<Self*>( const_cast<void*>( pInstance ) )->%#( context ); }", name );
                return string( b.view() );
            }

            /** @brief 에디터 메타데이터 한 묶음을 개발 전용 단위의 줄(`EditorMetadataRow`)들로 더합니다. @p nameExpr 은 대상의 이름 글(타입 · 열거형 자신이면 `nullptr`)입니다. */
            static void appendEditorRows( vector<string>& outListRow, const utf8* pTarget, const string& nameExpr, const AnnotationOutput& output )
            {
                const auto append = [&outListRow]( const string_view row )
                { outListRow.push_back( string( row ) ); };
                for ( const auto& [field, text] : output._listText )
                {
                    StringBuilder<constant::buffer::kMax512> row;
                    row.appendFormat( "{ Target::%#, Field::%#, %#, %# },", pTarget, field, nameExpr, CodeEmit::quoted( text ) );
                    append( row.view() );
                }
                for ( const string& flag : output._listFlag )
                {
                    StringBuilder<constant::buffer::kMax256> row;
                    row.appendFormat( "{ Target::%#, Field::%#, %# },", pTarget, flag, nameExpr );
                    append( row.view() );
                }
                for ( const auto& [key, value] : output._listCustomMeta )
                {
                    StringBuilder<constant::buffer::kMax512> row;
                    row.appendFormat( "{ Target::%#, Field::CustomMeta, %#, %#, %# },", pTarget, nameExpr, CodeEmit::quoted( value ), CodeEmit::quoted( key ) );
                    append( row.view() );
                }
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
        return string( buffer.view() );
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

    string CodeGenerator::emitPropertyMetadata( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::PropertyMetadata };
        AnnotationOutput        output = AnnotationOutput::collectRuntime( prop );
        // 범위는 경계마다 값과 "있음" 표시가 함께 가는 Manual 필드다. 적힌 쪽만 낸다 — 표시가 하나면 `Min` 만 적어도
        // 위 경계(기본 1)까지 나가 인스펙터가 그 값을 1 에서 막는다.
        if ( prop._bHasMinRange != SW_FALSE )
            output._listFlag.emplace_back( "HasMinRange" );
        if ( prop._bHasMaxRange != SW_FALSE )
            output._listFlag.emplace_back( "HasMaxRange" );

        string textTable( "nullptr" );
        if ( output._listText.empty() == false )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kMetaText%#", propIndex );
            StringBuilder<constant::buffer::kMax1024> rows;
            for ( const auto& [kind, text] : output._listText )
            {
                rows.appendFormat( "{ PropertyMetadataTextKind::%#, %# }, ", kind, CodeEmit::quoted( text ) );
            }
            emit.linef( "static constexpr PropertyMetadataText %#[] = { %#};", tableName.view(), rows.view() );
            textTable = string( tableName.view() );
        }

        StringBuilder<constant::buffer::kMax128> maxCount;
        maxCount.appendFormat( "%#u", prop._bHasMaxCount != SW_FALSE ? prop._maxCount : 0u ); // 상한은 코덱이 지키는 값 그대로다(상수 이름으로 적었으면 `PropertyResolver` 가 푼 값)
        StringBuilder<constant::buffer::kMax64> textCount;
        textCount.appendFormat( "%#u", static_cast<uint32>( output._listText.size() ) );
        // 단위는 상수 문맥에서 철자를 열거로 바꾼다 — 표에서 빠진 철자는 컴파일 오류다(검증은 `applyDisplayMeta` 가 먼저 했다).
        string unit( "ReflectUnitKind::None" );
        if ( prop._units.empty() == false )
        {
            StringBuilder<constant::buffer::kMax128> unitExpr;
            unitExpr.appendFormat( "ReflectUnit::getRequiredKindOf( %# )", CodeEmit::quoted( prop._units ) );
            unit = string( unitExpr.view() );
        }
        // 기본값 · 편집 조건은 글이 아니라 컴파일된 대입 · 판정이다 — 이름 · 철자가 틀리면 이 칸이 컴파일되지 않는다.
        // 이 표는 `Registrar<T>`(모든 REFLECT 타입의 친구) 안이라 기반의 비공개 칸도 읽는다.
        string applyDefault( "nullptr" );
        if ( prop._defaultValue.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> b;
            b.appendFormat( "[]( void* pInstance ) { static_cast<Self*>( pInstance )->%#%# = %#; }", prop._memberName, prop._bIsAccessor == SW_TRUE ? "()" : "",
                            CodeGeneratorInternal::makeDefaultLiteral( prop ) );
            applyDefault = string( b.view() );
        }
        string editCondition( "nullptr" );
        if ( prop._editCondition.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> b;
            b.appendFormat( "[]( const void* pInstance ) -> bool { const Self& o = *static_cast<const Self*>( pInstance ); return %#; }",
                            CodeGeneratorInternal::makeEditConditionExpr( typeInfo, prop._editCondition ) );
            editCondition = string( b.view() );
        }

        // 칸 차례는 엔진 `PropertyMetadataDesc` 와 같다. 슬라이더 범위는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`collectTypeEditorRows`).
        // 숫자가 아닌 경계 중 이 타입의 필드 하나를 가리키는 것은 인스턴스에서 읽는 함수다(인스펙터와 읽기 공통 끝이 그 값으로 막는다).
        const string cells = CodeGeneratorInternal::makeCellList( {
            { CodeGeneratorInternal::makeFlagExpr( output._listFlag, "PropertyMetadataFlag" ), "0u" },
            { prop._bHasMinRange != SW_FALSE ? CodeGeneratorInternal::formatRangeLiteral( prop._minRange ) : string( "0.0f" ), "0.0f" },
            { prop._bHasMaxRange != SW_FALSE ? CodeGeneratorInternal::formatRangeLiteral( prop._maxRange ) : string( "1.0f" ), "1.0f" },
            { unit, "ReflectUnitKind::None" },
            { string( maxCount.view() ), "0u" },
            { textTable, "nullptr" },
            { string( textCount.view() ), "0u" },
            { applyDefault, "nullptr" },
            { CodeGeneratorInternal::makeRangeReader( typeInfo, prop._minRangeExpr._text ), "nullptr" },
            { CodeGeneratorInternal::makeRangeReader( typeInfo, prop._maxRangeExpr._text ), "nullptr" },
            { editCondition, "nullptr" },
        } );
        if ( cells.empty() )
            return string( "nullptr" ); // 적은 것이 없는 프로퍼티는 공유 기본값을 쓴다(힙 없음, `ReflectMetadataBox`)
        emit.linef( "static constexpr PropertyMetadataDesc kMeta%#{ %# };", propIndex, cells );
        StringBuilder<constant::buffer::kMax64> address;
        address.appendFormat( "&kMeta%#", propIndex );
        return string( address.view() );
    }

    string CodeGenerator::emitPropertyContainer( CodeEmit& emit, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::PropertyContainer };
        if ( prop._bIsContainer == SW_FALSE )
            return string( "nullptr" );

        // 단계마다 상수 줄 하나다 — 안쪽이 먼저 있어야 바깥이 그 주소를 든다(아래에서 거꾸로 낸다).
        struct Level
        {
            string _kind;
            string _typeName;
            string _elementTypeName;
            string _keyTypeName;
            string _wrapperType;
        };
        const auto makeWrapperType = [propIndex]( const string& containerType, const int32 depth )
        {
            StringBuilder<constant::buffer::kMax256> b;
            b.appendFormat( "sw::%#Wrapper<NestC%#_%#>", containerType, propIndex, depth );
            return string( b.view() );
        };
        emit.linef( "using NestC%#_0 = %#;", propIndex, makePropertyDeclType( prop ) );
        const string  wrapperType = makeWrapperType( prop._containerType, 0 );
        vector<Level> listLevel;
        if ( prop._containerTree != nullptr && prop._containerTree->_bIsContainer != SW_FALSE )
        {
            listLevel.push_back( Level{ toCppExpr( prop._containerKind ), prop._containerTree->_typeName, _session._typeNameMap.normalize( prop._elementTypeName ),
                                        _session._typeNameMap.normalize( prop._keyTypeName ), wrapperType } );
            const ParsedContainerNode* node     = prop._containerTree->_elementNested.get();
            ContainerKind              prevKind = prop._containerKind;
            int32                      depth    = 1;
            // 깊이 한도는 두지 않는다 — 단계마다 앞 단계의 원소 형식 별칭(`NestC<프로퍼티>_<깊이>`)을 잇는 반복이라 몇 겹이든 같다. 예전 한도(3)는 넘는 단계를 조용히 잘랐다.
            while ( node != nullptr && node->_bIsContainer )
            {
                emit.linef( "using NestC%#_%# = typename NestC%#_%#::%#;", propIndex, depth, propIndex, depth - 1, containerElementTypeMember( prevKind ) );
                // 안쪽 단계의 키 이름은 정규화하지 않는다(예전 출력과 같다).
                listLevel.push_back( Level{ toCppExpr( node->_containerKind ), node->_typeName, _session._typeNameMap.normalize( node->_elementTypeName ), node->_keyTypeName,
                                            makeWrapperType( node->_containerType, depth ) } );
                prevKind = node->_containerKind;
                node     = node->_elementNested.get();
                ++depth;
            }
        }
        for ( size_t level = listLevel.size(); level-- > 0; )
        {
            const Level&                             entry = listLevel[level];
            StringBuilder<constant::buffer::kMax128> next;
            if ( level + 1 < listLevel.size() )
                next.appendFormat( "&kNested%#_%#", propIndex, static_cast<uint32>( level + 1 ) );
            else
                next.append( "nullptr" );
            emit.linef( "static constexpr NestedContainerDesc kNested%#_%#{ %#, %#, %#, %#, &ContainerWrapperOf<%#>::kInstance, %# };", propIndex, static_cast<uint32>( level ),
                        entry._kind, CodeEmit::quoted( entry._typeName ), CodeEmit::quoted( entry._elementTypeName ), CodeEmit::quoted( entry._keyTypeName ), entry._wrapperType,
                        next.view() );
        }

        // 표(`ReflectBuiltins.xxx` CONTAINER 줄)의 종류 · 역할이 래퍼 형식과 같은지 컴파일이 본다 — 어긋나면 이 칸의 생성 코드가 멈춘다.
        // 표 밖 컨테이너(`REFLECT_CONTAINER( Kind, Stem )`)는 역할 칸이 없어 종류만 본다.
        StringBuilder<constant::buffer::kMax512> wrapper;
        const ContainerTypeRule*                 pRule = _session._containerTypeMap.findByWrapperStem( prop._containerType );
        if ( pRule != nullptr )
            wrapper.appendFormat( "ContainerWrapperOf<%#>::getChecked<%#, %#, %#>()", wrapperType, toCppExpr( pRule->_kind ), pRule->_bState == SW_TRUE ? "true" : "false",
                                  pRule->_bFixed == SW_TRUE ? "true" : "false" );
        else
            wrapper.appendFormat( "ContainerWrapperOf<%#>::getOfKind<%#>()", wrapperType, toCppExpr( prop._containerKind ) );
        const auto textOrNull = []( const string& text )
        { return text.empty() ? string( "nullptr" ) : CodeEmit::quoted( text ); };
        StringBuilder<constant::buffer::kMax64> nested;
        if ( listLevel.empty() )
            nested.append( "nullptr" );
        else
            nested.appendFormat( "&kNested%#_0", propIndex );
        emit.linef( "static constexpr PropertyContainerDesc kContainer%#{ %#, %#, %#, %#, %# };", propIndex, toCppExpr( prop._containerKind ),
                    textOrNull( _session._typeNameMap.normalize( prop._elementTypeName ) ), textOrNull( _session._typeNameMap.normalize( prop._keyTypeName ) ), wrapper.view(),
                    nested.view() );
        StringBuilder<constant::buffer::kMax64> address;
        address.appendFormat( "&kContainer%#", propIndex );
        return string( address.view() );
    }

    string CodeGenerator::makePropertyDeclType( const ParsedPropertyInfo& prop )
    {
        // 접근자 프로퍼티(값이 객체 밖)는 필드가 없다. 선언 타입은 메서드가 돌려주는 참조의 대상이다.
        StringBuilder<constant::buffer::kMax256> b;
        if ( prop._bIsAccessor == SW_TRUE )
            b.appendFormat( "std::remove_reference_t<decltype( std::declval<Self&>().%#() )>", prop._memberName );
        else
            b.appendFormat( "decltype( Self::%# )", prop._memberName );
        return string( b.view() );
    }

    string CodeGenerator::emitPropertyBinding( CodeEmit& emit, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::PropertyBinding };
        string                  aliasTable( "nullptr" );
        if ( prop._listAlias.empty() == false )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kAlias%#", propIndex );
            CodeGeneratorInternal::emitTextTable( emit, tableName.view(), prop._listAlias );
            aliasTable = string( tableName.view() );
        }
        StringBuilder<constant::buffer::kMax64> aliasCount;
        aliasCount.appendFormat( "%#u", static_cast<uint32>( prop._listAlias.size() ) );

        // 값 자리는 메서드가 안다. 인자는 오프셋이 기준으로 삼는 객체 주소다(`PropertyInfo::getRawPtr`).
        string accessor( "nullptr" );
        if ( prop._bIsAccessor == SW_TRUE )
        {
            StringBuilder<constant::buffer::kMax256> b;
            b.appendFormat( "[]( void* pInstance ) -> void* { return std::addressof( static_cast<Self*>( pInstance )->%#() ); }", prop._memberName );
            accessor = string( b.view() );
        }
        // RepNotify · Validate 는 파서가 모양을 확인했다(`validateMemberFunctions`) — 이전 값을 받는 함수면 그 타입 그대로 넘긴다.
        string repNotify( "nullptr" );
        if ( prop._repNotify.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> b;
            if ( prop._bRepNotifyTakesOldValue == SW_TRUE )
                b.appendFormat( "[]( void* pInstance, const void* pOldValue ) { static_cast<Self*>( pInstance )->%#( *static_cast<const %#*>( pOldValue ) ); }", prop._repNotify,
                                makePropertyDeclType( prop ) );
            else
                b.appendFormat( "[]( void* pInstance, const void* ) { static_cast<Self*>( pInstance )->%#(); }", prop._repNotify );
            repNotify = string( b.view() );
        }
        // const 든 아니든 부를 수 있게 인스턴스를 그 타입으로 돌려 부른다.
        string validate( "nullptr" );
        if ( prop._validate.empty() == false )
            validate = CodeGeneratorInternal::makeValidateLambda( prop._validate );
        // 파서가 측정한 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 측정한다(`PropertyInfo::resolveBitField` 설명).
        string setBit( "nullptr" );
        if ( prop._bIsBitField == SW_TRUE )
        {
            StringBuilder<constant::buffer::kMax256> b;
            b.appendFormat( "[]( void* pInstance ) { static_cast<Self*>( pInstance )->%# = static_cast<%#>( 1 ); }", prop._memberName, makePropertyDeclType( prop ) );
            setBit = string( b.view() );
        }

        // 칸 차례는 엔진 `PropertyBindingDesc` 와 같다.
        const string cells = CodeGeneratorInternal::makeCellList( {
            {prop._handleTargetTypeName.empty() ? string( "nullptr" ) : CodeEmit::quoted( prop._handleTargetTypeName ), "nullptr"},
            {                                                                                               aliasTable, "nullptr"},
            {                                                                              string( aliasCount.view() ),      "0u"},
            {                                                                                                 accessor, "nullptr"},
            {                                                                                                repNotify, "nullptr"},
            {                                                                                                 validate, "nullptr"},
            {                                                                                                   setBit, "nullptr"},
            {                               prop._bIsBitField == SW_TRUE ? string( "sizeof( Self )" ) : string( "0u" ),      "0u"},
        } );
        if ( cells.empty() )
            return string( "nullptr" );
        emit.linef( "static constexpr PropertyBindingDesc kBinding%#{ %# };", propIndex, cells );
        StringBuilder<constant::buffer::kMax64> address;
        address.appendFormat( "&kBinding%#", propIndex );
        return string( address.view() );
    }

    void CodeGenerator::emitPropertyTable( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::PropertyDesc };
        // PROPERTY() 에 값으로 담으면 안 되는 기반 타입은 `ReflectGenerated.h` 의 검사가 막는다(타입마다 한 줄).
        StringBuilder<constant::buffer::kMax1024> declTypes;
        for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
        {
            declTypes.append( declTypes.size() > 0 ? ", " : "" );
            declTypes.append( makePropertyDeclType( prop ) );
        }
        emit.linef( "static_assert( arePropertyValuesAllowed<%#>() );", declTypes.view() );

        vector<string> listRow;
        for ( size_t propIndex = 0; propIndex < typeInfo._listProperty.size(); ++propIndex )
        {
            const ParsedPropertyInfo& prop      = typeInfo._listProperty[propIndex];
            const uint32              index     = static_cast<uint32>( propIndex );
            const string              metadata  = emitPropertyMetadata( emit, typeInfo, prop, index );
            const string              container = emitPropertyContainer( emit, prop, index );
            const string              binding   = emitPropertyBinding( emit, prop, index );
            // 비트필드는 `offsetof` 를 쓸 수 없다. 자리는 `resolveBitField` 가 이 구성의 실제 레이아웃에서 찾는다(`PropertyBindingDesc::_pSetBit`).
            StringBuilder<constant::buffer::kMax256> offset;
            if ( prop._bIsBitField == SW_TRUE || prop._bIsAccessor == SW_TRUE )
                offset.append( "0u" );
            else
                offset.appendFormat( "offsetof( Self, %# )", prop._memberName );
            const string                             tail = CodeGeneratorInternal::makeCellList( {
                { metadata, "nullptr"},
                {container, "nullptr"},
                {  binding, "nullptr"},
            } );
            StringBuilder<constant::buffer::kMax512> row;
            row.appendFormat( "{ %#, %#, %#%#%# },", CodeEmit::quoted( prop._name ), CodeEmit::quoted( _session._typeNameMap.normalize( prop._typeName ) ), offset.view(),
                              tail.empty() ? "" : ", ", tail );
            listRow.push_back( string( row.view() ) );
        }
        emit.line( "static constexpr PropertyDesc kProperties[] =" );
        emit.line( "{" );
        emit.push();
        for ( const string& row : listRow )
        {
            emit.line( row );
        }
        emit.pop();
        emit.line( "};" );
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
            const string lookupName = bConstructor ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;
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
            emit.linef( "%#, %#, %#", CodeGeneratorInternal::makeFlagExpr( output._listFlag, "FunctionMetadataFlag" ), toCppExpr( output._netRole ), maxCalls.view() );
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
        const string            registrarKey = makeRegistrarKey( typeInfo._bHidden == SW_TRUE, registrarName, typeInfo._fullyQualifiedName, keyDeclaration );
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
            listPart.push_back( CodeGeneratorInternal::makeCall( "withFlags", CodeGeneratorInternal::makeFlagExpr( listFlag, "TypeDescFlag" ) ) );
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
        if ( typeInfo._validate.empty() == false )
        {
            emit.linef( "static constexpr ::sw::ReflectValidateFunction kValidate = %#;", CodeGeneratorInternal::makeValidateLambda( typeInfo._validate ) );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withValidate", "kValidate" ) );
        }
        const auto addTextTable = [&emit, &listPart]( const utf8* pTableName, const utf8* pWith, const vector<string>& listText )
        {
            if ( listText.empty() )
                return;
            CodeGeneratorInternal::emitTextTable( emit, pTableName, listText );
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
            const CodeEmitByteScope codecScope{ _emitBytes, outBuffer, CodeEmitSection::FixedCodec };
            _fixedCodec.emitRegistration( emit, typeIndex );
            listPart.push_back( CodeGeneratorInternal::makeCall( "withFixedCodec", "&s_fixedCodec" ) );
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

        const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = findEnumerator( enumInfo, enumInfo._countEnumerator );

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
                        const ParsedEnumeratorInfo* pTarget = findEnumerator( enumInfo, targetName );
                        tableEmit.linef( "{ %#, static_cast<int64>( E::%# ) },", CodeEmit::quoted( oldName ), pTarget->_name );
                    }
                }
            } );
        }

        StringBuilder<constant::buffer::kMax64> aliases;
        const vector<string>                    listAlias = CodeGeneratorInternal::makeAliasList( enumInfo._listAlias, enumInfo._fullyQualifiedName );
        if ( listAlias.empty() == false )
        {
            CodeGeneratorInternal::emitTextTable( tableEmit, "kEnumAliases", listAlias );
            aliases.appendFormat( ", kEnumAliases, %#u", static_cast<uint32>( listAlias.size() ) );
        }

        // 열거형의 커스텀 메타는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`makeEditorMetadataText`).
        string       keyDeclaration;
        const string registrarKey = makeRegistrarKey( enumInfo._bHidden == SW_TRUE, registrarName, enumInfo._fullyQualifiedName, keyDeclaration );
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

    string CodeGenerator::makeRegistrarKey( const bool bHidden, const string& registrarName, const string& fqn, string& outKeyDeclaration )
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

    const ParsedEnumeratorInfo* CodeGenerator::findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec )
    {
        if ( spec.empty() )
            return nullptr;
        const string_view leaf = ParserUtil::scopeLeaf( spec );
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
            vector<string> listRow;
            collectTypeEditorRows( typeInfo, listRow );
            bAny = emitEditorRowTable( emit, typeInfo._fullyQualifiedName, "Type", listRow ) || bAny;
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            vector<string>   listRow;
            AnnotationOutput enumOutput = AnnotationOutput::collectEditor( enumInfo );
            CodeGeneratorInternal::appendEditorRows( listRow, "Enum", "nullptr", enumOutput );
            // 열거자마다의 표시 메타(`ENUMERATOR( … )`)는 열거자 이름으로 단다 — 적은 열거자만.
            for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
            {
                AnnotationOutput output = AnnotationOutput::collectEditor( enumerator );
                CodeGeneratorInternal::appendEditorRows( listRow, "Enumerator", CodeEmit::quoted( enumerator._name ), output );
            }
            bAny = emitEditorRowTable( emit, enumInfo._fullyQualifiedName, "Enum", listRow ) || bAny;
        }
        if ( bAny == false )
            emit.linef( "// No editor metadata in %#", _sourceFilePath );
        buffer.append( "} // namespace sw::generated::editormetadata\n" );
        return string( buffer.view() );
    }

    void CodeGenerator::collectTypeEditorRows( const ParsedTypeInfo& typeInfo, vector<string>& outListRow ) const
    {
        AnnotationOutput typeOutput = AnnotationOutput::collectEditor( typeInfo );
        CodeGeneratorInternal::appendEditorRows( outListRow, "Type", "nullptr", typeOutput );

        // 이름은 등록 단위와 같다 — 프로퍼티 · 이벤트는 이름, 메서드는 찾는 이름(`FunctionInfo::_hashName`, 생성자는 `makeCtorLookupName`).
        const auto appendProperty = [&outListRow]( const ParsedPropertyInfo& prop, const utf8* pTarget )
        {
            AnnotationOutput output = AnnotationOutput::collectEditor( prop );
            const string     name   = CodeEmit::quoted( prop._name );
            CodeGeneratorInternal::appendEditorRows( outListRow, pTarget, name, output );
            const auto appendNumber = [&outListRow, &name, pTarget]( const utf8* pField, const float32 value )
            {
                StringBuilder<constant::buffer::kMax256> row;
                row.appendFormat( "{ Target::%#, Field::%#, %#, nullptr, nullptr, %# },", pTarget, pField, name, CodeGeneratorInternal::formatRangeLiteral( value ) );
                outListRow.push_back( string( row.view() ) );
            };
            if ( prop._bHasUIMinRange != SW_FALSE )
                appendNumber( "UIMinRange", prop._uiMinRange );
            if ( prop._bHasUIMaxRange != SW_FALSE )
                appendNumber( "UIMaxRange", prop._uiMaxRange );
        };
        for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
        {
            appendProperty( prop, "Property" );
        }
        for ( const ParsedEventInfo& event : typeInfo._listEvent )
        {
            appendProperty( event._annotation, "Event" );
        }
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            AnnotationOutput output     = AnnotationOutput::collectEditor( method );
            const string     lookupName = ( method._bConstructor != SW_FALSE ) ? CodeGeneratorInternal::makeCtorLookupName( method, _session ) : method._name;
            CodeGeneratorInternal::appendEditorRows( outListRow, "Function", CodeEmit::quoted( lookupName ), output );
        }
    }

    bool CodeGenerator::emitEditorRowTable( CodeEmit& emit, const string& fqn, const utf8* pKind, const vector<string>& listRow )
    {
        if ( listRow.empty() )
            return false;
        const string id = sanitizeIdentifier( fqn );
        emit.linef( "// ── %#", fqn );
        emit.linef( "static constexpr ::sw::EditorMetadataRow kRows_%#[] =", id );
        emit.line( "{" );
        emit.push();
        for ( const string& row : listRow )
        {
            emit.line( row );
        }
        emit.pop();
        emit.line( "};" );
        emit.linef( "static const ::sw::%#EditorMetadataSource    s_%#_editorMetadata{ %#, kRows_%#, %#u };", pKind, id, CodeEmit::quoted( fqn ), id,
                    static_cast<uint32>( listRow.size() ) );
        emit.linef( "static const ::sw::%#EditorMetadataRegistrar s_%#_editorMetadataRegistrar{ &s_%#_editorMetadata };", pKind, id, id );
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
