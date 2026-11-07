#include "pch.h"

#include "Programs/ReflectionParser/FixedTypeAnalyzer.h"

#include "Core/Container/StringUtil.h"

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

    bool FixedTypeAnalyzer::isFixedStringType( const string_view normalizedName, const string_view canonicalSpelling ) noexcept
    {
        return normalizedName == "string" || StringUtil::startsWith( canonicalSpelling, "sw::basic_string<char>" ) ||
               StringUtil::startsWith( canonicalSpelling, "std::basic_string<char," ) || canonicalSpelling == "std::basic_string<char>";
    }

    shared_ptr<ParsedFixedEnum> FixedTypeAnalyzer::collectFixedEnum( const CXCursor declaration, const ParserSession& session )
    {
        shared_ptr<ParsedFixedEnum> result = make_shared<ParsedFixedEnum>();
        result->_fullyQualifiedName        = CursorReader::makeFullyQualifiedName( declaration );
        result->_underlyingTypeName =
            session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( clang_getCanonicalType( clang_getEnumDeclIntegerType( declaration ) ) ) ) );
        CursorReader::collectEnumerators( declaration, result->_listEnumerator );

        string spelling = AnnotationLookup::extractSourceAnnotation( declaration, annotation::kEnum, session._config );
        if ( spelling.empty() )
            spelling = AnnotationLookup::findAnnotateAttr( declaration, annotation::kEnumPrefix );
        if ( spelling.empty() )
            return result;
        ParsedEnumInfo annotated;
        vector<string> listUnknownToken; // 그 열거형의 헤더를 수집할 때 알립니다. 여기서 다시 알리지 않습니다
        sw::AnnotationApply::apply( spelling, annotated, session._annotationMeta, listUnknownToken );
        result->_countEnumerator = string( ParserUtil::scopeLeaf( annotated._countEnumerator ) );
        result->_bIsBitFlag      = annotated._bIsBitFlag;
        return result;
    }

    shared_ptr<ParsedFixedType> FixedTypeAnalyzer::classifyFixedType( const CXType type, const ParserSession& session, const int32 depth )
    {
        shared_ptr<ParsedFixedType> result            = make_shared<ParsedFixedType>();
        const CXType                canonical         = clang_getCanonicalType( type );
        const string                canonicalSpelling = CursorReader::takeString( clang_getTypeSpelling( canonical ) );
        result->_typeName                             = session._typeNameMap.normalize( CursorReader::takeString( clang_getTypeSpelling( type ) ) );
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
            return makeIneligibleFixedType( result, "type '" + result->_typeName + "' is outside the fixed layout encoding table" );

        if ( isFixedStringType( result->_typeName, canonicalSpelling ) )
        {
            result->_kind = FixedValueKind::String;
            return result;
        }
        if ( canonicalSpelling == "sw::DynamicBitset" )
        {
            result->_kind = FixedValueKind::Bitset;
            return result;
        }
        for ( const FixedMathTypeDef& math : fixedlayout::kArrMathType )
        {
            if ( result->_typeName == math._pName )
            {
                result->_kind = FixedValueKind::Math;
                return result;
            }
        }
        // 압축 타입은 인코딩 표 밖이다. 적격이 아닌 것으로 조용히 넘기지 않도록 표시를 달아 코덱 생성기가 오류로 멈추게 한다.
        if ( session._typeNameMap.isPacked( result->_typeName ) )
        {
            result->_bPacked = SW_TRUE;
            return makeIneligibleFixedType( result, "packed type '" + result->_typeName + "' has no fixed layout encoding" );
        }
        // `sw::vector` 는 자체 클래스라 정규 이름이 `sw::vector<…>` 입니다(`SW_ENABLE_STL_CONTAINER` 면 `std::vector` 별칭입니다).
        const bool bVector = StringUtil::startsWith( canonicalSpelling, "std::vector<" ) || StringUtil::startsWith( canonicalSpelling, "sw::vector<" );
        const bool bArray  = StringUtil::startsWith( canonicalSpelling, "std::array<" ) || StringUtil::startsWith( canonicalSpelling, "sw::array<" );
        if ( bVector || bArray )
        {
            result->_kind    = bVector ? FixedValueKind::Vector : FixedValueKind::FixedArray;
            result->_element = classifyFixedType( clang_Type_getTemplateArgumentAsType( canonical, 0 ), session, depth + 1 );
            return result;
        }
        const CXCursor declaration = clang_getTypeDeclaration( canonical );
        if ( AnnotationLookup::isReflectedRecord( declaration, session._config ) )
        {
            result->_kind               = FixedValueKind::Reflect;
            result->_fullyQualifiedName = CursorReader::makeFullyQualifiedName( declaration );
            return result;
        }
        return makeIneligibleFixedType( result, "type '" + result->_typeName + "' is outside the fixed layout encoding table" );
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
