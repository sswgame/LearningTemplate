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
            /** @brief 함수가 [_pBegin, _pEnd) 안인 변환기를 고릅니다(`removeCodeWithin`). */
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
        , _listEngineConverter{}
        , _listOwner{}
        , _nextOwnerID{ 1 }
        , _revision{ 0 }
    {
        registerEngineConverters();
    }

    void UIBindingConverterRegistry::registerEngineConverters()
    {
        using Internal       = UIBindingConverterRegistryInternal;
        _listEngineConverter = {
            UIBindingConverter{ "Percent",  &Internal::convertPercent, UIBindingValueKind::Number, UIBindingValueKind::Text},
            UIBindingConverter{  "Invert",   &Internal::convertInvert,   UIBindingValueKind::Bool, UIBindingValueKind::Bool},
            UIBindingConverter{"NotEmpty", &Internal::convertNotEmpty,   UIBindingValueKind::Text, UIBindingValueKind::Bool},
            UIBindingConverter{  "IsZero",   &Internal::convertIsZero, UIBindingValueKind::Number, UIBindingValueKind::Bool},
            UIBindingConverter{ "Seconds",  &Internal::convertSeconds, UIBindingValueKind::Number, UIBindingValueKind::Text},
        };
        for ( const UIBindingConverter& converter : _listEngineConverter )
        {
            _registry.addOrReplace( converter._name, converter, "UI binding converter" );
        }
    }

    Subscription UIBindingConverterRegistry::registerConverter( const UIBindingConverter& converter )
    {
        _registry.addOrReplace( converter._name, converter, "UI binding converter" );
        ++_revision;
        const uint64 id = _nextOwnerID++;
        if ( ConverterOwner* pOwner = VectorUtil::findBy<&ConverterOwner::_name>( _listOwner, converter._name ) )
            pOwner->_id = id; // 교체 — 옛 구독은 이제 아무것도 거두지 않는다
        else
            _listOwner.push_back( ConverterOwner{ converter._name, id } );
        return _subscriptions.issue( id );
    }

    void UIBindingConverterRegistry::releaseSubscription( uint64 id )
    {
        const std::optional<size_t> index = VectorUtil::findIndexBy<&ConverterOwner::_id>( _listOwner, id );
        if ( index.has_value() == false )
            return; // 다른 쪽이 그 사이 교체했다 — 그 등록을 지우지 않는다
        const hashed_string name = _listOwner[*index]._name;
        ++_revision;
        _listOwner.erase( _listOwner.begin() + static_cast<ptrdiff_t>( *index ) );
        if ( const UIBindingConverter* pEngine = VectorUtil::findBy<&UIBindingConverter::_name>( _listEngineConverter, name ) )
            _registry.addOrReplace( name, *pEngine, "UI binding converter" ); // 덮었던 엔진 기본으로 돌아간다
        else
            _registry.addOrReplace( name, UIBindingConverter{ name, nullptr, UIBindingValueKind::None, UIBindingValueKind::None }, "UI binding converter" );
    }

    const UIBindingConverter* UIBindingConverterRegistry::findConverter( const hashed_string& name ) const
    {
        const UIBindingConverter* pConverter = _registry.find( name );
        return ( pConverter != nullptr && pConverter->_pConvert != nullptr ) ? pConverter : nullptr; // 거둔 이름은 빈 함수로 남아 있다
    }
} // namespace sw
