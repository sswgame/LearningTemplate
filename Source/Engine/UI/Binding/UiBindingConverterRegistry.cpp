#include "pch.h"

#include "Engine/UI/Binding/UiBindingConverterRegistry.h"

#include "Core/Common/Defines.h"
#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct UiBindingConverterRegistryInternal
        {
            /** @brief 함수가 [_pBegin, _pEnd) 안인 변환기를 고릅니다(`removeCodeWithin`). */
            struct IsCodeWithin
            {
                const void* _pBegin;
                const void* _pEnd;

                bool operator()( const UiBindingConverter& converter ) const
                {
                    return IModuleUnloadListener::isAddressWithin( reinterpret_cast<const void*>( converter._pConvert ), _pBegin, _pEnd );
                }
            };

            /** @brief 1 분의 초입니다(Seconds 변환기 — m:ss). */
            static constexpr int64 kSecondsPerMinute = 60;
            /** @brief 비율 → 백분율 배수입니다(Percent 변환기). */
            static constexpr float64 kPercentScale = 100.0;

            /** @brief 0..1 비율을 "75%" 로 씁니다(반올림). */
            [[nodiscard]] static bool convertPercent( const UiBindingValue& in, UiBindingValue& outValue )
            {
                utf8 arrBuffer[constant::kMaxBuffer64];
                StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), static_cast<int64>( MathUtil::round( in.toNumber() * kPercentScale ) ) );
                outValue = UiBindingValue::makeText( string( arrBuffer ) + "%" );
                return true;
            }

            [[nodiscard]] static bool convertInvert( const UiBindingValue& in, UiBindingValue& outValue )
            {
                outValue = UiBindingValue::makeBool( in.toBool() == false );
                return true;
            }

            [[nodiscard]] static bool convertNotEmpty( const UiBindingValue& in, UiBindingValue& outValue )
            {
                outValue = UiBindingValue::makeBool( in.toText().empty() == false );
                return true;
            }

            [[nodiscard]] static bool convertIsZero( const UiBindingValue& in, UiBindingValue& outValue )
            {
                outValue = UiBindingValue::makeBool( in.toNumber() == 0.0 );
                return true;
            }

            /** @brief 초를 "m:ss" 로 씁니다(음수는 0, 소수는 올린다 — 카운트다운이 0 에 닿기 전까지 1 을 보인다). */
            [[nodiscard]] static bool convertSeconds( const UiBindingValue& in, UiBindingValue& outValue )
            {
                const int64 totalSeconds = static_cast<int64>( MathUtil::ceil( MathUtil::max( 0.0, in.toNumber() ) ) );
                const int64 minutes      = totalSeconds / kSecondsPerMinute;
                const int64 seconds      = totalSeconds % kSecondsPerMinute;
                utf8        arrMinuteText[constant::kMaxBuffer32];
                utf8        arrSecondText[constant::kMaxBuffer32];
                StringUtil::formatNumber( arrMinuteText, sizeof( arrMinuteText ), minutes );
                StringUtil::formatNumber( arrSecondText, sizeof( arrSecondText ), seconds );
                outValue = UiBindingValue::makeText( string( arrMinuteText ) + ( seconds < 10 ? ":0" : ":" ) + arrSecondText );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiBindingConverterRegistry::UiBindingConverterRegistry()
        : _registry{}
    {
        registerEngineConverters();
    }

    void UiBindingConverterRegistry::registerEngineConverters()
    {
        using Internal = UiBindingConverterRegistryInternal;
        registerConverter( UiBindingConverter{ "Percent", &Internal::convertPercent, UiBindingValueKind::Number, UiBindingValueKind::Text } );
        registerConverter( UiBindingConverter{ "Invert", &Internal::convertInvert, UiBindingValueKind::Bool, UiBindingValueKind::Bool } );
        registerConverter( UiBindingConverter{ "NotEmpty", &Internal::convertNotEmpty, UiBindingValueKind::Text, UiBindingValueKind::Bool } );
        registerConverter( UiBindingConverter{ "IsZero", &Internal::convertIsZero, UiBindingValueKind::Number, UiBindingValueKind::Bool } );
        registerConverter( UiBindingConverter{ "Seconds", &Internal::convertSeconds, UiBindingValueKind::Number, UiBindingValueKind::Text } );
    }

    void UiBindingConverterRegistry::registerConverter( const UiBindingConverter& converter )
    {
        _registry.addOrReplace( converter._name, converter );
    }

    const UiBindingConverter* UiBindingConverterRegistry::findConverter( const hashed_string& name ) const
    {
        return _registry.find( name );
    }

    uint32 UiBindingConverterRegistry::removeCodeWithin( const void* pBegin, const void* pEnd )
    {
        return _registry.removeIf( UiBindingConverterRegistryInternal::IsCodeWithin{ pBegin, pEnd } );
    }
} // namespace sw
