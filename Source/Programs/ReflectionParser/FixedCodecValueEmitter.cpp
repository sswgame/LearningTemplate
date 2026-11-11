#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecValueEmitter.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Programs/ReflectionParser/FixedCodecBoundResolver.h"
#include "Programs/ReflectionParser/ParserDefines.h"

#include <limits>

namespace sw
{
    const FixedMathTypeDef* FixedCodecValueEmitter::findMathType( const string_view typeName )
    {
        for ( const FixedMathTypeDef& math : fixedlayout::kArrMathType )
        {
            if ( typeName == math._pName )
                return &math;
        }
        return nullptr;
    }

    vector<string> FixedCodecValueEmitter::splitMembers( const string_view memberList )
    {
        vector<string>        listMember;
        const string_splitter parts( memberList, { "," } );
        for ( const string_view part : parts.getSplitList() )
        {
            listMember.emplace_back( StringUtil::trim( part ) );
        }
        return listMember;
    }

    const ParsedFixedType& FixedCodecValueEmitter::findLeafType( const ParsedFixedType& type )
    {
        const ParsedFixedType* pType = &type;
        while ( pType->_element != nullptr )
        {
            const bool bContainer = pType->_kind == FixedValueKind::Vector || pType->_kind == FixedValueKind::FixedArray;
            if ( bContainer == false )
                break;
            pType = pType->_element.get();
        }
        return *pType;
    }

    int32 FixedCodecValueEmitter::countVariableLengthLevels( const ParsedFixedType& type )
    {
        const bool bVariable = type._kind == FixedValueKind::Vector || type._kind == FixedValueKind::String || type._kind == FixedValueKind::Bitset;
        int32      count     = bVariable ? 1 : 0;
        if ( type._element != nullptr )
            count += countVariableLengthLevels( *type._element );
        return count;
    }

    string FixedCodecValueEmitter::findIneligibleReason( const ParsedFixedType& type )
    {
        if ( type._kind == FixedValueKind::Ineligible )
            return type._reason;
        if ( type._element != nullptr )
            return findIneligibleReason( *type._element );
        if ( type._kind == FixedValueKind::Vector || type._kind == FixedValueKind::FixedArray )
            return "container element type is unknown";
        return {};
    }

    void FixedCodecValueEmitter::appendEnumCanonical( string& out, const ParsedFixedEnum& enumInfo )
    {
        out += "enum{" + enumInfo._fullyQualifiedName + ";" + enumInfo._underlyingTypeName + ";";
        out += enumInfo._bIsBitFlag == SW_TRUE ? "flags" : "plain";
        if ( enumInfo._countEnumerator.empty() == false )
            out += ";count=" + enumInfo._countEnumerator;
        for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
        {
            out += ";" + enumerator._name + "=" + FixedCodecEmitState::formatInteger( enumerator._value );
        }
        out += "}";
    }

    void FixedCodecValueEmitter::appendFieldHead( string& out, const ParsedPropertyInfo& prop )
    {
        out += "F=" + prop._memberName + ":" + prop._typeName;
        if ( prop._bIsBitField == SW_TRUE )
            out += ":bit";
        if ( prop._minRangeExpr._text.empty() == false )
            out += ":min=" + prop._minRangeExpr._text;
        if ( prop._maxRangeExpr._text.empty() == false )
            out += ":max=" + prop._maxRangeExpr._text;
        if ( prop._bHasStep == SW_TRUE )
            out += ":step=" + FixedCodecEmitState::formatFloat( static_cast<float64>( prop._step ) );
        if ( prop._bHasMaxCount == SW_TRUE )
            out += ":maxcount=" + FixedCodecEmitState::formatInteger( static_cast<int64>( prop._maxCount ) );
        out += ":";
    }

    void FixedCodecValueEmitter::emitInteger( FixedCodecEmitState& state, const FixedCodecSite& site, const FixedCodecBound& minBound, const FixedCodecBound& maxBound )
    {
        const string                              minExpr = FixedCodecBoundResolver::makeIntegerBound( minBound, true );
        const string                              maxExpr = FixedCodecBoundResolver::makeIntegerBound( maxBound, false );
        const bool                                bRanged = minBound._bPresent == SW_TRUE && maxBound._bPresent == SW_TRUE;
        const utf8*                               pWrite  = bRanged ? "writeRangedInteger" : "writeVarInteger";
        const utf8*                               pRead   = bRanged ? "readRangedInteger" : "readVarInteger";
        StringBuilder<constant::buffer::kMax1024> call;
        call.appendFormat( "::sw::FixedCodecUtil::%#( writer, %#, %#, %# )", pWrite, site._expr, minExpr, maxExpr );
        state.emitWriteCheck( string( call.view() ) );
        call.clear();
        call.appendFormat( "::sw::FixedCodecUtil::%#( reader, %#, %#, %# )", pRead, minExpr, maxExpr, site._expr );
        state.emitReadCheck( string( call.view() ) );
        state.emitEqualsCheck( site._exprA + " != " + site._exprB );
    }

    void FixedCodecValueEmitter::emitFloat( FixedCodecEmitState& state, const FixedCodecSite& site, const FixedValueKind kind, const FixedCodecBound& minBound, const FixedCodecBound& maxBound )
    {
        const ParsedPropertyInfo& prop = *site._pProp;
        if ( prop._bHasStep == SW_TRUE )
        {
            const string minExpr  = FixedCodecBoundResolver::makeFloatBound( minBound, true, "float32" );
            const string maxExpr  = FixedCodecBoundResolver::makeFloatBound( maxBound, false, "float32" );
            const string stepExpr = "static_cast<float32>( " + FixedCodecEmitState::formatFloat( static_cast<float64>( prop._step ) ) + " )";
            state.emitWriteCheck( "::sw::FixedCodecUtil::writeQuantizedFloat32( writer, " + site._expr + ", " + minExpr + ", " + maxExpr + ", " + stepExpr + " )" );
            state.emitReadCheck( "::sw::FixedCodecUtil::readQuantizedFloat32( reader, " + minExpr + ", " + maxExpr + ", " + stepExpr + ", " + site._expr + " )" );
        }
        else
        {
            const string minExpr  = FixedCodecBoundResolver::makeFloatBound( minBound, true, "float64" );
            const string maxExpr  = FixedCodecBoundResolver::makeFloatBound( maxBound, false, "float64" );
            const bool   b32      = kind == FixedValueKind::Float32;
            const bool   bBounded = minBound._bPresent == SW_TRUE || maxBound._bPresent == SW_TRUE;
            if ( bBounded )
            {
                const utf8* pSuffix = b32 ? "Float32" : "Float64";
                state.emitWriteCheck( string( "::sw::FixedCodecUtil::writeBounded" ) + pSuffix + "( writer, " + site._expr + ", " + minExpr + ", " + maxExpr + " )" );
                state.emitReadCheck( string( "::sw::FixedCodecUtil::readBounded" ) + pSuffix + "( reader, " + minExpr + ", " + maxExpr + ", " + site._expr + " )" );
            }
            else if ( b32 )
            {
                state._write.linef( "writer.writeFloat( %# );", site._expr );
                // 범위 없는 실수는 비트 그대로 왕복한다(NaN 포함 — `isBitEqual` 과 같은 뜻). 유한 검사는 범위를 적은 필드의 몫이다.
                state._read.linef( "%# = reader.readFloatBits();", site._expr );
            }
            else
            {
                state._write.linef( "::sw::FixedCodecUtil::writeFloat64( writer, %# );", site._expr );
                state._read.linef( "%# = ::sw::FixedCodecUtil::readFloat64( reader );", site._expr );
            }
        }
        state.emitEqualsCheck( "::sw::FixedCodecUtil::isBitEqual( " + site._exprA + ", " + site._exprB + " ) == false" );
    }

    void FixedCodecValueEmitter::emitEnum( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedEnum& enumInfo )
    {
        state.addEnum( enumInfo );
        const string helper   = FixedCodecEmitState::makeEnumHelperName( enumInfo );
        const string enumType = "::" + enumInfo._fullyQualifiedName;
        state.emitWriteCheck( helper + "( " + site._expr + " )" );

        if ( enumInfo._bIsBitFlag == SW_FALSE )
        {
            // 범위는 [가장 작은 열거자, Count - 1] 이고, `Count` 가 없으면 [가장 작은 열거자, 가장 큰 열거자] 입니다. 기반 타입 전체 폭으로
            // 쓰지 않습니다 — 열거자 셋에 8 비트를 쓰고, 그 폭은 선언에 `Count` 를 적었는지라는 우연으로 갈립니다(언리얼도 열거형 프로퍼티를
            // 가장 큰 열거자 값의 비트 수로 복제합니다). 어느 열거자가 끝인지는 파서의 값으로 고르고 값은 컴파일러가 계산합니다.
            const ParsedEnumeratorInfo* pMin = nullptr;
            const ParsedEnumeratorInfo* pMax = nullptr;
            for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
            {
                if ( enumerator._name == enumInfo._countEnumerator )
                    continue;
                if ( pMin == nullptr || enumerator._value < pMin->_value )
                    pMin = &enumerator;
                if ( pMax == nullptr || enumerator._value > pMax->_value )
                    pMax = &enumerator;
            }
            const uint64 width = static_cast<uint64>( pMax->_value ) - static_cast<uint64>( pMin->_value );
            if ( width > static_cast<uint64>( std::numeric_limits<uint32>::max() ) )
            {
                // 범위 비트는 32 비트까지입니다. 넘는 열거형을 실행 중 쓰기 실패로 두지 않고 생성에서 멈춥니다.
                state.reportError( *site._pProp, "enum '" + enumInfo._fullyQualifiedName + "' spans more than 32 bits of values" );
                return;
            }
            const string minExpr = "static_cast<int64>( " + enumType + "::" + pMin->_name + " )";
            const string maxExpr = enumInfo._countEnumerator.empty() ? "static_cast<int64>( " + enumType + "::" + pMax->_name + " )"
                                                                     : "static_cast<int64>( " + enumType + "::" + enumInfo._countEnumerator + " ) - 1";
            state.emitWriteCheck( "::sw::FixedCodecUtil::writeRangedInteger( writer, static_cast<int64>( " + site._expr + " ), " + minExpr + ", " + maxExpr + " )" );
            const string value = "value" + FixedCodecEmitState::formatInteger( site._depth );
            state._read.line( "{" ).push();
            state._read.linef( "int64 %# = 0;", value );
            state.emitReadCheck( "::sw::FixedCodecUtil::readRangedInteger( reader, " + minExpr + ", " + maxExpr + ", " + value + " )" );
            state._read.linef( "if ( %#( static_cast< %# >( %# ) ) == false )", helper, enumType, value );
            state._read.push().linef( "return %#;", FixedCodecEmitState::kReadOutOfRange ).pop();
            state._read.linef( "%# = static_cast< %# >( %# );", site._expr, enumType, value );
            state._read.pop().line( "}" );
        }
        else
        {
            // 비트 플래그는 켜진 비트의 조합이라 범위가 없습니다. 기반 타입 폭으로 쓰고, 등록된 비트 밖은 위의 검사 함수가 거절합니다.
            state._write.linef( "::sw::FixedCodecUtil::writeEnumBits( writer, %# );", site._expr );
            const string value = "value" + FixedCodecEmitState::formatInteger( site._depth );
            state._read.line( "{" ).push();
            state._read.linef( "const %# %# = ::sw::FixedCodecUtil::readEnumBits< %# >( reader );", enumType, value, enumType );
            state._read.linef( "if ( %#( %# ) == false )", helper, value );
            state._read.push().linef( "return %#;", FixedCodecEmitState::kReadOutOfRange ).pop();
            state._read.linef( "%# = %#;", site._expr, value );
            state._read.pop().line( "}" );
        }
        state.emitEqualsCheck( site._exprA + " != " + site._exprB );
    }

    FixedCodecSite FixedCodecValueEmitter::makeChildSite( const FixedCodecSite& parent, const string& expr, const string& exprA, const string& exprB )
    {
        FixedCodecSite child = parent;
        child._expr          = expr;
        child._exprA         = exprA;
        child._exprB         = exprB;
        child._depth         = parent._depth + 1;
        child._bBitField     = SW_FALSE;
        return child;
    }

    void FixedCodecValueEmitter::emitContainer( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical )
    {
        const ParsedPropertyInfo& prop     = *site._pProp;
        const string              depth    = FixedCodecEmitState::formatInteger( site._depth );
        const string              element  = "element" + depth;
        const string              index    = "index" + depth;
        const bool                bVector  = type._kind == FixedValueKind::Vector;
        const string              maxCount = FixedCodecEmitState::formatInteger( static_cast<int64>( prop._maxCount ) ) + "u";
        if ( bVector )
        {
            state.emitWriteCheck( "::sw::FixedCodecUtil::writeCount( writer, " + site._expr + ".size(), " + maxCount + " )" );
            const string count = "count" + depth;
            state._read.line( "{" ).push();
            state._read.linef( "uint32 %# = 0;", count );
            state.emitReadCheck( "::sw::FixedCodecUtil::readCount( reader, " + maxCount + ", " + count + " )" );
            state._read.linef( "%#.resize( %# );", site._expr, count );
            state._read.pop().line( "}" );
            state.emitEqualsCheck( site._exprA + ".size() != " + site._exprB + ".size()" );
            state._equals.linef( "for ( size_t %# = 0; %# < %#.size(); ++%# )", index, index, site._exprA, index );
        }
        else
        {
            state._equals.linef( "for ( size_t %# = 0; %# < std::size( %# ); ++%# )", index, index, site._exprA, index );
        }
        state._write.linef( "for ( const auto& %# : %# )", element, site._expr );
        state._read.linef( "for ( auto&& %# : %# )", element, site._expr );
        state._write.line( "{" ).push();
        state._read.line( "{" ).push();
        state._equals.line( "{" ).push();
        outCanonical += bVector ? "vec<" : "arr<";
        const FixedCodecSite child = makeChildSite( site, element, site._exprA + "[" + index + "]", site._exprB + "[" + index + "]" );
        emitValue( state, child, *type._element, outCanonical );
        outCanonical += ">";
        state._write.pop().line( "}" );
        state._read.pop().line( "}" );
        state._equals.pop().line( "}" );
    }

    const ParsedTypeInfo* FixedCodecValueEmitter::findHeaderType( const FixedCodecEmitState& state, const string& fqn )
    {
        for ( const ParsedTypeInfo& candidate : state._pHeader->_listType )
        {
            if ( candidate._fullyQualifiedName == fqn )
                return &candidate;
        }
        return nullptr;
    }

    bool FixedCodecValueEmitter::canInline( const FixedCodecEmitState& state, const ParsedTypeInfo* pNested )
    {
        if ( pNested == nullptr )
            return false;
        const bool bHasExtraLogic = pNested->_baseCount != 0 || pNested->_validate.empty() == false;
        if ( bHasExtraLogic )
            return false;
        if ( std::find( state._listInlineFqn.begin(), state._listInlineFqn.end(), pNested->_fullyQualifiedName ) != state._listInlineFqn.end() )
            return false;
        for ( const ParsedPropertyInfo& prop : pNested->_listProperty )
        {
            if ( prop._bTransient == SW_FALSE && prop._bPublic == SW_FALSE )
                return false;
        }
        return true;
    }

    void FixedCodecValueEmitter::emitReflect( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical )
    {
        const string&         fqn     = type._fullyQualifiedName;
        const ParsedTypeInfo* pNested = findHeaderType( state, fqn );
        if ( canInline( state, pNested ) )
        {
            outCanonical += "inline{T=" + fqn + "|";
            state._listInlineFqn.push_back( fqn );
            emitProperties( state, *pNested, site._expr, site._exprA, site._exprB, site._depth + 1, outCanonical );
            state._listInlineFqn.pop_back();
            outCanonical += "}";
            // 펼친 타입이 혼자서는 풀지 못하던 바깥 범위 이름을 여기서 풀었습니다. 그 타입의 "바깥 이름" 오류는 사라집니다.
            if ( state._bError == SW_FALSE )
                state._listResolvedFqn.push_back( fqn );
            return;
        }

        outCanonical += "ref{" + fqn + "}";
        const bool   bSelf = fqn == state._pRoot->_fullyQualifiedName;
        const string codec = bSelf ? string() : FixedCodecEmitState::makeCodecAccessorName( fqn ) + "()";
        if ( bSelf == false )
            state.addDependency( site._pProp->_memberName, fqn );
        const string writeCall  = bSelf ? string( "fixedWrite" ) : codec + "._pWrite";
        const string readCall   = bSelf ? string( "fixedRead" ) : codec + "._pRead";
        const string equalsCall = bSelf ? string( "fixedEquals" ) : codec + "._pEquals";
        state.emitWriteCheck( writeCall + "( writer, &" + site._expr + " )" );
        state.emitReadCheck( readCall + "( reader, &" + site._expr + " )" );
        state.emitEqualsCheck( equalsCall + "( &" + site._exprA + ", &" + site._exprB + " ) == false" );
    }

    void FixedCodecValueEmitter::emitValue( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical )
    {
        switch ( type._kind )
        {
            case FixedValueKind::Ineligible:
            {
                break;
            }
            case FixedValueKind::Bool:
            {
                outCanonical += "bool";
                if ( site._bBitField == SW_TRUE )
                {
                    state._write.linef( "writer.writeBool( %# != 0 );", site._expr );
                    state._read.linef( "%# = static_cast<decltype( %# )>( reader.readBool() ? 1 : 0 );", site._expr, site._expr );
                }
                else
                {
                    state._write.linef( "writer.writeBool( %# );", site._expr );
                    state._read.linef( "%# = reader.readBool();", site._expr );
                }
                state.emitEqualsCheck( site._exprA + " != " + site._exprB );
                break;
            }
            case FixedValueKind::Integer:
            {
                outCanonical += "int";
                emitInteger( state, site, FixedCodecBoundResolver::pickBound( site._listMin, 0 ), FixedCodecBoundResolver::pickBound( site._listMax, 0 ) );
                break;
            }
            case FixedValueKind::Float32:
            case FixedValueKind::Float64:
            {
                outCanonical += type._kind == FixedValueKind::Float32 ? "f32" : "f64";
                emitFloat( state, site, type._kind, FixedCodecBoundResolver::pickBound( site._listMin, 0 ), FixedCodecBoundResolver::pickBound( site._listMax, 0 ) );
                break;
            }
            case FixedValueKind::Math:
            {
                const FixedMathTypeDef* pMath      = findMathType( type._typeName );
                const vector<string>    listMember = splitMembers( pMath->_pMemberList );
                outCanonical += "math(" + type._typeName + ")";
                for ( size_t memberIndex = 0; memberIndex < listMember.size(); ++memberIndex )
                {
                    const string&        member = listMember[memberIndex];
                    const FixedCodecSite child  = makeChildSite( site, site._expr + "." + member, site._exprA + "." + member, site._exprB + "." + member );
                    emitFloat( state, child, FixedValueKind::Float32, FixedCodecBoundResolver::pickBound( site._listMin, memberIndex ), FixedCodecBoundResolver::pickBound( site._listMax, memberIndex ) );
                }
                break;
            }
            case FixedValueKind::Enum:
            {
                appendEnumCanonical( outCanonical, *type._enum );
                emitEnum( state, site, *type._enum );
                break;
            }
            case FixedValueKind::String:
            {
                outCanonical += "str";
                const string maxCount = FixedCodecEmitState::formatInteger( static_cast<int64>( site._pProp->_maxCount ) ) + "u";
                state.emitWriteCheck( "::sw::FixedCodecUtil::writeString( writer, " + site._expr + ", " + maxCount + " )" );
                state.emitReadCheck( "::sw::FixedCodecUtil::readString( reader, " + maxCount + ", " + site._expr + " )" );
                state.emitEqualsCheck( site._exprA + " != " + site._exprB );
                break;
            }
            case FixedValueKind::Bitset:
            {
                outCanonical += "bits";
                const string maxCount = FixedCodecEmitState::formatInteger( static_cast<int64>( site._pProp->_maxCount ) ) + "u";
                state.emitWriteCheck( "::sw::FixedCodecUtil::writeBitset( writer, " + site._expr + ", " + maxCount + " )" );
                state.emitReadCheck( "::sw::FixedCodecUtil::readBitset( reader, " + maxCount + ", " + site._expr + " )" );
                state.emitEqualsCheck( site._exprA + " != " + site._exprB );
                break;
            }
            case FixedValueKind::Vector:
            case FixedValueKind::FixedArray:
            {
                emitContainer( state, site, type, outCanonical );
                break;
            }
            case FixedValueKind::Reflect:
            {
                emitReflect( state, site, type, outCanonical );
                break;
            }
        }
    }

    bool FixedCodecValueEmitter::hasEnumValues( const ParsedFixedEnum& enumInfo )
    {
        for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
        {
            if ( enumerator._name != enumInfo._countEnumerator )
                return true;
        }
        return false;
    }

    void FixedCodecValueEmitter::emitProperty( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& objectExpr, const string& exprA, const string& exprB, const int32 depth, string& outCanonical )
    {
        if ( prop._bTransient == SW_TRUE )
            return;
        if ( prop._fixedType == nullptr )
        {
            state.markIneligible( prop, "field type was not classified" );
            return;
        }
        const ParsedFixedType& type     = *prop._fixedType;
        const ParsedFixedType& leaf     = findLeafType( type );
        const FixedValueKind   leafKind = leaf._kind;
        const int32            variable = countVariableLengthLevels( type );
        const bool             bNumeric = leafKind == FixedValueKind::Integer || leafKind == FixedValueKind::Float32 || leafKind == FixedValueKind::Float64 ||
                              leafKind == FixedValueKind::Math;
        const bool              bHasRange    = prop._minRangeExpr._text.empty() == false || prop._maxRangeExpr._text.empty() == false;
        const FixedMathTypeDef* pMath        = leafKind == FixedValueKind::Math ? findMathType( leaf._typeName ) : nullptr;
        const size_t            elementCount = pMath != nullptr ? splitMembers( pMath->_pMemberList ).size() : 1;

        // 1) 키 검사. 생성 오류입니다
        const bool bRangeOnNonNumeric = bHasRange && bNumeric == false && leafKind != FixedValueKind::Ineligible;
        if ( bRangeOnNonNumeric )
            state.reportError( prop, "Min/Max apply to integer, floating-point and math fields only" );
        if ( prop._bHasStep == SW_TRUE )
        {
            const bool bFloat32 = leafKind == FixedValueKind::Float32 || leafKind == FixedValueKind::Math;
            if ( bFloat32 == false )
                state.reportError( prop, "Step applies to float32 fields (and float32 math types) only" );
            else if ( ( prop._step > 0.0f ) == false )
                state.reportError( prop, "Step must be greater than zero" );
            else if ( prop._minRangeExpr._text.empty() || prop._maxRangeExpr._text.empty() )
                state.reportError( prop, "Step needs both Min and Max" );
        }
        if ( prop._bHasMaxCount == SW_TRUE && variable == 0 )
            state.reportError( prop, "MaxCount applies to string, vector and DynamicBitset fields only" );

        FixedCodecSite site;
        site._pProp     = &prop;
        site._expr      = objectExpr + "." + prop._memberName;
        site._exprA     = exprA + "." + prop._memberName;
        site._exprB     = exprB + "." + prop._memberName;
        site._depth     = depth;
        site._bBitField = prop._bIsBitField;
        site._listMin.emplace_back();
        site._listMax.emplace_back();
        if ( bNumeric )
        {
            const bool bInteger = leafKind == FixedValueKind::Integer;
            if ( FixedCodecBoundResolver::resolveBounds( state, prop, prop._minRangeExpr._text, bInteger, site._listMin ) == false ||
                 FixedCodecBoundResolver::resolveBounds( state, prop, prop._maxRangeExpr._text, bInteger, site._listMax ) == false )
                return;
            const bool bMinCountMismatch = site._listMin.size() != 1 && site._listMin.size() != elementCount;
            const bool bMaxCountMismatch = site._listMax.size() != 1 && site._listMax.size() != elementCount;
            if ( bMinCountMismatch || bMaxCountMismatch )
            {
                state.reportError( prop, "Min/Max list has " + FixedCodecEmitState::formatInteger( static_cast<int64>( site._listMin.size() ) ) + "/" + FixedCodecEmitState::formatInteger( static_cast<int64>( site._listMax.size() ) ) + " terms but the value has " + FixedCodecEmitState::formatInteger( static_cast<int64>( elementCount ) ) + " elements" );
                return;
            }
        }

        // 압축 타입(half · unorm · snorm · OctahedralNormal · RGB10A2)은 인코딩 표에 없다. 포인터처럼 적격이 아닌 것과 달리 저장용으로 만든 타입이라
        // 코덱 없이 등록되면 넷 · 롤백에서야 드러나므로 여기서 멈춘다. 코덱에 실으려면 표(`fixedlayout::kArrMathType`)가 아니라 새 인코딩 종류를 더한다.
        if ( leaf._bPacked == SW_TRUE )
        {
            state.reportError( prop, "packed type '" + leaf._typeName + "' is not in the fixed layout encoding table - mark the field Transient or add a packed encoding to the generator" );
            return;
        }

        // 2) 적격 판정. 오류가 아닙니다
        const string reason = findIneligibleReason( type );
        if ( reason.empty() == false )
        {
            state.markIneligible( prop, reason );
            return;
        }
        if ( variable > 1 )
        {
            state.markIneligible( prop, "nested variable-length values (vector of vector or string) need a MaxCount each" );
            return;
        }
        if ( variable == 1 && prop._bHasMaxCount == SW_FALSE )
        {
            state.markIneligible( prop, "variable-length value without MaxCount" );
            return;
        }
        if ( leafKind == FixedValueKind::Enum && hasEnumValues( *leaf._enum ) == false )
        {
            state.markIneligible( prop, "enum has no enumerators" );
            return;
        }

        appendFieldHead( outCanonical, prop );
        emitValue( state, site, type, outCanonical );
        outCanonical += "|";
    }

    void FixedCodecValueEmitter::emitProperties( FixedCodecEmitState& state, const ParsedTypeInfo& type, const string& objectExpr, const string& exprA, const string& exprB, const int32 depth, string& outCanonical )
    {
        state._listScope.push_back( FixedCodecScopeFrame{ &type, objectExpr, 0 } );
        for ( size_t index = 0; index < type._listProperty.size(); ++index )
        {
            state._listScope.back()._limit = index;
            emitProperty( state, type._listProperty[index], objectExpr, exprA, exprB, depth, outCanonical );
        }
        state._listScope.pop_back();
    }
} // namespace sw
