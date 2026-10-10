#include "pch.h"

#include "Engine/UI/Binding/UIBindingConverterRegistry.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

namespace sw
{
    namespace
    {
        struct UIBindingConverterRegistryInternal
        {
            /** @brief 함수가 [_pBegin, _pEnd) 안인 변환기를 고릅니다(`removeCodeWithin`). */
            struct IsCodeWithin
            {
                const void* _pBegin;
                const void* _pEnd;

                bool operator()( const UIBindingConverter& converter ) const
                {
                    return IModuleUnloadListener::isAddressWithin( reinterpret_cast<const void*>( converter._pConvert ), _pBegin, _pEnd );
                }
            };

            /** @brief 1 분의 초입니다(Seconds 변환기 — m:ss). */
            static constexpr int64 kSecondsPerMinute = 60;
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

            /** @brief 초를 "m:ss" 로 씁니다(음수는 0, 소수는 올린다 — 카운트다운이 0 에 닿기 전까지 1 을 보인다). */
            [[nodiscard]] static bool convertSeconds( const UIBindingValue& in, UIBindingValue& outValue )
            {
                const int64 totalSeconds = static_cast<int64>( MathUtil::ceil( MathUtil::max( 0.0, in.toNumber() ) ) );
                const int64 minutes      = totalSeconds / kSecondsPerMinute;
                const int64 seconds      = totalSeconds % kSecondsPerMinute;
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
        using Internal = UIBindingConverterRegistryInternal;
        registerConverter( UIBindingConverter{ "Percent", &Internal::convertPercent, UIBindingValueKind::Number, UIBindingValueKind::Text } );
        registerConverter( UIBindingConverter{ "Invert", &Internal::convertInvert, UIBindingValueKind::Bool, UIBindingValueKind::Bool } );
        registerConverter( UIBindingConverter{ "NotEmpty", &Internal::convertNotEmpty, UIBindingValueKind::Text, UIBindingValueKind::Bool } );
        registerConverter( UIBindingConverter{ "IsZero", &Internal::convertIsZero, UIBindingValueKind::Number, UIBindingValueKind::Bool } );
        registerConverter( UIBindingConverter{ "Seconds", &Internal::convertSeconds, UIBindingValueKind::Number, UIBindingValueKind::Text } );
    }

    void UIBindingConverterRegistry::registerConverter( const UIBindingConverter& converter )
    {
        _registry.addOrReplace( converter._name, converter );
    }

    const UIBindingConverter* UIBindingConverterRegistry::findConverter( const hashed_string& name ) const
    {
        return _registry.find( name );
    }

    uint32 UIBindingConverterRegistry::removeCodeWithin( const void* pBegin, const void* pEnd )
    {
        return _registry.removeIf( UIBindingConverterRegistryInternal::IsCodeWithin{ pBegin, pEnd } );
    }
} // namespace sw
