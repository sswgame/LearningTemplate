#include "pch.h"

#include "ReflectionParser/FixedCodecGenerator.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/string_splitter.h"

#include "ReflectionParser/CodeGenerator.h"
#include "ReflectionParser/ParserDefines.h"

SW_LOG_CALLER( "FixedCodecGenerator" );
namespace sw
{
    namespace
    {
        struct FixedCodecGeneratorInternal
        {
            static constexpr int32       kMemberIndent   = 2;
            static constexpr const utf8* kReadOk         = "::sw::FixedReadResult::Ok";
            static constexpr const utf8* kReadOutOfRange = "::sw::FixedReadResult::OutOfRange";
            static constexpr const utf8* kBaseFieldName  = "(base)";

            /** @brief 범위 한쪽 끝입니다. 숫자면 `_value` 이고, 필드 이름이면 `_expr`(쓰기 · 읽기 함수의 `object` 기준 식)입니다. */
            struct Bound
            {
                string  _text; ///< 적힌 글입니다(진단용)
                string  _expr; ///< 필드 이름이면 C++ 식입니다(`( -object._halfSize )`)
                float64 _value;
                uint8   _bPresent;
                uint8   _bLiteral;

                Bound() noexcept
                    : _text{}
                    , _expr{}
                    , _value{ 0.0 }
                    , _bPresent{ SW_FALSE }
                    , _bLiteral{ SW_FALSE }
                {
                }
            };

            /** @brief 값 하나를 쓰는 자리입니다. 쓰기 · 읽기 식, 비교의 두 식, 원소마다의 범위를 담습니다. */
            struct Site
            {
                const ParsedPropertyInfo* _pProp;
                string                    _expr;
                string                    _exprA;
                string                    _exprB;
                vector<Bound>             _listMin; ///< 하나면 모든 원소에, 수학 타입 원소 수만큼이면 원소마다 씁니다
                vector<Bound>             _listMax;
                int32                     _depth;
                uint8                     _bBitField;
            };

            /** @brief 범위 이름을 찾는 범위 하나입니다. 타입, 그 객체의 식, 볼 수 있는 필드 수(앞선 필드만)를 담습니다. */
            struct ScopeFrame
            {
                const ParsedTypeInfo* _pType;
                string                _objectExpr;
                size_t                _limit;
            };

            /** @brief 최상위 타입 하나의 코덱을 만드는 동안의 상태입니다. */
            struct EmitState
            {
                const ParsedHeader*            _pHeader;
                const string*                  _pSourcePath;
                const ParsedTypeInfo*          _pRoot;
                CodeEmitBuffer                 _writeBuffer;
                CodeEmitBuffer                 _readBuffer;
                CodeEmitBuffer                 _equalsBuffer;
                CodeEmit                       _write;
                CodeEmit                       _read;
                CodeEmit                       _equals;
                string                         _canonical;
                vector<pair<string, string>>   _listDependency;
                vector<const ParsedFixedEnum*> _listEnum;
                vector<ScopeFrame>             _listScope;
                vector<string>                 _listInlineFqn;   ///< 지금 펼치는 중첩 타입들입니다(순환을 막습니다)
                vector<string>                 _listResolvedFqn; ///< 펼치며 바깥 범위 이름을 푼 중첩 타입입니다
                string                         _reason;
                string                         _unresolvedName;
                string                         _unresolvedMember;
                uint32                         _unresolvedLine;
                uint8                          _bError;

                EmitState( const ParsedHeader& header, const string& sourcePath, const ParsedTypeInfo& root )
                    : _pHeader{ &header }
                    , _pSourcePath{ &sourcePath }
                    , _pRoot{ &root }
                    , _writeBuffer{}
                    , _readBuffer{}
                    , _equalsBuffer{}
                    , _write{ _writeBuffer }
                    , _read{ _readBuffer }
                    , _equals{ _equalsBuffer }
                    , _canonical{}
                    , _listDependency{}
                    , _listEnum{}
                    , _listScope{}
                    , _listInlineFqn{}
                    , _listResolvedFqn{}
                    , _reason{}
                    , _unresolvedName{}
                    , _unresolvedMember{}
                    , _unresolvedLine{ 0 }
                    , _bError{ SW_FALSE }
                {
                }
            };

            // ------------------------------------------------------------------------------
            // A) 진단
            // ------------------------------------------------------------------------------
            static void reportError( EmitState& state, const ParsedPropertyInfo& prop, const string_view message )
            {
                SW_LOG_ERROR( "ERROR: %#(%#): fixed layout of '%#::%#': %#", *state._pSourcePath, prop._line, state._pRoot->_fullyQualifiedName,
                              prop._memberName, message );
                state._bError = SW_TRUE;
            }

            /** @brief 첫 이유만 남깁니다. 타입이 적격이 아닌 까닭은 하나면 충분합니다. 오류와 달리 생성은 계속됩니다. */
            static void markIneligible( EmitState& state, const ParsedPropertyInfo& prop, const string_view reason )
            {
                if ( state._reason.empty() )
                    state._reason = "field '" + prop._memberName + "': " + string( reason );
            }

            static string formatFloat( const float64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer64]{};
                std::snprintf( arrBuffer, sizeof( arrBuffer ), "%.9g", value );
                return string( arrBuffer );
            }

            static string formatInteger( const int64 value )
            {
                StringBuilder<constant::kMaxBuffer64> b;
                b.appendFormat( "%#", value );
                return string( b.view() );
            }

            static const FixedMathTypeDef* findMathType( const string_view typeName )
            {
                for ( const FixedMathTypeDef& math : fixedlayout::kArrMathType )
                {
                    if ( typeName == math._pName )
                        return &math;
                }
                return nullptr;
            }

            static vector<string> splitMembers( const string_view memberList )
            {
                vector<string>        listMember;
                const string_splitter parts( memberList, { "," } );
                for ( const string_view part : parts.getSplitList() )
                {
                    listMember.emplace_back( StringUtil::trim( part ) );
                }
                return listMember;
            }

            /** @brief 쓰기 · 읽기 · 비교 · 정규 문자열이 볼 원소 종류입니다. 컨테이너는 원소까지 내려갑니다. */
            static const ParsedFixedType& findLeafType( const ParsedFixedType& type )
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

            /** @brief 가변 길이 단계 수입니다(vector · 문자열). 중첩 REFLECT 타입 안은 그 타입의 키가 맡으므로 세지 않습니다. */
            static int32 countVariableLengthLevels( const ParsedFixedType& type )
            {
                const bool bVariable = type._kind == FixedValueKind::Vector || type._kind == FixedValueKind::String || type._kind == FixedValueKind::Bitset;
                int32      count     = bVariable ? 1 : 0;
                if ( type._element != nullptr )
                    count += countVariableLengthLevels( *type._element );
                return count;
            }

            /** @brief 원소 타입까지 내려가 인코딩 표 밖의 것을 찾습니다. 없으면 빈 문자열입니다. */
            static string findIneligibleReason( const ParsedFixedType& type )
            {
                if ( type._kind == FixedValueKind::Ineligible )
                    return type._reason;
                if ( type._element != nullptr )
                    return findIneligibleReason( *type._element );
                if ( type._kind == FixedValueKind::Vector || type._kind == FixedValueKind::FixedArray )
                    return "container element type is unknown";
                return {};
            }

            // ------------------------------------------------------------------------------
            // B) 범위. `Min` · `Max` 의 항을 숫자나 앞선 필드의 식으로 풉니다
            // ------------------------------------------------------------------------------
            static bool isNumericTerm( const string_view term ) noexcept
            {
                if ( term.empty() )
                    return false;
                const utf8 first  = term.front();
                const utf8 second = term.size() > 1 ? term[1] : '\0';
                if ( ( '0' <= first && first <= '9' ) || first == '.' )
                    return true;
                const bool bSigned      = first == '-' || first == '+';
                const bool bDigitSecond = ( '0' <= second && second <= '9' ) || second == '.';
                return bSigned && bDigitSecond;
            }

            enum class LookupResult : uint8
            {
                Found,
                NotFound,
                Error,
            };

            /**
             * @brief 범위 이름을 안쪽 범위부터 바깥으로 찾습니다. 찾은 필드는 앞에 선언된 Transient 아닌 정수 · 실수여야 합니다.
             * @param bIntegerSite 범위를 받는 값이 정수입니다. 그러면 범위 필드도 정수여야 합니다. 실수 필드를 정수로 바꾸면 소수부가 잘리기 때문입니다.
             */
            static LookupResult lookupRangeName( EmitState& state, const ParsedPropertyInfo& prop, const string& name, const bool bIntegerSite, string& outExpr )
            {
                for ( auto frameIt = state._listScope.rbegin(); frameIt != state._listScope.rend(); ++frameIt )
                {
                    const vector<ParsedPropertyInfo>& listProperty = frameIt->_pType->_listProperty;
                    for ( size_t index = 0; index < listProperty.size(); ++index )
                    {
                        const ParsedPropertyInfo& candidate = listProperty[index];
                        if ( candidate._memberName != name )
                            continue;
                        if ( index >= frameIt->_limit )
                        {
                            reportError( state, prop, "range field '" + name + "' must be declared before this field - the reader needs its value first" );
                            return LookupResult::Error;
                        }
                        const FixedValueKind kind     = candidate._fixedType != nullptr ? candidate._fixedType->_kind : FixedValueKind::Ineligible;
                        const bool           bNumeric = kind == FixedValueKind::Integer || kind == FixedValueKind::Float32 || kind == FixedValueKind::Float64;
                        const bool           bStored  = candidate._bTransient == SW_FALSE && candidate._bIsBitField == SW_FALSE;
                        if ( ( bNumeric && bStored ) == false )
                        {
                            reportError( state, prop, "range field '" + name + "' must be a non-transient integer or floating-point field" );
                            return LookupResult::Error;
                        }
                        if ( bIntegerSite && kind != FixedValueKind::Integer )
                        {
                            reportError( state, prop, "range field '" + name + "' is floating-point but this field is an integer - the fraction would be cut" );
                            return LookupResult::Error;
                        }
                        outExpr = frameIt->_objectExpr + "." + name;
                        return LookupResult::Found;
                    }
                }
                return LookupResult::NotFound;
            }

            /**
             * @brief `Min` 또는 `Max` 글을 항마다 풉니다.
             * @return 생성 오류이거나 이름을 찾지 못했으면 false 입니다. 이름을 못 찾은 것은 `state._unresolvedName` 에 남습니다.
             */
            static bool resolveBounds( EmitState& state, const ParsedPropertyInfo& prop, const string& text, const bool bInteger, vector<Bound>& outListBound )
            {
                outListBound.clear();
                if ( text.empty() )
                {
                    outListBound.emplace_back();
                    return true;
                }
                const string_splitter parts( text, { "," } );
                for ( const string_view part : parts.getSplitList() )
                {
                    const string_view term = StringUtil::trim( part );
                    Bound             bound;
                    bound._bPresent = SW_TRUE;
                    bound._text     = string( term );
                    if ( isNumericTerm( term ) )
                    {
                        if ( StringUtil::parseDouble( term, bound._value ) == false )
                        {
                            reportError( state, prop, "range term '" + bound._text + "' is not a number" );
                            return false;
                        }
                        bound._bLiteral = SW_TRUE;
                        // 경계는 float32 로 보관합니다(`PropertyMetadata::_minRange`). 정수 경계는 float32 로 정확히 나타나야 두 형식이 같은 값을 봅니다.
                        const bool bWholeNumber = std::floor( bound._value ) == bound._value;
                        const bool bExact       = static_cast<float64>( static_cast<float32>( bound._value ) ) == bound._value;
                        if ( bInteger && bWholeNumber == false )
                        {
                            reportError( state, prop, "integer range term '" + bound._text + "' is not a whole number" );
                            return false;
                        }
                        if ( bInteger && bExact == false )
                        {
                            reportError( state, prop, "integer range term '" + bound._text + "' is not exactly representable as float32 - Min/Max are stored as float32" );
                            return false;
                        }
                        outListBound.push_back( std::move( bound ) );
                        continue;
                    }

                    const bool         bNegative = term.front() == '-';
                    const string       name( bNegative ? term.substr( 1 ) : term );
                    string             fieldExpr;
                    const LookupResult result = lookupRangeName( state, prop, name, bInteger, fieldExpr );
                    if ( result == LookupResult::Error )
                        return false;
                    if ( result == LookupResult::NotFound )
                    {
                        if ( state._listScope.size() > 1 )
                        {
                            reportError( state, prop, "range name '" + name + "' is not a field of this type or of any enclosing type" );
                            return false;
                        }
                        // 자기 타입에는 없습니다. 같은 헤더의 바깥 타입이 펼치며 풀 수 있고, 그 타입 혼자서는 코덱이 없습니다.
                        if ( state._unresolvedName.empty() )
                        {
                            state._unresolvedName   = name;
                            state._unresolvedMember = prop._memberName;
                            state._unresolvedLine   = prop._line;
                        }
                        markIneligible( state, prop, "range name '" + name + "' refers to a field of an enclosing type" );
                        return false;
                    }
                    bound._expr = bNegative ? "( -" + fieldExpr + " )" : fieldExpr;
                    outListBound.push_back( std::move( bound ) );
                }
                return true;
            }

            /** @brief 원소 @p elementIndex 의 범위입니다. 항이 하나면 모든 원소가 같습니다. */
            static const Bound& pickBound( const vector<Bound>& listBound, const size_t elementIndex )
            {
                return listBound.size() == 1 ? listBound.front() : listBound[elementIndex];
            }

            static string makeIntegerBound( const Bound& bound, const bool bLower )
            {
                if ( bound._bPresent == SW_FALSE )
                    return bLower ? "::sw::FixedCodecUtil::kNoLowerBound" : "::sw::FixedCodecUtil::kNoUpperBound";
                if ( bound._bLiteral == SW_TRUE )
                    return "static_cast<int64>( " + formatInteger( static_cast<int64>( bound._value ) ) + " )";
                return "static_cast<int64>( " + bound._expr + " )";
            }

            static string makeFloatBound( const Bound& bound, const bool bLower, const utf8* pCastType )
            {
                if ( bound._bPresent == SW_FALSE )
                    return bLower ? "::sw::FixedCodecUtil::kNoLowerFloatBound" : "::sw::FixedCodecUtil::kNoUpperFloatBound";
                if ( bound._bLiteral == SW_TRUE )
                    return string( "static_cast<" ) + pCastType + ">( " + bound._text + " )";
                return string( "static_cast<" ) + pCastType + ">( " + bound._expr + " )";
            }

            // ------------------------------------------------------------------------------
            // C) 정규 문자열
            // ------------------------------------------------------------------------------
            static void appendEnumCanonical( string& out, const ParsedFixedEnum& enumInfo )
            {
                out += "enum{" + enumInfo._fullyQualifiedName + ";" + enumInfo._underlyingTypeName + ";";
                out += enumInfo._bIsBitFlag == SW_TRUE ? "flags" : "plain";
                if ( enumInfo._countEnumerator.empty() == false )
                    out += ";count=" + enumInfo._countEnumerator;
                for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
                {
                    out += ";" + enumerator._name + "=" + formatInteger( enumerator._value );
                }
                out += "}";
            }

            static void appendFieldHead( string& out, const ParsedPropertyInfo& prop )
            {
                out += "F=" + prop._memberName + ":" + prop._typeName;
                if ( prop._bIsBitField == SW_TRUE )
                    out += ":bit";
                if ( prop._minRangeExpr._text.empty() == false )
                    out += ":min=" + prop._minRangeExpr._text;
                if ( prop._maxRangeExpr._text.empty() == false )
                    out += ":max=" + prop._maxRangeExpr._text;
                if ( prop._bHasStep == SW_TRUE )
                    out += ":step=" + formatFloat( static_cast<float64>( prop._step ) );
                if ( prop._bHasMaxCount == SW_TRUE )
                    out += ":maxcount=" + formatInteger( static_cast<int64>( prop._maxCount ) );
                out += ":";
            }

            // ------------------------------------------------------------------------------
            // D) 값 하나의 코드. 쓰기 · 읽기 · 비교를 같은 순서로 냅니다
            // ------------------------------------------------------------------------------
            static string makeEnumHelperName( const ParsedFixedEnum& enumInfo )
            {
                return "isFixedEnumValue_" + CodeGenerator::sanitizeIdentifier( enumInfo._fullyQualifiedName );
            }

            static string makeCodecAccessorName( const string& fqn )
            {
                return "getFixedCodec_" + CodeGenerator::sanitizeIdentifier( fqn );
            }

            static void addDependency( EmitState& state, const string& fieldName, const string& fqn )
            {
                for ( const pair<string, string>& dependency : state._listDependency )
                {
                    if ( dependency.second == fqn )
                        return;
                }
                state._listDependency.emplace_back( fieldName, fqn );
            }

            static void addEnum( EmitState& state, const ParsedFixedEnum& enumInfo )
            {
                for ( const ParsedFixedEnum* pEnum : state._listEnum )
                {
                    if ( pEnum->_fullyQualifiedName == enumInfo._fullyQualifiedName )
                        return;
                }
                state._listEnum.push_back( &enumInfo );
            }

            static void emitReadCheck( EmitState& state, const string& call )
            {
                state._read.linef( "result = %#;", call );
                state._read.linef( "if ( result != %# )", kReadOk );
                state._read.push().line( "return result;" ).pop();
            }

            static void emitWriteCheck( EmitState& state, const string& call )
            {
                state._write.linef( "if ( %# == false )", call );
                state._write.push().line( "return false;" ).pop();
            }

            static void emitEqualsCheck( EmitState& state, const string& condition )
            {
                state._equals.linef( "if ( %# )", condition );
                state._equals.push().line( "return false;" ).pop();
            }

            static void emitInteger( EmitState& state, const Site& site, const Bound& minBound, const Bound& maxBound )
            {
                const string                            minExpr = makeIntegerBound( minBound, true );
                const string                            maxExpr = makeIntegerBound( maxBound, false );
                const bool                              bRanged = minBound._bPresent == SW_TRUE && maxBound._bPresent == SW_TRUE;
                const utf8*                             pWrite  = bRanged ? "writeRangedInteger" : "writeVarInteger";
                const utf8*                             pRead   = bRanged ? "readRangedInteger" : "readVarInteger";
                StringBuilder<constant::kMaxBuffer1024> call;
                call.appendFormat( "::sw::FixedCodecUtil::%#( writer, %#, %#, %# )", pWrite, site._expr, minExpr, maxExpr );
                emitWriteCheck( state, string( call.view() ) );
                call.clear();
                call.appendFormat( "::sw::FixedCodecUtil::%#( reader, %#, %#, %# )", pRead, minExpr, maxExpr, site._expr );
                emitReadCheck( state, string( call.view() ) );
                emitEqualsCheck( state, site._exprA + " != " + site._exprB );
            }

            static void emitFloat( EmitState& state, const Site& site, const FixedValueKind kind, const Bound& minBound, const Bound& maxBound )
            {
                const ParsedPropertyInfo& prop = *site._pProp;
                if ( prop._bHasStep == SW_TRUE )
                {
                    const string minExpr  = makeFloatBound( minBound, true, "float32" );
                    const string maxExpr  = makeFloatBound( maxBound, false, "float32" );
                    const string stepExpr = "static_cast<float32>( " + formatFloat( static_cast<float64>( prop._step ) ) + " )";
                    emitWriteCheck( state, "::sw::FixedCodecUtil::writeQuantizedFloat32( writer, " + site._expr + ", " + minExpr + ", " + maxExpr + ", " + stepExpr + " )" );
                    emitReadCheck( state, "::sw::FixedCodecUtil::readQuantizedFloat32( reader, " + minExpr + ", " + maxExpr + ", " + stepExpr + ", " + site._expr + " )" );
                }
                else
                {
                    const string minExpr  = makeFloatBound( minBound, true, "float64" );
                    const string maxExpr  = makeFloatBound( maxBound, false, "float64" );
                    const bool   b32      = kind == FixedValueKind::Float32;
                    const bool   bBounded = minBound._bPresent == SW_TRUE || maxBound._bPresent == SW_TRUE;
                    if ( bBounded )
                    {
                        const utf8* pSuffix = b32 ? "Float32" : "Float64";
                        emitWriteCheck( state, string( "::sw::FixedCodecUtil::writeBounded" ) + pSuffix + "( writer, " + site._expr + ", " + minExpr + ", " + maxExpr + " )" );
                        emitReadCheck( state, string( "::sw::FixedCodecUtil::readBounded" ) + pSuffix + "( reader, " + minExpr + ", " + maxExpr + ", " + site._expr + " )" );
                    }
                    else if ( b32 )
                    {
                        state._write.linef( "writer.writeFloat( %# );", site._expr );
                        state._read.linef( "%# = reader.readFloat();", site._expr );
                    }
                    else
                    {
                        state._write.linef( "::sw::FixedCodecUtil::writeFloat64( writer, %# );", site._expr );
                        state._read.linef( "%# = ::sw::FixedCodecUtil::readFloat64( reader );", site._expr );
                    }
                }
                emitEqualsCheck( state, "::sw::FixedCodecUtil::isBitEqual( " + site._exprA + ", " + site._exprB + " ) == false" );
            }

            static void emitEnum( EmitState& state, const Site& site, const ParsedFixedEnum& enumInfo )
            {
                addEnum( state, enumInfo );
                const string helper   = makeEnumHelperName( enumInfo );
                const string enumType = "::" + enumInfo._fullyQualifiedName;
                emitWriteCheck( state, helper + "( " + site._expr + " )" );

                const bool bCounted = enumInfo._countEnumerator.empty() == false && enumInfo._bIsBitFlag == SW_FALSE;
                if ( bCounted )
                {
                    // 범위는 [가장 작은 열거자, Count - 1] 입니다. 값은 컴파일러가 계산합니다(파서의 값은 진단용입니다).
                    const ParsedEnumeratorInfo* pMin = nullptr;
                    for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
                    {
                        if ( enumerator._name == enumInfo._countEnumerator )
                            continue;
                        if ( pMin == nullptr || enumerator._value < pMin->_value )
                            pMin = &enumerator;
                    }
                    const string minExpr = "static_cast<int64>( " + enumType + "::" + pMin->_name + " )";
                    const string maxExpr = "static_cast<int64>( " + enumType + "::" + enumInfo._countEnumerator + " ) - 1";
                    emitWriteCheck( state, "::sw::FixedCodecUtil::writeRangedInteger( writer, static_cast<int64>( " + site._expr + " ), " + minExpr + ", " + maxExpr + " )" );
                    const string value = "value" + formatInteger( site._depth );
                    state._read.line( "{" ).push();
                    state._read.linef( "int64 %# = 0;", value );
                    emitReadCheck( state, "::sw::FixedCodecUtil::readRangedInteger( reader, " + minExpr + ", " + maxExpr + ", " + value + " )" );
                    state._read.linef( "if ( %#( static_cast< %# >( %# ) ) == false )", helper, enumType, value );
                    state._read.push().linef( "return %#;", kReadOutOfRange ).pop();
                    state._read.linef( "%# = static_cast< %# >( %# );", site._expr, enumType, value );
                    state._read.pop().line( "}" );
                }
                else
                {
                    state._write.linef( "::sw::FixedCodecUtil::writeEnumBits( writer, %# );", site._expr );
                    const string value = "value" + formatInteger( site._depth );
                    state._read.line( "{" ).push();
                    state._read.linef( "const %# %# = ::sw::FixedCodecUtil::readEnumBits< %# >( reader );", enumType, value, enumType );
                    state._read.linef( "if ( %#( %# ) == false )", helper, value );
                    state._read.push().linef( "return %#;", kReadOutOfRange ).pop();
                    state._read.linef( "%# = %#;", site._expr, value );
                    state._read.pop().line( "}" );
                }
                emitEqualsCheck( state, site._exprA + " != " + site._exprB );
            }

            static Site makeChildSite( const Site& parent, const string& expr, const string& exprA, const string& exprB )
            {
                Site child       = parent;
                child._expr      = expr;
                child._exprA     = exprA;
                child._exprB     = exprB;
                child._depth     = parent._depth + 1;
                child._bBitField = SW_FALSE;
                return child;
            }

            static void emitContainer( EmitState& state, const Site& site, const ParsedFixedType& type, string& outCanonical )
            {
                const ParsedPropertyInfo& prop     = *site._pProp;
                const string              depth    = formatInteger( site._depth );
                const string              element  = "element" + depth;
                const string              index    = "index" + depth;
                const bool                bVector  = type._kind == FixedValueKind::Vector;
                const string              maxCount = formatInteger( static_cast<int64>( prop._maxCount ) ) + "u";
                if ( bVector )
                {
                    emitWriteCheck( state, "::sw::FixedCodecUtil::writeCount( writer, " + site._expr + ".size(), " + maxCount + " )" );
                    const string count = "count" + depth;
                    state._read.line( "{" ).push();
                    state._read.linef( "uint32 %# = 0;", count );
                    emitReadCheck( state, "::sw::FixedCodecUtil::readCount( reader, " + maxCount + ", " + count + " )" );
                    state._read.linef( "%#.resize( %# );", site._expr, count );
                    state._read.pop().line( "}" );
                    emitEqualsCheck( state, site._exprA + ".size() != " + site._exprB + ".size()" );
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
                const Site child = makeChildSite( site, element, site._exprA + "[" + index + "]", site._exprB + "[" + index + "]" );
                emitValue( state, child, *type._element, outCanonical );
                outCanonical += ">";
                state._write.pop().line( "}" );
                state._read.pop().line( "}" );
                state._equals.pop().line( "}" );
            }

            static const ParsedTypeInfo* findHeaderType( const EmitState& state, const string& fqn )
            {
                for ( const ParsedTypeInfo& candidate : state._pHeader->_listType )
                {
                    if ( candidate._fullyQualifiedName == fqn )
                        return &candidate;
                }
                return nullptr;
            }

            /** @brief 같은 헤더의 중첩 타입을 부모 코덱 안에 펼칠 수 있는지 봅니다. 기반과 검증 함수가 없고 코덱 필드가 모두 공개여야 합니다. */
            static bool canInline( const EmitState& state, const ParsedTypeInfo* pNested )
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

            static void emitReflect( EmitState& state, const Site& site, const ParsedFixedType& type, string& outCanonical )
            {
                const string&         fqn     = type._fullyQualifiedName;
                const ParsedTypeInfo* pNested = findHeaderType( state, fqn );
                if ( canInline( state, pNested ) )
                {
                    outCanonical += "inline{T=" + fqn + "|";
                    state._listInlineFqn.push_back( fqn );
                    emitFields( state, *pNested, site._expr, site._exprA, site._exprB, site._depth + 1, outCanonical );
                    state._listInlineFqn.pop_back();
                    outCanonical += "}";
                    // 펼친 타입이 혼자서는 풀지 못하던 바깥 범위 이름을 여기서 풀었습니다. 그 타입의 "바깥 이름" 오류는 사라집니다.
                    if ( state._bError == SW_FALSE )
                        state._listResolvedFqn.push_back( fqn );
                    return;
                }

                outCanonical += "ref{" + fqn + "}";
                const bool   bSelf = fqn == state._pRoot->_fullyQualifiedName;
                const string codec = bSelf ? string() : makeCodecAccessorName( fqn ) + "()";
                if ( bSelf == false )
                    addDependency( state, site._pProp->_memberName, fqn );
                const string writeCall  = bSelf ? string( "fixedWrite" ) : codec + "._pWrite";
                const string readCall   = bSelf ? string( "fixedRead" ) : codec + "._pRead";
                const string equalsCall = bSelf ? string( "fixedEquals" ) : codec + "._pEquals";
                emitWriteCheck( state, writeCall + "( writer, &" + site._expr + " )" );
                emitReadCheck( state, readCall + "( reader, &" + site._expr + " )" );
                emitEqualsCheck( state, equalsCall + "( &" + site._exprA + ", &" + site._exprB + " ) == false" );
            }

            static void emitValue( EmitState& state, const Site& site, const ParsedFixedType& type, string& outCanonical )
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
                        emitEqualsCheck( state, site._exprA + " != " + site._exprB );
                        break;
                    }
                    case FixedValueKind::Integer:
                    {
                        outCanonical += "int";
                        emitInteger( state, site, pickBound( site._listMin, 0 ), pickBound( site._listMax, 0 ) );
                        break;
                    }
                    case FixedValueKind::Float32:
                    case FixedValueKind::Float64:
                    {
                        outCanonical += type._kind == FixedValueKind::Float32 ? "f32" : "f64";
                        emitFloat( state, site, type._kind, pickBound( site._listMin, 0 ), pickBound( site._listMax, 0 ) );
                        break;
                    }
                    case FixedValueKind::Math:
                    {
                        const FixedMathTypeDef* pMath      = findMathType( type._typeName );
                        const vector<string>    listMember = splitMembers( pMath->_pMemberList );
                        outCanonical += "math(" + type._typeName + ")";
                        for ( size_t memberIndex = 0; memberIndex < listMember.size(); ++memberIndex )
                        {
                            const string& member = listMember[memberIndex];
                            const Site    child  = makeChildSite( site, site._expr + "." + member, site._exprA + "." + member, site._exprB + "." + member );
                            emitFloat( state, child, FixedValueKind::Float32, pickBound( site._listMin, memberIndex ), pickBound( site._listMax, memberIndex ) );
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
                        const string maxCount = formatInteger( static_cast<int64>( site._pProp->_maxCount ) ) + "u";
                        emitWriteCheck( state, "::sw::FixedCodecUtil::writeString( writer, " + site._expr + ", " + maxCount + " )" );
                        emitReadCheck( state, "::sw::FixedCodecUtil::readString( reader, " + maxCount + ", " + site._expr + " )" );
                        emitEqualsCheck( state, site._exprA + " != " + site._exprB );
                        break;
                    }
                    case FixedValueKind::Bitset:
                    {
                        outCanonical += "bits";
                        const string maxCount = formatInteger( static_cast<int64>( site._pProp->_maxCount ) ) + "u";
                        emitWriteCheck( state, "::sw::FixedCodecUtil::writeBitset( writer, " + site._expr + ", " + maxCount + " )" );
                        emitReadCheck( state, "::sw::FixedCodecUtil::readBitset( reader, " + maxCount + ", " + site._expr + " )" );
                        emitEqualsCheck( state, site._exprA + " != " + site._exprB );
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

            /** @brief 열거형 값이 있는지 봅니다. `Count` 열거자는 값이 아닙니다. */
            static bool hasEnumValues( const ParsedFixedEnum& enumInfo )
            {
                for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
                {
                    if ( enumerator._name != enumInfo._countEnumerator )
                        return true;
                }
                return false;
            }

            /**
             * @brief 프로퍼티 하나의 키를 검사하고 코드를 냅니다. 적격이 아니면 이유만 남기고 코드를 내지 않습니다.
             * @details 키를 잘못 적은 것은 타입이 적격이든 아니든 생성 오류입니다. 고칠 곳이 선언에 있기 때문입니다.
             */
            static void emitProperty( EmitState& state, const ParsedPropertyInfo& prop, const string& objectExpr, const string& exprA, const string& exprB,
                                      const int32 depth, string& outCanonical )
            {
                if ( prop._bTransient == SW_TRUE )
                    return;
                if ( prop._fixedType == nullptr )
                {
                    markIneligible( state, prop, "field type was not classified" );
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
                    reportError( state, prop, "Min/Max apply to integer, floating-point and math fields only" );
                if ( prop._bHasStep == SW_TRUE )
                {
                    const bool bFloat32 = leafKind == FixedValueKind::Float32 || leafKind == FixedValueKind::Math;
                    if ( bFloat32 == false )
                        reportError( state, prop, "Step applies to float32 fields (and float32 math types) only" );
                    else if ( ( prop._step > 0.0f ) == false )
                        reportError( state, prop, "Step must be greater than zero" );
                    else if ( prop._minRangeExpr._text.empty() || prop._maxRangeExpr._text.empty() )
                        reportError( state, prop, "Step needs both Min and Max" );
                }
                if ( prop._bHasMaxCount == SW_TRUE && variable == 0 )
                    reportError( state, prop, "MaxCount applies to string, vector and DynamicBitset fields only" );

                Site site;
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
                    if ( resolveBounds( state, prop, prop._minRangeExpr._text, bInteger, site._listMin ) == false ||
                         resolveBounds( state, prop, prop._maxRangeExpr._text, bInteger, site._listMax ) == false )
                        return;
                    const bool bMinCountMismatch = site._listMin.size() != 1 && site._listMin.size() != elementCount;
                    const bool bMaxCountMismatch = site._listMax.size() != 1 && site._listMax.size() != elementCount;
                    if ( bMinCountMismatch || bMaxCountMismatch )
                    {
                        reportError( state, prop, "Min/Max list has " + formatInteger( static_cast<int64>( site._listMin.size() ) ) + "/" + formatInteger( static_cast<int64>( site._listMax.size() ) ) + " terms but the value has " + formatInteger( static_cast<int64>( elementCount ) ) + " elements" );
                        return;
                    }
                }

                // 2) 적격 판정. 오류가 아닙니다
                const string reason = findIneligibleReason( type );
                if ( reason.empty() == false )
                {
                    markIneligible( state, prop, reason );
                    return;
                }
                if ( variable > 1 )
                {
                    markIneligible( state, prop, "nested variable-length values (vector of vector or string) need a MaxCount each" );
                    return;
                }
                if ( variable == 1 && prop._bHasMaxCount == SW_FALSE )
                {
                    markIneligible( state, prop, "variable-length value without MaxCount" );
                    return;
                }
                if ( leafKind == FixedValueKind::Enum && hasEnumValues( *leaf._enum ) == false )
                {
                    markIneligible( state, prop, "enum has no enumerators" );
                    return;
                }

                appendFieldHead( outCanonical, prop );
                emitValue( state, site, type, outCanonical );
                outCanonical += "|";
            }

            static void emitFields( EmitState& state, const ParsedTypeInfo& type, const string& objectExpr, const string& exprA, const string& exprB,
                                    const int32 depth, string& outCanonical )
            {
                state._listScope.push_back( ScopeFrame{ &type, objectExpr, 0 } );
                for ( size_t index = 0; index < type._listProperty.size(); ++index )
                {
                    state._listScope.back()._limit = index;
                    emitProperty( state, type._listProperty[index], objectExpr, exprA, exprB, depth, outCanonical );
                }
                state._listScope.pop_back();
            }

            // ------------------------------------------------------------------------------
            // E) 멤버 함수 글. Registrar<T> 안에 들어갑니다
            // ------------------------------------------------------------------------------
            static string makeMemberText( EmitState& state, const ParsedTypeInfo& type, const bool bHasBase, const string& canonical )
            {
                const string&  fqn = type._fullyQualifiedName;
                CodeEmitBuffer buffer;
                CodeEmit       emit( buffer );
                emit.push( kMemberIndent );
                emit.blank();
                emit.linef( "static constexpr const utf8* kFixedCanonical = %#;", CodeEmit::quoted( canonical ) );
                emit.blank();

                // 쓰기. 필드가 없는 타입도 있으므로 인자를 쓰지 않는 경고를 막습니다
                emit.line( "static bool fixedWrite( ::sw::BitWriter& writer, const void* pObject )" );
                emit.line( "{" ).push();
                emit.linef( "const %#& object = *static_cast<const %#*>( pObject );", fqn, fqn );
                emit.line( "(void)object;" );
                emit.line( "(void)writer;" );
                if ( bHasBase )
                {
                    emit.linef( "if ( %#()._pWrite( writer, static_cast<const ::%#*>( &object ) ) == false )", makeCodecAccessorName( type._parentFQN ), type._parentFQN );
                    emit.push().line( "return false;" ).pop();
                }
                emit.raw( state._writeBuffer.view() );
                emit.line( "return true;" );
                emit.pop().line( "}" );
                emit.blank();

                // 읽기
                emit.line( "static ::sw::FixedReadResult fixedRead( ::sw::BitReader& reader, void* pObject )" );
                emit.line( "{" ).push();
                emit.linef( "%#& object = *static_cast<%#*>( pObject );", fqn, fqn );
                emit.line( "(void)object;" );
                emit.line( "(void)reader;" );
                emit.linef( "::sw::FixedReadResult result = %#;", kReadOk );
                if ( bHasBase )
                {
                    emit.linef( "result = %#()._pRead( reader, static_cast<::%#*>( &object ) );", makeCodecAccessorName( type._parentFQN ), type._parentFQN );
                    emit.linef( "if ( result != %# )", kReadOk );
                    emit.push().line( "return result;" ).pop();
                }
                emit.raw( state._readBuffer.view() );
                if ( type._validate.empty() == false )
                {
                    emit.line( "{" ).push();
                    emit.line( "::sw::ValidationContext context;" );
                    emit.linef( "object.%#( context );", type._validate );
                    emit.line( "if ( context.hasError() )" );
                    emit.push().line( "return ::sw::FixedReadResult::ValidateFailed;" ).pop();
                    emit.pop().line( "}" );
                }
                emit.line( "return result;" );
                emit.pop().line( "}" );
                emit.blank();

                // 비교
                emit.line( "static bool fixedEquals( const void* pA, const void* pB )" );
                emit.line( "{" ).push();
                emit.linef( "const %#& a = *static_cast<const %#*>( pA );", fqn, fqn );
                emit.linef( "const %#& b = *static_cast<const %#*>( pB );", fqn, fqn );
                emit.line( "(void)a;" );
                emit.line( "(void)b;" );
                if ( bHasBase )
                {
                    emit.linef( "if ( %#()._pEquals( static_cast<const ::%#*>( &a ), static_cast<const ::%#*>( &b ) ) == false )", makeCodecAccessorName( type._parentFQN ),
                                type._parentFQN, type._parentFQN );
                    emit.push().line( "return false;" ).pop();
                }
                emit.raw( state._equalsBuffer.view() );
                emit.line( "return true;" );
                emit.pop().line( "}" );
                emit.blank();

                // 스키마 해시. 다른 타입의 해시는 다른 헤더 · 모듈일 수 있으므로 실행 시간에 그 코덱에서 얻어 섞습니다
                emit.line( "static uint64 computeFixedSchemaHash()" );
                emit.line( "{" ).push();
                StringBuilder<constant::kMaxBuffer1024> nested;
                for ( const pair<string, string>& dependency : state._listDependency )
                {
                    if ( nested.size() > 0 )
                        nested.append( ", " );
                    nested.appendFormat( "%#()._pComputeSchemaHash()", makeCodecAccessorName( dependency.second ) );
                }
                emit.linef( "static const uint64 s_hash = ::sw::FixedLayout::computeSchemaHash( ::sw::FixedLayout::kVersion, kFixedCanonical, { %# } );", nested.view() );
                emit.line( "return s_hash;" );
                emit.pop().line( "}" );

                // 다른 타입의 코덱
                for ( const pair<string, string>& dependency : state._listDependency )
                {
                    emit.blank();
                    emit.linef( "static const ::sw::FixedCodec& %#()", makeCodecAccessorName( dependency.second ) );
                    emit.line( "{" ).push();
                    emit.linef( "static const ::sw::hashed_string s_fqn( %# );", CodeEmit::quoted( dependency.second ) );
                    emit.line( "static ::sw::TypeLookupCache     s_cache;" );
                    emit.line( "return ::sw::FixedLayout::requireNestedCodec( s_cache, s_fqn );" );
                    emit.pop().line( "}" );
                }

                // 열거자 확인. 등록된 열거자(비트 플래그면 등록된 비트)만 받습니다
                for ( const ParsedFixedEnum* pEnum : state._listEnum )
                {
                    const string enumType = "::" + pEnum->_fullyQualifiedName;
                    emit.blank();
                    emit.linef( "static bool %#( const %# value )", makeEnumHelperName( *pEnum ), enumType );
                    emit.line( "{" ).push();
                    if ( pEnum->_bIsBitFlag == SW_TRUE )
                    {
                        StringBuilder<constant::kMaxBuffer1024> mask;
                        for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
                        {
                            if ( mask.size() > 0 )
                                mask.append( " | " );
                            mask.appendFormat( "static_cast<uint64>( %#::%# )", enumType, enumerator._name );
                        }
                        emit.linef( "constexpr uint64 kMask = %#;", mask.size() > 0 ? mask.view() : string_view( "0" ) );
                        emit.line( "return ( static_cast<uint64>( value ) & ~kMask ) == 0;" );
                    }
                    else
                    {
                        emit.line( "switch ( static_cast<int64>( value ) )" );
                        emit.line( "{" ).push();
                        vector<int64> listSeen;
                        for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
                        {
                            const bool bDuplicate = std::find( listSeen.begin(), listSeen.end(), enumerator._value ) != listSeen.end();
                            if ( enumerator._name == pEnum->_countEnumerator || bDuplicate )
                                continue;
                            listSeen.push_back( enumerator._value );
                            emit.linef( "case static_cast<int64>( %#::%# ):", enumType, enumerator._name );
                        }
                        emit.push().line( "return true;" ).pop();
                        emit.line( "default:" );
                        emit.push().line( "return false;" ).pop();
                        emit.pop().line( "}" );
                    }
                    emit.pop().line( "}" );
                }
                return string( buffer.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FixedCodecGenerator::TypePlan::TypePlan() noexcept
        : _canonical{}
        , _reason{}
        , _memberText{}
        , _listDependency{}
        , _unresolvedName{}
        , _unresolvedMember{}
        , _unresolvedLine{ 0 }
        , _bEligible{ SW_FALSE }
        , _bResolvedByParent{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FixedCodecGenerator::FixedCodecGenerator( const ParsedHeader& header, const string& sourceFilePath )
        : _header{ header }
        , _sourceFilePath{ sourceFilePath }
        , _listPlan{}
    {
    }

    bool FixedCodecGenerator::makePlan()
    {
        using Internal = FixedCodecGeneratorInternal;
        _listPlan.clear();
        _listPlan.resize( _header._listType.size() );

        bool           bValid = true;
        vector<string> listResolvedFqn;
        for ( size_t typeIndex = 0; typeIndex < _header._listType.size(); ++typeIndex )
        {
            const ParsedTypeInfo& type = _header._listType[typeIndex];
            TypePlan&             plan = _listPlan[typeIndex];
            Internal::EmitState   state( _header, _sourceFilePath, type );
            // 함수 본문 들여쓰기입니다. 네임스페이스, 구조체, 함수 안입니다.
            state._write.push( Internal::kMemberIndent + 1 );
            state._read.push( Internal::kMemberIndent + 1 );
            state._equals.push( Internal::kMemberIndent + 1 );

            string     canonical = "T=" + type._fullyQualifiedName + "|";
            const bool bHasBase  = type._baseCount == 1 && type._bParentReflected == SW_TRUE && type._parentFQN.empty() == false;
            if ( bHasBase )
            {
                canonical += "B=" + type._parentFQN + "|";
                Internal::addDependency( state, Internal::kBaseFieldName, type._parentFQN );
            }
            state._listInlineFqn.push_back( type._fullyQualifiedName );
            Internal::emitFields( state, type, "object", "a", "b", 0, canonical );

            if ( state._bError == SW_TRUE )
                bValid = false;
            listResolvedFqn.insert( listResolvedFqn.end(), state._listResolvedFqn.begin(), state._listResolvedFqn.end() );
            plan._unresolvedName   = state._unresolvedName;
            plan._unresolvedMember = state._unresolvedMember;
            plan._unresolvedLine   = state._unresolvedLine;
            if ( state._reason.empty() == false )
            {
                plan._reason = state._reason;
                continue;
            }
            plan._bEligible      = SW_TRUE;
            plan._canonical      = canonical;
            plan._listDependency = state._listDependency;
            plan._memberText     = Internal::makeMemberText( state, type, bHasBase, canonical );
        }

        // 바깥 타입의 필드를 범위로 쓰는 타입은 같은 헤더의 부모가 펼치며 그 이름을 풀어야 합니다. 아무도 풀지 않았으면 선언이 틀렸습니다.
        for ( size_t typeIndex = 0; typeIndex < _listPlan.size(); ++typeIndex )
        {
            TypePlan&             plan = _listPlan[typeIndex];
            const ParsedTypeInfo& type = _header._listType[typeIndex];
            if ( plan._unresolvedName.empty() )
                continue;
            if ( std::find( listResolvedFqn.begin(), listResolvedFqn.end(), type._fullyQualifiedName ) != listResolvedFqn.end() )
            {
                plan._bResolvedByParent = SW_TRUE;
                continue;
            }
            SW_LOG_ERROR( "ERROR: %#(%#): fixed layout of '%#::%#': range name '%#' is not a field of this type, and no type in this header "
                          "embeds it with such a field declared before it",
                          _sourceFilePath, plan._unresolvedLine, type._fullyQualifiedName, plan._unresolvedMember, plan._unresolvedName );
            bValid = false;
        }
        return bValid;
    }

    void FixedCodecGenerator::emitRegistration( CodeEmit& emit, const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        if ( plan._bEligible == SW_FALSE )
        {
            emit.linef( "// FixedLayout: not eligible - %#", plan._reason );
            emit.linef( "static ::sw::FixedCodec s_fixedCodec( %# );", CodeEmit::quoted( plan._reason ) );
            emit.line( "info._pFixedCodec = &s_fixedCodec;" );
            return;
        }
        string dependencyArgs = "nullptr, 0u";
        if ( plan._listDependency.empty() == false )
        {
            emit.line( "static const ::sw::FixedCodec::Dependency s_arrFixedDependency[] =" );
            emit.line( "{" ).push();
            for ( const pair<string, string>& dependency : plan._listDependency )
            {
                emit.linef( "{ %#, %# },", CodeEmit::quoted( dependency.first ), CodeEmit::quoted( dependency.second ) );
            }
            emit.pop().line( "};" );
            dependencyArgs = "s_arrFixedDependency, " + FixedCodecGeneratorInternal::formatInteger( static_cast<int64>( plan._listDependency.size() ) ) + "u";
        }
        emit.linef( "static ::sw::FixedCodec s_fixedCodec( &fixedWrite, &fixedRead, &fixedEquals, &computeFixedSchemaHash, kFixedCanonical, %# );", dependencyArgs );
        emit.line( "info._pFixedCodec = &s_fixedCodec;" );
    }

    const string& FixedCodecGenerator::getMemberText( const size_t typeIndex ) const
    {
        return _listPlan[typeIndex]._memberText;
    }

    void FixedCodecGenerator::appendSchemaLine( CodeEmitBuffer& outBuffer, const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        outBuffer.append( fixedlayout::kSchemaLineMarker );
        outBuffer.append( _header._listType[typeIndex]._fullyQualifiedName );
        outBuffer.append( plan._bEligible == SW_TRUE ? " | " : " | (not eligible) " );
        outBuffer.append( plan._bEligible == SW_TRUE ? plan._canonical : plan._reason );
        outBuffer.append( '\n' );
    }

    string FixedCodecGenerator::makeDumpLine( const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        if ( plan._bEligible == SW_TRUE )
            return "  FIXED codec: eligible  " + plan._canonical;
        return "  FIXED codec: not eligible - " + plan._reason;
    }
} // namespace sw
