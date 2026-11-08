#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecBoundResolver.h"

#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include <cmath>

namespace sw
{
    namespace
    {
        struct FixedCodecBoundResolverInternal
        {
            enum class LookupResult : uint8
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
            static LookupResult lookupRangeName( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& name, const bool bIntegerSite, string& outExpr )
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
                            state.reportError( prop, "range field '" + name + "' must be declared before this field - the reader needs its value first" );
                            return LookupResult::Error;
                        }
                        const FixedValueKind kind     = candidate._fixedType != nullptr ? candidate._fixedType->_kind : FixedValueKind::Ineligible;
                        const bool           bNumeric = kind == FixedValueKind::Integer || kind == FixedValueKind::Float32 || kind == FixedValueKind::Float64;
                        const bool           bStored  = candidate._bTransient == SW_FALSE && candidate._bIsBitField == SW_FALSE;
                        if ( ( bNumeric && bStored ) == false )
                        {
                            state.reportError( prop, "range field '" + name + "' must be a non-transient integer or floating-point field" );
                            return LookupResult::Error;
                        }
                        if ( bIntegerSite && kind != FixedValueKind::Integer )
                        {
                            state.reportError( prop, "range field '" + name + "' is floating-point but this field is an integer - the fraction would be cut" );
                            return LookupResult::Error;
                        }
                        outExpr = frameIt->_objectExpr + "." + name;
                        return LookupResult::Found;
                    }
                }
                return LookupResult::NotFound;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
                    state.reportError( prop, "range term '" + bound._text + "' is not a number" );
                    return false;
                }
                bound._bLiteral = SW_TRUE;
                // 경계는 float32 로 보관합니다(`PropertyMetadata::_minRange`). 정수 경계는 float32 로 정확히 나타나야 두 형식이 같은 값을 봅니다.
                const bool bWholeNumber = std::floor( bound._value ) == bound._value;
                const bool bExact       = static_cast<float64>( static_cast<float32>( bound._value ) ) == bound._value;
                if ( bInteger && bWholeNumber == false )
                {
                    state.reportError( prop, "integer range term '" + bound._text + "' is not a whole number" );
                    return false;
                }
                if ( bInteger && bExact == false )
                {
                    state.reportError( prop, "integer range term '" + bound._text + "' is not exactly representable as float32 - Min/Max are stored as float32" );
                    return false;
                }
                outListBound.push_back( std::move( bound ) );
                continue;
            }

            const bool                                          bNegative = term.front() == '-';
            const string                                        name( bNegative ? term.substr( 1 ) : term );
            string                                              fieldExpr;
            const FixedCodecBoundResolverInternal::LookupResult result = FixedCodecBoundResolverInternal::lookupRangeName( state, prop, name, bInteger, fieldExpr );
            if ( result == FixedCodecBoundResolverInternal::LookupResult::Error )
                return false;
            if ( result == FixedCodecBoundResolverInternal::LookupResult::NotFound )
            {
                if ( state._listScope.size() > 1 )
                {
                    state.reportError( prop, "range name '" + name + "' is not a field of this type or of any enclosing type" );
                    return false;
                }
                // 자기 타입에는 없습니다. 같은 헤더의 바깥 타입이 펼치며 풀 수 있고, 그 타입 혼자서는 코덱이 없습니다.
                if ( state._unresolvedName.empty() )
                {
                    state._unresolvedName   = name;
                    state._unresolvedMember = prop._memberName;
                    state._unresolvedLine   = prop._line;
                }
                state.markIneligible( prop, "range name '" + name + "' refers to a field of an enclosing type" );
                return false;
            }
            bound._expr = bNegative ? "( -" + fieldExpr + " )" : fieldExpr;
            outListBound.push_back( std::move( bound ) );
        }
        return true;
    }

    const FixedCodecBound& FixedCodecBoundResolver::pickBound( const vector<FixedCodecBound>& listBound, const size_t elementIndex )
    {
        return listBound.size() == 1 ? listBound.front() : listBound[elementIndex];
    }

    string FixedCodecBoundResolver::makeIntegerBound( const FixedCodecBound& bound, const bool bLower )
    {
        if ( bound._bPresent == SW_FALSE )
            return bLower ? "::sw::FixedCodecUtil::kNoLowerBound" : "::sw::FixedCodecUtil::kNoUpperBound";
        if ( bound._bLiteral == SW_TRUE )
            return "static_cast<int64>( " + FixedCodecEmitState::formatInteger( static_cast<int64>( bound._value ) ) + " )";
        return "static_cast<int64>( " + bound._expr + " )";
    }

    string FixedCodecBoundResolver::makeFloatBound( const FixedCodecBound& bound, const bool bLower, const utf8* pCastType )
    {
        if ( bound._bPresent == SW_FALSE )
            return bLower ? "::sw::FixedCodecUtil::kNoLowerFloatBound" : "::sw::FixedCodecUtil::kNoUpperFloatBound";
        if ( bound._bLiteral == SW_TRUE )
            return string( "static_cast<" ) + pCastType + ">( " + bound._text + " )";
        return string( "static_cast<" ) + pCastType + ">( " + bound._expr + " )";
    }
} // namespace sw
