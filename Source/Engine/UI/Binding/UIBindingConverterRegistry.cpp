#include "pch.h"

#include "Engine/UI/Binding/UIBindingConverterRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/Defines.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct UIBindingConverterRegistryInternal
        {
            /** @brief 비율 → 백분율 배수입니다(Percent 변환기). */
            static constexpr float64 kPercentScale = 100.0;

            /** @brief 0..1 비율을 "75%" 로 씁니다(반올림). */
            [[nodiscard]] static bool convertPercent( const UIBindingValue& in, UIBindingValue& outValue )
            {
                utf8 arrBuffer[constant::kMaxBuffer64];
                StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), static_cast<int64>( MathUtil::round( in.toNumber() * kPercentScale ) ) );
                outValue = UIBindingValue::makeText( string( arrBuffer ) + "%" );
                return true;
            }

            [[nodiscard]] static bool convertInvert( const UIBindingValue& in, UIBindingValue& outValue )
            {
                outValue = UIBindingValue::makeBool( in.toBool() == false );
                return true;
            }

            [[nodiscard]] static bool convertNotEmpty( const UIBindingValue& in, UIBindingValue& outValue )
            {
                outValue = UIBindingValue::makeBool( in.toText().empty() == false );
                return true;
            }

            [[nodiscard]] static bool convertIsZero( const UIBindingValue& in, UIBindingValue& outValue )
            {
                outValue = UIBindingValue::makeBool( in.toNumber() == 0.0 );
                return true;
            }

            /** @brief 초를 "m:ss" 로 씁니다(음수는 0 이고, 소수는 올립니다. 카운트다운이 0 에 닿기 전까지 1 을 보여 줍니다). */
            [[nodiscard]] static bool convertSeconds( const UIBindingValue& in, UIBindingValue& outValue )
            {
                const int64 totalSeconds = static_cast<int64>( MathUtil::ceil( MathUtil::max( 0.0, in.toNumber() ) ) );
                const int64 minutes      = totalSeconds / constant::kSecondsPerMinute;
                const int64 seconds      = totalSeconds % constant::kSecondsPerMinute;
                utf8        arrMinuteText[constant::kMaxBuffer32];
                utf8        arrSecondText[constant::kMaxBuffer32];
                StringUtil::formatNumber( arrMinuteText, sizeof( arrMinuteText ), minutes );
                StringUtil::formatNumber( arrSecondText, sizeof( arrSecondText ), seconds );
                outValue = UIBindingValue::makeText( string( arrMinuteText ) + ( seconds < 10 ? ":0" : ":" ) + arrSecondText );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIBindingConverterRegistry::UIBindingConverterRegistry()
        : _registry{}
    {
        registerEngineConverters();
    }

    void UIBindingConverterRegistry::registerEngineConverters()
    {
        using Internal                                = UIBindingConverterRegistryInternal;
        const UIBindingConverter arrEngineConverter[] = {
            UIBindingConverter{ "Percent",  &Internal::convertPercent, UIBindingValueKind::Number, UIBindingValueKind::Text},
            UIBindingConverter{  "Invert",   &Internal::convertInvert,   UIBindingValueKind::Bool, UIBindingValueKind::Bool},
            UIBindingConverter{"NotEmpty", &Internal::convertNotEmpty,   UIBindingValueKind::Text, UIBindingValueKind::Bool},
            UIBindingConverter{  "IsZero",   &Internal::convertIsZero, UIBindingValueKind::Number, UIBindingValueKind::Bool},
            UIBindingConverter{ "Seconds",  &Internal::convertSeconds, UIBindingValueKind::Number, UIBindingValueKind::Text},
        };
        for ( const UIBindingConverter& converter : arrEngineConverter )
        {
            _registry.addOrReplace( converter._name, converter, "UI binding converter" );
        }
    }

    Subscription UIBindingConverterRegistry::registerConverter( const UIBindingConverter& converter )
    {
        return _registry.addOwned( converter._name, converter, "UI binding converter" );
    }

    const UIBindingConverter* UIBindingConverterRegistry::findConverter( const hashed_string& name ) const
    {
        const UIBindingConverter* pConverter = _registry.find( name );
        return ( pConverter != nullptr && pConverter->_pConvert != nullptr ) ? pConverter : nullptr; // 함수 없이 올린 변환기는 없는 것으로 본다
    }
} // namespace sw
