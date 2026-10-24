#include "pch.h"

#include "Engine/Automation/AutomationThresholdBasis.h"

#include "Core/Container/StringUtil.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct AutomationThresholdBasisInternal
        {
            /** @brief `measured` 의 백엔드 이름입니다 — 쿠킹 표(`SW_RHI_BACKEND_TABLE`)의 명령줄 이름에 모든 백엔드가 같은 값일 때 쓰는 `all` 을 더했습니다. */
            static constexpr const utf8* kArrBackendName[] = {
#define SW_THRESHOLD_BASIS_BACKEND_NAME( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) CommandLineName,
                SW_RHI_BACKEND_TABLE( SW_THRESHOLD_BASIS_BACKEND_NAME )
#undef SW_THRESHOLD_BASIS_BACKEND_NAME
                    "all",
            };

            /** @brief `YYYY-MM-DD` 모양이면 true 입니다. */
            [[nodiscard]] static bool isDate( string_view text )
            {
                if ( text.size() != 10 )
                    return false;
                for ( size_t index = 0; index < text.size(); ++index )
                {
                    const bool bDash = index == 4 || index == 7;
                    if ( bDash ? text[index] != '-' : StringUtil::isDigitChar( text[index] ) == false )
                        return false;
                }
                return true;
            }

            /** @brief `<백엔드>:<값>` 이고 백엔드가 알려진 이름이며 값이 수이면 true 입니다. */
            [[nodiscard]] static bool isBackendValue( string_view token )
            {
                const size_t colon = token.find( ':' );
                if ( colon == string_view::npos )
                    return false;
                const string_view backend = token.substr( 0, colon );
                float32           value   = 0.0f;
                if ( StringUtil::parseFloat( token.substr( colon + 1 ), value ) == false )
                    return false;
                for ( const utf8* pName : kArrBackendName )
                {
                    if ( backend == pName )
                        return true;
                }
                return false;
            }

            /** @brief `YYYY-MM-DD <백엔드>:<값> …` 이면 true 입니다(값이 하나 이상). */
            [[nodiscard]] static bool isMeasuredRecord( string_view text )
            {
                size_t tokenCount = 0;
                bool   bValid     = true;
                StringUtil::forEachToken( text, " \t", [&tokenCount, &bValid]( string_view token )
                {
                    bValid = tokenCount == 0 ? isDate( token ) : isBackendValue( token );
                    ++tokenCount;
                    return bValid;
                } );
                return bValid && tokenCount >= 2;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationThresholdBasis::tryParse( const string* pMeasured, const string* pProvisional, AutomationThresholdBasis& outBasis, string& outError )
    {
        if ( ( pMeasured != nullptr ) == ( pProvisional != nullptr ) )
        {
            outError = "a threshold needs exactly one of measured=\"YYYY-MM-DD dx12:<value> …\" (a measured value) or provisional=\"<reason>\" (not measured yet)";
            return false;
        }
        if ( pMeasured != nullptr )
        {
            if ( AutomationThresholdBasisInternal::isMeasuredRecord( *pMeasured ) == false )
            {
                outError = "measured must be \"YYYY-MM-DD <backend>:<value> …\" with backend dx11 · dx12 · vk · gl · all (e.g. \"2026-10-22 all:0.095\")";
                return false;
            }
            outBasis._kind = AutomationThresholdBasisKind::Measured;
            outBasis._text = *pMeasured;
            return true;
        }
        if ( StringUtil::trim( string_view{ *pProvisional } ).empty() )
        {
            outError = "provisional must say why the threshold is not measured yet";
            return false;
        }
        outBasis._kind = AutomationThresholdBasisKind::Provisional;
        outBasis._text = *pProvisional;
        return true;
    }
} // namespace sw
