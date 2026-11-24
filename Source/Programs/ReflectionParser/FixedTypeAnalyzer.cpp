#include "pch.h"

#include "Programs/ReflectionParser/FixedTypeAnalyzer.h"

#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/AnnotationApply.h"
#include "Programs/ReflectionParser/AnnotationLookup.h"
#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserUtil.h"
#include "Programs/ReflectionParser/TypeNameMap.h"

namespace sw
{
    shared_ptr<ParsedFixedType> FixedTypeAnalyzer::makeIneligibleFixedType( shared_ptr<ParsedFixedType> type, const string_view reason )
    {
        type->_kind   = FixedValueKind::Ineligible;
        type->_reason = string( reason );
        return type;
    }

    bool FixedTypeAnalyzer::isFixedIntegerKind( const CXTypeKind kind ) noexcept
    {
        return kind == CXType_Char_S || kind == CXType_SChar || kind == CXType_Char_U || kind == CXType_UChar || kind == CXType_Short ||
               kind == CXType_UShort || kind == CXType_Int || kind == CXType_UInt || kind == CXType_Long || kind == CXType_ULong ||
               kind == CXType_LongLong || kind == CXType_ULongLong;
    }

    bool FixedTypeAnalyzer::isFixedPointerKind( const CXTypeKind kind ) noexcept
    {
        return kind == CXType_Pointer || kind == CXType_LValueReference || kind == CXType_RValueReference || kind == CXType_MemberPointer ||
               kind == CXType_BlockPointer || kind == CXType_ObjCObjectPointer;
    }

    shared_ptr<ParsedFixedEnum> FixedTypeAnalyzer::collectFixedEnum( const CXCursor declaration, const ParserSession& session )
    {
        shared_ptr<ParsedFixedEnum> result = make_shared<ParsedFixedEnum>();
        result->_fullyQualifiedName        = CursorReader::makeFullyQualifiedName( declaration );
        result->_underlyingTypeName =
            session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getCanonicalType( clang_getEnumDeclIntegerType( declaration ) ) ) ) );
        CursorReader::collectEnumerators( declaration, result->_listEnumerator );

        // 칸의 열거형은 다른 헤더에 있을 수 있다 — 선언의 부모를 따라가 바로 앞 표식을 읽는다.
        const string spelling = AnnotationLookup::findPreviousSiblingMarker( declaration, annotation::kEnum );
        if ( spelling.empty() )
            return result;
        ParsedEnumInfo annotated;
        vector<string> listUnknownToken; // 그 열거형의 헤더를 수집할 때 알립니다. 여기서 다시 알리지 않습니다
        sw::AnnotationApply::apply( spelling, annotated, listUnknownToken );
        result->_bIsBitFlag      = annotated._bIsBitFlag;
        result->_countEnumerator = CursorReader::resolveCountEnumerator( ParserUtil::scopeLeaf( annotated._countEnumerator ), result->_listEnumerator, annotated._bIsBitFlag != SW_FALSE );
        return result;
    }

    shared_ptr<ParsedFixedType> FixedTypeAnalyzer::classifyFixedType( const CXType type, const ParserSession& session, const int32 depth )
    {
        shared_ptr<ParsedFixedType> result    = make_shared<ParsedFixedType>();
        const CXType                canonical = clang_getCanonicalType( type );
        result->_typeName                     = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( type ) ) );
        if ( depth > kMaxFixedTypeDepth )
            return makeIneligibleFixedType( result, "containers nested too deeply" );

        const CXTypeKind kind = canonical.kind;
        if ( kind == CXType_Bool )
        {
            result->_kind = FixedValueKind::Bool;
            return result;
        }
        if ( isFixedIntegerKind( kind ) )
        {
            result->_kind = FixedValueKind::Integer;
            return result;
        }
        if ( kind == CXType_Float || kind == CXType_Double )
        {
            result->_kind = ( kind == CXType_Float ) ? FixedValueKind::Float32 : FixedValueKind::Float64;
            return result;
        }
        if ( kind == CXType_Enum )
        {
            result->_kind = FixedValueKind::Enum;
            result->_enum = collectFixedEnum( clang_getTypeDeclaration( canonical ), session );
            return result;
        }
        if ( kind == CXType_ConstantArray )
        {
            result->_kind    = FixedValueKind::FixedArray;
            result->_element = classifyFixedType( clang_getArrayElementType( canonical ), session, depth + 1 );
            return result;
        }
        if ( isFixedPointerKind( kind ) )
            return makeIneligibleFixedType( result, "pointer or reference" );
        if ( kind != CXType_Record )
        {
            StringBuilder<constant::buffer::kMax512> reason;
            reason.appendFormat( "type '%#' is outside the fixed layout encoding table", result->_typeName );
            return makeIneligibleFixedType( result, reason.view() );
        }

        // 형식의 정체는 선언의 FQN(템플릿이면 기본 템플릿)으로 가른다 — 정규 철자의 머리를 견주면 표준 라이브러리 구현(인라인 이름공간 · 기본 인자)마다 철자가 다르다.
        const string identity = CursorReader::getTypeIdentity( canonical );
        // 글은 문자 원소가 `char` 인 문자열 템플릿이다 — 넓은 문자(`wchar_t` · `char16_t`) 문자열은 인코딩 표 밖이다.
        if ( CursorReader::isTextStringType( canonical ) )
        {
            result->_kind = FixedValueKind::String;
            return result;
        }
        if ( identity == typeidentity::kDynamicBitset )
        {
            result->_kind = FixedValueKind::Bitset;
            return result;
        }
        // 성분 규칙은 `ReflectBuiltins.xxx` 의 Layout 칸이 정본이다 — 핸들처럼 모양이 같아도 뜻이 다른 타입은 Layout 이 none 이라 섞이지 않는다.
        // `int2` 는 `packed` 줄이지만 메모리는 int32 둘이라(Layout i32) 압축 타입 판정보다 먼저 잡혀 정수 칸 둘로 실린다(언리얼 `FIntPoint` 복제와 같은 꼴).
        if ( const BuiltinTypeTraits* pTraits = session._typeNameMap.findTraits( result->_typeName ) )
        {
            const bool bMath = pTraits->_layout == BuiltinLayoutKind::Float32 || pTraits->_layout == BuiltinLayoutKind::Int32;
            if ( bMath )
            {
                result->_kind           = FixedValueKind::Math;
                result->_listMathMember = pTraits->_listMember;
                result->_bIntegerMath   = pTraits->_layout == BuiltinLayoutKind::Int32 ? SW_TRUE : SW_FALSE;
                return result;
            }
            // 각은 메모리가 라디안 float32 하나다 — 압축 타입 표시보다 먼저 float32 칸으로 분류한다(경계 변환은 `FixedCodecValueEmitter::emitProperty`).
            if ( pTraits->_layout == BuiltinLayoutKind::Angle )
            {
                result->_kind   = FixedValueKind::Float32;
                result->_bAngle = SW_TRUE;
                return result;
            }
        }
        // 압축 타입은 인코딩 표 밖이다. 적격이 아닌 것으로 조용히 넘기지 않도록 표시를 달아 코덱 생성기가 오류로 멈추게 한다.
        if ( session._typeNameMap.isPacked( result->_typeName ) )
        {
            result->_bPacked = SW_TRUE;
            StringBuilder<constant::buffer::kMax512> reason;
            reason.appendFormat( "packed type '%#' has no fixed layout encoding", result->_typeName );
            return makeIneligibleFixedType( result, reason.view() );
        }
        // `sw::vector` 는 두 구성(`SW_ENABLE_STL_CONTAINER`) 모두 sw 의 클래스입니다(안의 저장만 다르다).
        const bool bVector = CursorReader::isAnyIdentity( identity, typeidentity::kArrVectorTemplate );
        const bool bArray  = CursorReader::isAnyIdentity( identity, typeidentity::kArrArrayTemplate );
        if ( bVector || bArray )
        {
            result->_kind    = bVector ? FixedValueKind::Vector : FixedValueKind::FixedArray;
            result->_element = classifyFixedType( clang_Type_getTemplateArgumentAsType( canonical, 0 ), session, depth + 1 );
            return result;
        }
        // `EnumArray<E, T>` 의 본문은 `T[열거자 수]` 와 같다(개수 없이 원소 N 개). 스키마 해시는 칸 철자를 섞어 C 배열과 다르다. 원소는 두 번째 인자다.
        if ( identity == typeidentity::kEnumArray )
        {
            result->_kind    = FixedValueKind::FixedArray;
            result->_element = classifyFixedType( clang_Type_getTemplateArgumentAsType( canonical, 1 ), session, depth + 1 );
            return result;
        }
        const CXCursor declaration = clang_getTypeDeclaration( canonical );
        if ( AnnotationLookup::isReflectedRecord( declaration ) )
        {
            result->_kind               = FixedValueKind::Reflect;
            result->_fullyQualifiedName = CursorReader::makeFullyQualifiedName( declaration );
            return result;
        }
        StringBuilder<constant::buffer::kMax512> reason;
        reason.appendFormat( "type '%#' is outside the fixed layout encoding table", result->_typeName );
        return makeIneligibleFixedType( result, reason.view() );
    }

    void FixedTypeAnalyzer::fillFixedLayoutFacts( const CXCursor cursor, const CXType fieldType, ParsedPropertyInfo& prop, const ParserSession& session )
    {
        uint32 line = 0;
        clang_getExpansionLocation( clang_getCursorLocation( cursor ), nullptr, &line, nullptr, nullptr );
        prop._line      = line;
        prop._bPublic   = clang_getCXXAccessSpecifier( cursor ) == CX_CXXPublic ? SW_TRUE : SW_FALSE;
        prop._fixedType = classifyFixedType( fieldType, session, 0 );
        if ( prop._bIsBitField == SW_FALSE )
            return;
        const FixedValueKind kind = prop._fixedType->_kind;
        if ( kind == FixedValueKind::Bool || kind == FixedValueKind::Integer )
            prop._fixedType->_kind = FixedValueKind::Bool;
        else
            prop._fixedType = makeIneligibleFixedType( prop._fixedType, "bit field of a non-integer type" );
    }
} // namespace sw
