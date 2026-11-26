#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/Config/ParserConfig.h"
#include "Programs/ReflectionParser/Emit/CodeGenerator.h"
#include "Programs/ReflectionParser/Emit/EmitTemplateStore.h"
#include "Programs/ReflectionParser/Parse/AnnotationFields.h"
#include "Programs/ReflectionParser/TypeMap/ContainerTypeMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeNameMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeSpelling.h"

namespace sw
{
    namespace
    {
        struct CodeGeneratorPropertyInternal
        {
            /**
             * @brief `Min` · `Max` 글이 이 타입의 필드 하나(`_halfSize`, `-_halfSize`)이면 그 필드와 부호를 찾습니다.
             * @return 그 필드입니다. 숫자 · 성분 목록 · 이 타입에 없는 이름(바깥 타입의 필드)이면 nullptr 입니다. 그 꼴은 레이아웃 스키마만 씁니다.
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
                const string_view     text = StringUtil::trim( prop._defaultValue );
                const LayoutValueKind kind = prop._layoutType != nullptr ? prop._layoutType->_kind : LayoutValueKind::Ineligible;
                if ( kind == LayoutValueKind::String )
                    return CodeEmit::quoted( text );
                const bool bBareEnumerator = kind == LayoutValueKind::Enum && prop._layoutType->_enum != nullptr && text.find( "::" ) == string_view::npos &&
                                             ( text.empty() == false && ( text.front() < '0' || text.front() > '9' ) && text.front() != '-' );
                if ( bBareEnumerator )
                {
                    StringBuilder<constant::buffer::kMax256> b;
                    b.appendFormat( "::%#::%#", prop._layoutType->_enum->_fullyQualifiedName, text );
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
                value = TypeSpelling::scopeLeaf( value );
                b.appendFormat( "%# %# ::std::decay_t<decltype( %# )>::%#", read, op, read, value );
                return string( b.view() );
            }

            /** @brief 프로퍼티의 선언 타입 식(`decltype( Self::m )`, 접근자면 반환 참조의 대상)입니다. `Self` 는 서술자 함수의 그 타입이다. */
            static string makePropertyDeclType( const ParsedPropertyInfo& prop )
            {
                // 접근자 프로퍼티(값이 객체 밖)는 필드가 없다. 선언 타입은 메서드가 돌려주는 참조의 대상이다.
                StringBuilder<constant::buffer::kMax256> b;
                if ( prop._bIsAccessor == SW_TRUE )
                    b.appendFormat( "std::remove_reference_t<decltype( std::declval<Self&>().%#() )>", prop._memberName );
                else
                    b.appendFormat( "decltype( Self::%# )", prop._memberName );
                return string( b.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
        maxCount.appendFormat( "%#u", prop._bHasMaxCount != SW_FALSE ? prop._maxCount : 0u ); // 상한은 스키마이 지키는 값 그대로다(상수 이름으로 적었으면 `PropertyResolver` 가 푼 값)
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
                            CodeGeneratorPropertyInternal::makeDefaultLiteral( prop ) );
            applyDefault = string( b.view() );
        }
        string editCondition( "nullptr" );
        if ( prop._editCondition.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> b;
            b.appendFormat( "[]( const void* pInstance ) -> bool { const Self& o = *static_cast<const Self*>( pInstance ); return %#; }",
                            CodeGeneratorPropertyInternal::makeEditConditionExpr( typeInfo, prop._editCondition ) );
            editCondition = string( b.view() );
        }

        // 칸 차례는 엔진 `PropertyMetadataDesc` 와 같다. 슬라이더 범위는 에디터 메타데이터다 — 개발 전용 단위가 낸다(`collectTypeEditorRows`).
        // 숫자가 아닌 경계 중 이 타입의 필드 하나를 가리키는 것은 인스턴스에서 읽는 함수다(인스펙터와 읽기 공통 끝이 그 값으로 막는다).
        const string cells = CodeEmit::cellList( {
            { CodeEmit::flagExpr( output._listFlag, "PropertyMetadataFlag" ), "0u" },
            { prop._bHasMinRange != SW_FALSE ? CodeEmit::floatLiteral( prop._minRange ) : string( "0.0f" ), "0.0f" },
            { prop._bHasMaxRange != SW_FALSE ? CodeEmit::floatLiteral( prop._maxRange ) : string( "1.0f" ), "1.0f" },
            { unit, "ReflectUnitKind::None" },
            { string( maxCount.view() ), "0u" },
            { textTable, "nullptr" },
            { string( textCount.view() ), "0u" },
            { applyDefault, "nullptr" },
            { CodeGeneratorPropertyInternal::makeRangeReader( typeInfo, prop._minRangeExpr._text ), "nullptr" },
            { CodeGeneratorPropertyInternal::makeRangeReader( typeInfo, prop._maxRangeExpr._text ), "nullptr" },
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
        emit.linef( "using NestC%#_0 = %#;", propIndex, CodeGeneratorPropertyInternal::makePropertyDeclType( prop ) );
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

    string CodeGenerator::emitPropertyBinding( CodeEmit& emit, const ParsedPropertyInfo& prop, const uint32 propIndex ) const
    {
        const CodeEmitByteScope byteScope{ _emitBytes, emit.buffer(), CodeEmitSection::PropertyBinding };
        string                  aliasTable( "nullptr" );
        if ( prop._listAlias.empty() == false )
        {
            StringBuilder<constant::buffer::kMax64> tableName;
            tableName.appendFormat( "kAlias%#", propIndex );
            emit.textTable( tableName.view(), prop._listAlias );
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
                                CodeGeneratorPropertyInternal::makePropertyDeclType( prop ) );
            else
                b.appendFormat( "[]( void* pInstance, const void* ) { static_cast<Self*>( pInstance )->%#(); }", prop._repNotify );
            repNotify = string( b.view() );
        }
        // const 든 아니든 부를 수 있게 인스턴스를 그 타입으로 돌려 부른다. 프로퍼티 검증은 에디터 데이터 검증이라 배포에는 없다(`SW_DEV_ONLY_FUNCTION`).
        // 그래서 배포는 검증 함수를 참조하지 않고, 본문은 개발 전용 소스(`Dev/`)에 둘 수 있다. 배포에서 도는 읽기 검사는 `REFLECT( ReadValidate )` 다.
        string validate( "nullptr" );
        if ( prop._validate.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> b;
            b.appendFormat( "SW_DEV_ONLY_FUNCTION( ( %# ) )", makeValidateLambda( prop._validate ) );
            validate = string( b.view() );
        }
        // 파서가 측정한 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 측정한다(`PropertyInfo::resolveBitField` 설명).
        string setBit( "nullptr" );
        if ( prop._bIsBitField == SW_TRUE )
        {
            StringBuilder<constant::buffer::kMax256> b;
            b.appendFormat( "[]( void* pInstance ) { static_cast<Self*>( pInstance )->%# = static_cast<%#>( 1 ); }", prop._memberName, CodeGeneratorPropertyInternal::makePropertyDeclType( prop ) );
            setBit = string( b.view() );
        }

        // 칸 차례는 엔진 `PropertyBindingDesc` 와 같다.
        const string cells = CodeEmit::cellList( {
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
            declTypes.append( CodeGeneratorPropertyInternal::makePropertyDeclType( prop ) );
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
            const string                             tail = CodeEmit::cellList( {
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
} // namespace sw
