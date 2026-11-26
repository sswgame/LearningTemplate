#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecValueEmitter.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/String/string_splitter.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"
#include "Programs/ReflectionParser/ParserDefines.h"

#include <cmath>
#include <limits>

namespace sw
{
    /** @brief `Min` · `Max` 의 항을 숫자나 앞선 필드의 식으로 풀고, 코덱 호출에 넣을 범위 식을 만듭니다. */
    struct FixedCodecBoundResolver
    {
        /**
         * @brief `Min` 또는 `Max` 글을 항마다 풉니다.
         * @return 생성 오류이거나 이름을 찾지 못했으면 false 입니다. 이름을 못 찾은 것은 `state._unresolvedName` 에 남습니다.
         */
        [[nodiscard]] static bool resolveBounds( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& text, bool bInteger, vector<FixedCodecBound>& outListBound );
        /** @brief 원소 @p elementIndex 의 범위입니다. 항이 하나면 모든 원소가 같습니다. */
        static const FixedCodecBound& pickBound( const vector<FixedCodecBound>& listBound, size_t elementIndex );
        /** @brief 정수 자리의 `::sw::FixedBound` 식입니다. */
        static string makeIntegerBound( const FixedCodecBound& bound );
        /** @brief 실수 자리의 `::sw::FixedBound` 식입니다. @p bFloat32 면 `Step` 자리라 숫자를 float32 로 바꿉니다. */
        static string makeFloatBound( const FixedCodecBound& bound, bool bFloat32 );

    private:
        static string makeFieldBound( const FixedCodecBound& bound );
    };

    namespace
    {
        struct FixedCodecBoundResolverInternal
        {
            enum class FindResult : uint8
            {
                Found,
                NotFound,
                Error,
            };

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

            /**
             * @brief 범위 이름을 안쪽 범위부터 바깥으로 찾습니다. 찾은 필드는 앞에 선언된 Transient 아닌 정수 · 실수여야 합니다.
             * @param bIntegerSite 범위를 받는 값이 정수입니다. 그러면 범위 필드도 정수여야 합니다. 실수 필드를 정수로 바꾸면 소수부가 잘리기 때문입니다.
             */
            static FindResult findRangeName( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& name, const bool bIntegerSite, FixedCodecBound& outBound )
            {
                for ( size_t frameIndex = state._listScope.size(); frameIndex > 0; --frameIndex )
                {
                    const FixedCodecScopeFrame&       frame        = state._listScope[frameIndex - 1];
                    const vector<ParsedPropertyInfo>& listProperty = frame._pType->_listProperty;
                    for ( size_t index = 0; index < listProperty.size(); ++index )
                    {
                        const ParsedPropertyInfo& candidate = listProperty[index];
                        if ( candidate._memberName != name )
                            continue;
                        if ( index >= frame._limit )
                        {
                            fixed_string<constant::buffer::kMax512> message;
                            formatstring( message.data(), message.capacity(), "range field '%#' must be declared before this field - the reader needs its value first", name );
                            state.reportError( prop, message.view() );
                            return FindResult::Error;
                        }
                        const FixedValueKind kind     = candidate._fixedType != nullptr ? candidate._fixedType->_kind : FixedValueKind::Ineligible;
                        const bool           bNumeric = kind == FixedValueKind::Integer || kind == FixedValueKind::Float32 || kind == FixedValueKind::Float64;
                        const bool           bStored  = candidate._bTransient == SW_FALSE && candidate._bIsBitField == SW_FALSE;
                        if ( ( bNumeric && bStored ) == false )
                        {
                            fixed_string<constant::buffer::kMax512> message;
                            formatstring( message.data(), message.capacity(), "range field '%#' must be a non-transient integer or floating-point field", name );
                            state.reportError( prop, message.view() );
                            return FindResult::Error;
                        }
                        if ( bIntegerSite && kind != FixedValueKind::Integer )
                        {
                            fixed_string<constant::buffer::kMax512> message;
                            formatstring( message.data(), message.capacity(), "range field '%#' is floating-point but this field is an integer - the fraction would be cut", name );
                            state.reportError( prop, message.view() );
                            return FindResult::Error;
                        }
                        outBound._ownerFqn   = frame._pType->_fullyQualifiedName;
                        outBound._fieldName  = name;
                        outBound._scopeDepth = static_cast<uint32>( frameIndex - 1 );
                        return FindResult::Found;
                    }
                }
                return FindResult::NotFound;
            }
        };
    } // namespace

    bool FixedCodecBoundResolver::resolveBounds( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& text, const bool bInteger, vector<FixedCodecBound>& outListBound )
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
            FixedCodecBound   bound;
            bound._bPresent = SW_TRUE;
            bound._text     = string( term );
            if ( FixedCodecBoundResolverInternal::isNumericTerm( term ) )
            {
                if ( StringUtil::parseDouble( term, bound._value ) == false )
                {
                    fixed_string<constant::buffer::kMax512> message;
                    formatstring( message.data(), message.capacity(), "range term '%#' is not a number", bound._text );
                    state.reportError( prop, message.view() );
                    return false;
                }
                bound._bLiteral = SW_TRUE;
                // 경계는 float32 로 보관합니다(`PropertyMetadata::_minRange`). 정수 경계는 float32 로 정확히 나타나야 두 형식이 같은 값을 봅니다.
                const bool bWholeNumber = std::floor( bound._value ) == bound._value;
                const bool bExact       = static_cast<float64>( static_cast<float32>( bound._value ) ) == bound._value;
                if ( bInteger && bWholeNumber == false )
                {
                    fixed_string<constant::buffer::kMax512> message;
                    formatstring( message.data(), message.capacity(), "integer range term '%#' is not a whole number", bound._text );
                    state.reportError( prop, message.view() );
                    return false;
                }
                if ( bInteger && bExact == false )
                {
                    fixed_string<constant::buffer::kMax512> message;
                    formatstring( message.data(), message.capacity(), "integer range term '%#' is not exactly representable as float32 - Min/Max are stored as float32", bound._text );
                    state.reportError( prop, message.view() );
                    return false;
                }
                outListBound.push_back( std::move( bound ) );
                continue;
            }

            const bool                                        bNegative = term.front() == '-';
            const string                                      name( bNegative ? term.substr( 1 ) : term );
            const FixedCodecBoundResolverInternal::FindResult result = FixedCodecBoundResolverInternal::findRangeName( state, prop, name, bInteger, bound );
            if ( result == FixedCodecBoundResolverInternal::FindResult::Error )
                return false;
            if ( result == FixedCodecBoundResolverInternal::FindResult::NotFound )
            {
                if ( state._listScope.size() > 1 )
                {
                    fixed_string<constant::buffer::kMax512> message;
                    formatstring( message.data(), message.capacity(), "range name '%#' is not a field of this type or of any enclosing type", name );
                    state.reportError( prop, message.view() );
                    return false;
                }
                // 자기 타입에는 없습니다. 같은 헤더의 바깥 타입이 펼치며 풀 수 있고, 그 타입 혼자서는 코덱이 없습니다.
                if ( state._unresolvedName.empty() )
                {
                    state._unresolvedName   = name;
                    state._unresolvedMember = prop._memberName;
                    state._unresolvedLine   = prop._line;
                }
                fixed_string<constant::buffer::kMax512> reason;
                formatstring( reason.data(), reason.capacity(), "range name '%#' refers to a field of an enclosing type", name );
                state.markIneligible( prop, reason.view() );
                return false;
            }
            bound._bNegate = bNegative ? SW_TRUE : SW_FALSE;
            outListBound.push_back( std::move( bound ) );
        }
        return true;
    }

    const FixedCodecBound& FixedCodecBoundResolver::pickBound( const vector<FixedCodecBound>& listBound, const size_t elementIndex )
    {
        return listBound.size() == 1 ? listBound.front() : listBound[elementIndex];
    }

    string FixedCodecBoundResolver::makeFieldBound( const FixedCodecBound& bound )
    {
        StringBuilder<constant::buffer::kMax1024> text;
        text.appendFormat( "::sw::FixedBound::field( %#u, offsetof( ::%#, %# ), ::sw::getFixedNumberType< decltype( ::%#::%# ) >(), %# )", bound._scopeDepth, bound._ownerFqn,
                           bound._fieldName, bound._ownerFqn, bound._fieldName, bound._bNegate == SW_TRUE ? "true" : "false" );
        return string( text.view() );
    }

    string FixedCodecBoundResolver::makeIntegerBound( const FixedCodecBound& bound )
    {
        if ( bound._bPresent == SW_FALSE )
            return "::sw::FixedBound::none()";
        if ( bound._bLiteral == SW_FALSE )
            return makeFieldBound( bound );
        StringBuilder<constant::buffer::kMax256> text;
        text.appendFormat( "::sw::FixedBound::integer( %# )", static_cast<int64>( bound._value ) );
        return string( text.view() );
    }

    string FixedCodecBoundResolver::makeFloatBound( const FixedCodecBound& bound, const bool bFloat32 )
    {
        if ( bound._bPresent == SW_FALSE )
            return "::sw::FixedBound::none()";
        if ( bound._bLiteral == SW_FALSE )
            return makeFieldBound( bound );
        // 숫자는 적힌 글을 C++ 리터럴로 그대로 내 컴파일러가 값을 정한다. `Step` 자리는 float32 로 바꾼 값이고, 넓혀 실어도 되돌리면 같다.
        StringBuilder<constant::buffer::kMax256> text;
        if ( bFloat32 )
            text.appendFormat( "::sw::FixedBound::real( static_cast<float64>( static_cast<float32>( %# ) ) )", bound._text );
        else
            text.appendFormat( "::sw::FixedBound::real( static_cast<float64>( %# ) )", bound._text );
        return string( text.view() );
    }
} // namespace sw

namespace sw
{
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

    void FixedCodecValueEmitter::appendEnumCanonical( FixedCanonicalBuilder& out, const ParsedFixedEnum& enumInfo )
    {
        out.appendFormat( "enum{%#;%#;", enumInfo._fullyQualifiedName, enumInfo._underlyingTypeName );
        out.append( enumInfo._bIsBitFlag == SW_TRUE ? "flags" : "plain" );
        if ( enumInfo._countEnumerator.empty() == false )
            out.appendFormat( ";count=%#", enumInfo._countEnumerator );
        for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
        {
            out.appendFormat( ";%#=%#", enumerator._name, static_cast<int64>( enumerator._value ) );
        }
        out.append( '}' );
    }

    void FixedCodecValueEmitter::appendFieldHead( FixedCanonicalBuilder& out, const ParsedPropertyInfo& prop )
    {
        out.appendFormat( "F=%#:%#", prop._memberName, prop._typeName );
        if ( prop._bIsBitField == SW_TRUE )
            out.append( ":bit" );
        if ( prop._minRangeExpr._text.empty() == false )
            out.appendFormat( ":min=%#", prop._minRangeExpr._text );
        if ( prop._maxRangeExpr._text.empty() == false )
            out.appendFormat( ":max=%#", prop._maxRangeExpr._text );
        if ( prop._bHasStep == SW_TRUE )
            out.appendFormat( ":step=%#", FixedCodecEmitState::formatFloat( static_cast<float64>( prop._step ) ) );
        if ( prop._bHasMaxCount == SW_TRUE )
            out.appendFormat( ":maxcount=%#", static_cast<int64>( prop._maxCount ) );
        out.append( ':' );
    }

    FixedCodecNode FixedCodecValueEmitter::makeInteger( const FixedCodecSite& site, const FixedCodecBound& minBound, const FixedCodecBound& maxBound )
    {
        StringBuilder<constant::buffer::kMax2048> call;
        call.appendFormat( "::sw::FixedFieldDesc::makeInteger( %#, ::sw::getFixedNumberType< %# >(), %#, %#", site._offsetExpr, site._typeExpr,
                           FixedCodecBoundResolver::makeIntegerBound( minBound ), FixedCodecBoundResolver::makeIntegerBound( maxBound ) );
        return FixedCodecNode( string( call.view() ), FixedCodecNode::ChildArgs::None );
    }

    FixedCodecNode FixedCodecValueEmitter::makeFloat( const FixedCodecSite& site, const FixedValueKind kind, const FixedCodecBound& minBound, const FixedCodecBound& maxBound )
    {
        const ParsedPropertyInfo&                 prop = *site._pProp;
        StringBuilder<constant::buffer::kMax2048> call;
        if ( prop._bHasStep == SW_TRUE )
        {
            // `Step` 은 float32 · float32 수학 타입만 받는다(아니면 `emitProperty` 가 생성 오류로 멈춘다). 간격은 %.9g 라 float32 로 되돌려도 같은 값이다.
            call.appendFormat( "::sw::FixedFieldDesc::makeFloat32( %#, %#, %#, static_cast<float32>( %# )", site._offsetExpr, FixedCodecBoundResolver::makeFloatBound( minBound, true ),
                               FixedCodecBoundResolver::makeFloatBound( maxBound, true ), FixedCodecEmitState::formatFloat( static_cast<float64>( prop._step ) ) );
        }
        else if ( kind == FixedValueKind::Float32 )
        {
            call.appendFormat( "::sw::FixedFieldDesc::makeFloat32( %#, %#, %#, 0.0f", site._offsetExpr, FixedCodecBoundResolver::makeFloatBound( minBound, false ),
                               FixedCodecBoundResolver::makeFloatBound( maxBound, false ) );
        }
        else
        {
            call.appendFormat( "::sw::FixedFieldDesc::makeFloat64( %#, %#, %#", site._offsetExpr, FixedCodecBoundResolver::makeFloatBound( minBound, false ),
                               FixedCodecBoundResolver::makeFloatBound( maxBound, false ) );
        }
        return FixedCodecNode( string( call.view() ), FixedCodecNode::ChildArgs::None );
    }

    FixedCodecNode FixedCodecValueEmitter::makeBool( const FixedCodecSite& site )
    {
        StringBuilder<constant::buffer::kMax2048> call;
        if ( site._bBitField == SW_TRUE )
        {
            // 비트필드는 주소가 없다. 바로 위 객체를 받는 접근 함수 둘을 낸다(언리얼 `FBoolProperty` 의 생성 SetBit 자리). 1 비트라 값 비교와 쓰는 비트가 같다.
            const string& owner  = site._ownerFqn;
            const string& member = site._pProp->_memberName;
            call.appendFormat( "::sw::FixedFieldDesc::makeBitFieldBool( []( const void* pOwner ) -> bool { return static_cast<const ::%#*>( pOwner )->%# != 0; }, "
                               "[]( void* pOwner, const bool bValue ) { ::%#& owner = *static_cast< ::%#* >( pOwner ); owner.%# = static_cast<decltype( owner.%# )>( bValue ? 1 : 0 ); }",
                               owner, member, owner, owner, member, member );
        }
        else
        {
            call.appendFormat( "::sw::FixedFieldDesc::makeBool( %#", site._offsetExpr );
        }
        return FixedCodecNode( string( call.view() ), FixedCodecNode::ChildArgs::None );
    }

    bool FixedCodecValueEmitter::emitEnum( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedEnum& enumInfo, vector<FixedCodecNode>& outListNode )
    {
        StringBuilder<constant::buffer::kMax256> enumTypeText;
        enumTypeText.appendFormat( "::%#", enumInfo._fullyQualifiedName );
        const string_view                         enumType = enumTypeText.view();
        StringBuilder<constant::buffer::kMax8192> call;
        if ( enumInfo._bIsBitFlag == SW_TRUE )
        {
            // 비트 플래그는 켜진 비트의 조합이라 범위가 없다. 기반 타입 폭으로 쓰고, 등록된 비트(모든 열거자의 합) 밖은 해석기가 거절한다.
            StringBuilder<constant::buffer::kMax4096> mask;
            for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
            {
                if ( mask.size() > 0 )
                    mask.append( " | " );
                mask.appendFormat( "static_cast<uint64>( %#::%# )", enumType, enumerator._name );
            }
            call.appendFormat( "::sw::FixedFieldDesc::makeFlagEnum( %#, ::sw::getFixedNumberType< %# >(), %#", site._offsetExpr, enumType,
                               mask.size() > 0 ? mask.view() : string_view( "0" ) );
            outListNode.emplace_back( string( call.view() ), FixedCodecNode::ChildArgs::None );
            return true;
        }

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
            fixed_string<constant::buffer::kMax512> message;
            formatstring( message.data(), message.capacity(), "enum '%#' spans more than 32 bits of values", enumInfo._fullyQualifiedName );
            state.reportError( *site._pProp, message.view() );
            return false;
        }
        state.addEnum( enumInfo );
        uint32        valueCount = 0;
        vector<int64> listSeen;
        for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
        {
            const bool bDuplicate = std::find( listSeen.begin(), listSeen.end(), enumerator._value ) != listSeen.end();
            if ( enumerator._name == enumInfo._countEnumerator || bDuplicate )
                continue;
            listSeen.push_back( enumerator._value );
            ++valueCount;
        }
        StringBuilder<constant::buffer::kMax512> maxExpr;
        if ( enumInfo._countEnumerator.empty() )
            maxExpr.appendFormat( "static_cast<int64>( %#::%# )", enumType, pMax->_name );
        else
            maxExpr.appendFormat( "static_cast<int64>( %#::%# ) - 1", enumType, enumInfo._countEnumerator );
        call.appendFormat( "::sw::FixedFieldDesc::makeEnum( %#, ::sw::getFixedNumberType< %# >(), static_cast<int64>( %#::%# ), %#, %#, %#u", site._offsetExpr, enumType, enumType,
                           pMin->_name, maxExpr.view(), FixedCodecEmitState::makeEnumValueTableName( enumInfo ), valueCount );
        outListNode.emplace_back( string( call.view() ), FixedCodecNode::ChildArgs::None );
        return true;
    }

    FixedCodecSite FixedCodecValueEmitter::makeElementSite( const FixedCodecSite& parent )
    {
        FixedCodecSite child = parent;
        child._ownerFqn.clear();
        child._offsetExpr = "0";
        StringBuilder<constant::buffer::kMax1024> typeExpr;
        typeExpr.appendFormat( "::sw::FixedElementType< %# >", parent._typeExpr );
        child._typeExpr  = string( typeExpr.view() );
        child._bBitField = SW_FALSE;
        return child;
    }

    void FixedCodecValueEmitter::emitContainer( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode )
    {
        const bool                                bVector = type._kind == FixedValueKind::Vector;
        StringBuilder<constant::buffer::kMax2048> call;
        if ( bVector )
        {
            call.appendFormat( "::sw::FixedFieldDesc::makeVector( %#, %#u, &::sw::FixedVectorOpsOf< %# >::kInstance", site._offsetExpr, site._pProp->_maxCount, site._typeExpr );
        }
        else
        {
            call.appendFormat( "::sw::FixedFieldDesc::makeFixedArray( %#, &::sw::FixedArrayOpsOf< %# >::kInstance", site._offsetExpr, site._typeExpr );
        }
        FixedCodecNode node( string( call.view() ), FixedCodecNode::ChildArgs::Element );
        outCanonical.append( bVector ? "vec<" : "arr<" );
        emitValue( state, makeElementSite( site ), *type._element, outCanonical, node._listChild );
        outCanonical.append( '>' );
        outListNode.push_back( std::move( node ) );
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

    void FixedCodecValueEmitter::emitReflect( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode )
    {
        const string&         fqn     = type._fullyQualifiedName;
        const ParsedTypeInfo* pNested = findHeaderType( state, fqn );
        if ( canInline( state, pNested ) )
        {
            StringBuilder<constant::buffer::kMax512> call;
            call.appendFormat( "::sw::FixedFieldDesc::makeInline( %#", site._offsetExpr );
            FixedCodecNode node( string( call.view() ), FixedCodecNode::ChildArgs::Group );
            outCanonical.appendFormat( "inline{T=%#|", fqn );
            state._listInlineFqn.push_back( fqn );
            emitProperties( state, *pNested, outCanonical, node._listChild );
            state._listInlineFqn.pop_back();
            outCanonical.append( '}' );
            outListNode.push_back( std::move( node ) );
            // 펼친 타입이 혼자서는 풀지 못하던 바깥 범위 이름을 여기서 풀었습니다. 그 타입의 "바깥 이름" 오류는 사라집니다.
            if ( state._bError == SW_FALSE )
                state._listResolvedFqn.push_back( fqn );
            return;
        }

        outCanonical.appendFormat( "ref{%#}", fqn );
        const bool                               bSelf = fqn == state._pRoot->_fullyQualifiedName;
        StringBuilder<constant::buffer::kMax512> call;
        if ( bSelf )
            call.appendFormat( "::sw::FixedFieldDesc::makeCodec( %#, ::sw::FixedFieldDesc::kSelfDependency", site._offsetExpr );
        else
            call.appendFormat( "::sw::FixedFieldDesc::makeCodec( %#, %#u", site._offsetExpr, state.addDependency( site._pProp->_memberName, fqn ) );
        outListNode.emplace_back( string( call.view() ), FixedCodecNode::ChildArgs::None );
    }

    void FixedCodecValueEmitter::emitValue( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode )
    {
        switch ( type._kind )
        {
            case FixedValueKind::Ineligible:
            {
                break;
            }
            case FixedValueKind::Bool:
            {
                outCanonical.append( "bool" );
                outListNode.push_back( makeBool( site ) );
                break;
            }
            case FixedValueKind::Integer:
            {
                outCanonical.append( "int" );
                outListNode.push_back( makeInteger( site, FixedCodecBoundResolver::pickBound( site._listMin, 0 ), FixedCodecBoundResolver::pickBound( site._listMax, 0 ) ) );
                break;
            }
            case FixedValueKind::Float32:
            case FixedValueKind::Float64:
            {
                outCanonical.append( type._kind == FixedValueKind::Float32 ? "f32" : "f64" );
                outListNode.push_back( makeFloat( site, type._kind, FixedCodecBoundResolver::pickBound( site._listMin, 0 ), FixedCodecBoundResolver::pickBound( site._listMax, 0 ) ) );
                break;
            }
            case FixedValueKind::Math:
            {
                const vector<string>& listMember = type._listMathMember;
                outCanonical.appendFormat( "math(%#)", type._typeName );
                StringBuilder<constant::buffer::kMax512> call;
                call.appendFormat( "::sw::FixedFieldDesc::makeMath( %#", site._offsetExpr );
                FixedCodecNode node( string( call.view() ), FixedCodecNode::ChildArgs::Group );
                for ( size_t memberIndex = 0; memberIndex < listMember.size(); ++memberIndex )
                {
                    FixedCodecSite                           child = makeElementSite( site );
                    StringBuilder<constant::buffer::kMax256> expr;
                    expr.appendFormat( "offsetof( ::sw::%#, %# )", type._typeName, listMember[memberIndex] );
                    child._offsetExpr = string( expr.view() );
                    expr.clear();
                    expr.appendFormat( "decltype( ::sw::%#::%# )", type._typeName, listMember[memberIndex] );
                    child._typeExpr                 = string( expr.view() );
                    const FixedCodecBound& minBound = FixedCodecBoundResolver::pickBound( site._listMin, memberIndex );
                    const FixedCodecBound& maxBound = FixedCodecBoundResolver::pickBound( site._listMax, memberIndex );
                    if ( type._bIntegerMath == SW_TRUE )
                        node._listChild.push_back( makeInteger( child, minBound, maxBound ) );
                    else
                        node._listChild.push_back( makeFloat( child, FixedValueKind::Float32, minBound, maxBound ) );
                }
                outListNode.push_back( std::move( node ) );
                break;
            }
            case FixedValueKind::Enum:
            {
                appendEnumCanonical( outCanonical, *type._enum );
                (void)emitEnum( state, site, *type._enum, outListNode );
                break;
            }
            case FixedValueKind::String:
            case FixedValueKind::Bitset:
            {
                const bool                               bString = type._kind == FixedValueKind::String;
                StringBuilder<constant::buffer::kMax512> call;
                outCanonical.append( bString ? "str" : "bits" );
                call.appendFormat( "::sw::FixedFieldDesc::%#( %#, %#u", bString ? "makeString" : "makeBitset", site._offsetExpr, site._pProp->_maxCount );
                outListNode.emplace_back( string( call.view() ), FixedCodecNode::ChildArgs::None );
                break;
            }
            case FixedValueKind::Vector:
            case FixedValueKind::FixedArray:
            {
                emitContainer( state, site, type, outCanonical, outListNode );
                break;
            }
            case FixedValueKind::Reflect:
            {
                emitReflect( state, site, type, outCanonical, outListNode );
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

    void FixedCodecValueEmitter::emitProperty( FixedCodecEmitState& state, const ParsedTypeInfo& owner, const ParsedPropertyInfo& prop, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode )
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
        const bool   bHasRange    = prop._minRangeExpr._text.empty() == false || prop._maxRangeExpr._text.empty() == false;
        const bool   bMath        = leafKind == FixedValueKind::Math;
        const size_t elementCount = bMath ? leaf._listMathMember.size() : 1;
        const bool   bIntegerMath = bMath && leaf._bIntegerMath == SW_TRUE;

        // 1) 키 검사. 생성 오류입니다
        const bool bRangeOnNonNumeric = bHasRange && bNumeric == false && leafKind != FixedValueKind::Ineligible;
        if ( bRangeOnNonNumeric )
            state.reportError( prop, "Min/Max apply to integer, floating-point and math fields only" );
        if ( prop._bHasStep == SW_TRUE )
        {
            const bool bFloat32 = leafKind == FixedValueKind::Float32 || ( leafKind == FixedValueKind::Math && bIntegerMath == false );
            if ( bFloat32 == false )
                state.reportError( prop, "Step applies to float32 fields (and float32 math types) only" );
            else if ( ( prop._step > 0.0f ) == false )
                state.reportError( prop, "Step must be greater than zero" );
            else if ( prop._minRangeExpr._text.empty() || prop._maxRangeExpr._text.empty() )
                state.reportError( prop, "Step needs both Min and Max" );
        }
        if ( prop._bHasMaxCount == SW_TRUE && variable == 0 )
            state.reportError( prop, "MaxCount applies to string, vector and DynamicBitset fields only" );

        FixedCodecSite                           site;
        StringBuilder<constant::buffer::kMax512> expr;
        site._pProp    = &prop;
        site._ownerFqn = owner._fullyQualifiedName;
        expr.appendFormat( "offsetof( ::%#, %# )", owner._fullyQualifiedName, prop._memberName );
        site._offsetExpr = string( expr.view() );
        expr.clear();
        expr.appendFormat( "decltype( ::%#::%# )", owner._fullyQualifiedName, prop._memberName );
        site._typeExpr  = string( expr.view() );
        site._bBitField = prop._bIsBitField;
        site._listMin.emplace_back();
        site._listMax.emplace_back();
        if ( bNumeric )
        {
            const bool bInteger = leafKind == FixedValueKind::Integer || bIntegerMath;
            if ( FixedCodecBoundResolver::resolveBounds( state, prop, prop._minRangeExpr._text, bInteger, site._listMin ) == false ||
                 FixedCodecBoundResolver::resolveBounds( state, prop, prop._maxRangeExpr._text, bInteger, site._listMax ) == false )
                return;
            const bool bMinCountMismatch = site._listMin.size() != 1 && site._listMin.size() != elementCount;
            const bool bMaxCountMismatch = site._listMax.size() != 1 && site._listMax.size() != elementCount;
            if ( bMinCountMismatch || bMaxCountMismatch )
            {
                fixed_string<constant::buffer::kMax256> message;
                formatstring( message.data(), message.capacity(), "Min/Max list has %#/%# terms but the value has %# elements", static_cast<int64>( site._listMin.size() ),
                              static_cast<int64>( site._listMax.size() ), static_cast<int64>( elementCount ) );
                state.reportError( prop, message.view() );
                return;
            }
            // 각의 숫자 경계는 도로 적혀 있고 메모리는 라디안이다 — 데이터 글 `180` 이 담기는 것과 같은 식으로 바꿔야 경계 값이 경계 안에 든다.
            // 필드 이름 경계는 같은 라디안 칸을 읽으므로 그대로다.
            if ( leaf._bAngle == SW_TRUE )
            {
                for ( vector<FixedCodecBound>* pListBound : { &site._listMin, &site._listMax } )
                {
                    for ( FixedCodecBound& bound : *pListBound )
                    {
                        if ( bound._bPresent == SW_FALSE || bound._bLiteral == SW_FALSE )
                            continue;
                        StringBuilder<constant::buffer::kMax256> radians;
                        radians.appendFormat( "::sw::Radians::fromDegrees( static_cast<float32>( %# ) ).radians()", bound._text );
                        bound._text = string( radians.view() );
                    }
                }
            }
        }

        // 압축 타입(half · unorm · snorm · OctahedralNormal · RGB10A2)은 인코딩 표에 없다. 포인터처럼 적격이 아닌 것과 달리 저장용으로 만든 타입이라
        // 코덱 없이 등록되면 넷 · 롤백에서야 드러나므로 여기서 멈춘다. 코덱에 실으려면 Layout 칸(`ReflectBuiltins.xxx`)이 성분 규칙을 갖거나 새 인코딩 종류를 더한다.
        if ( leaf._bPacked == SW_TRUE )
        {
            fixed_string<constant::buffer::kMax512> message;
            formatstring( message.data(), message.capacity(),
                          "packed type '%#' is not in the fixed layout encoding table - mark the field Transient or add a packed encoding to the generator", leaf._typeName );
            state.reportError( prop, message.view() );
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
        emitValue( state, site, type, outCanonical, outListNode );
        outCanonical.append( '|' );
    }

    void FixedCodecValueEmitter::emitProperties( FixedCodecEmitState& state, const ParsedTypeInfo& type, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode )
    {
        state._listScope.push_back( FixedCodecScopeFrame{ &type, 0 } );
        for ( size_t index = 0; index < type._listProperty.size(); ++index )
        {
            state._listScope.back()._limit = index;
            emitProperty( state, type, type._listProperty[index], outCanonical, outListNode );
        }
        state._listScope.pop_back();
    }
} // namespace sw
