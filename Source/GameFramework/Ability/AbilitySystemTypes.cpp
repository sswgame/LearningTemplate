#include "pch.h"

#include "GameFramework/Ability/AbilitySystemTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 열거자 하나의 이름표입니다. */
        template <typename TEnum>
        struct AbilitySystemEnumName
        {
            TEnum       _value;
            const utf8* _pName;
        };

        struct AbilitySystemTypesInternal
        {
            static constexpr AbilitySystemEnumName<AttributeModOp> kArrModOpName[] = {
                {     AttributeModOp::Add,      "Add"},
                {AttributeModOp::Multiply, "Multiply"},
                {  AttributeModOp::Divide,   "Divide"},
                {AttributeModOp::Override, "Override"},
            };

            static constexpr AbilitySystemEnumName<EffectDurationPolicy> kArrDurationPolicyName[] = {
                {    EffectDurationPolicy::Instant,     "Instant"},
                {EffectDurationPolicy::HasDuration, "HasDuration"},
                {   EffectDurationPolicy::Infinite,    "Infinite"},
            };

            static constexpr AbilitySystemEnumName<EffectStackingPolicy> kArrStackingPolicyName[] = {
                {             EffectStackingPolicy::None,              "None"},
                {EffectStackingPolicy::AggregateByTarget, "AggregateByTarget"},
            };

            static constexpr AbilitySystemEnumName<EffectStackExpirationPolicy> kArrStackExpirationPolicyName[] = {
                {                   EffectStackExpirationPolicy::ClearEntireStack,                    "ClearEntireStack"},
                {EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration, "RemoveSingleStackAndRefreshDuration"},
            };

            static constexpr AbilitySystemEnumName<EffectMagnitudeSource> kArrMagnitudeSourceName[] = {
                { EffectMagnitudeSource::ScalableFloat,  "ScalableFloat"},
                {EffectMagnitudeSource::AttributeBased, "AttributeBased"},
                {   EffectMagnitudeSource::SetByCaller,    "SetByCaller"},
            };

            /** @brief 표에서 @p text 와 같은 이름(대소문자 무시)을 찾아 @p outValue 에 씁니다. */
            template <typename TEnum, size_t Count>
            [[nodiscard]] static bool parse( const AbilitySystemEnumName<TEnum> ( &arrName )[Count], string_view text, TEnum& outValue )
            {
                for ( const AbilitySystemEnumName<TEnum>& entry : arrName )
                {
                    if ( StringUtil::equals( text, string_view( entry._pName ), true ) )
                    {
                        outValue = entry._value;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 표에서 @p value 의 이름을 찾습니다. 없으면 "Unknown" 입니다. */
            template <typename TEnum, size_t Count>
            static const utf8* findName( const AbilitySystemEnumName<TEnum> ( &arrName )[Count], TEnum value )
            {
                for ( const AbilitySystemEnumName<TEnum>& entry : arrName )
                {
                    if ( entry._value == value )
                        return entry._pName;
                }
                return "Unknown";
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AbilityActivationResult result )
    {
        switch ( result )
        {
            case AbilityActivationResult::Activated:
                return "Activated";
            case AbilityActivationResult::InvalidSpec:
                return "InvalidSpec";
            case AbilityActivationResult::AlreadyActive:
                return "AlreadyActive";
            case AbilityActivationResult::OnCooldown:
                return "OnCooldown";
            case AbilityActivationResult::CannotAffordCost:
                return "CannotAffordCost";
            case AbilityActivationResult::MissingRequiredTag:
                return "MissingRequiredTag";
            case AbilityActivationResult::BlockedByTag:
                return "BlockedByTag";
            case AbilityActivationResult::RejectedByAbility:
                return "RejectedByAbility";
        }
        return "Unknown";
    }

    bool AbilitySystemEnumUtil::parseModOp( string_view text, AttributeModOp& outValue )
    {
        return AbilitySystemTypesInternal::parse( AbilitySystemTypesInternal::kArrModOpName, text, outValue );
    }

    bool AbilitySystemEnumUtil::parseDurationPolicy( string_view text, EffectDurationPolicy& outValue )
    {
        return AbilitySystemTypesInternal::parse( AbilitySystemTypesInternal::kArrDurationPolicyName, text, outValue );
    }

    bool AbilitySystemEnumUtil::parseStackingPolicy( string_view text, EffectStackingPolicy& outValue )
    {
        return AbilitySystemTypesInternal::parse( AbilitySystemTypesInternal::kArrStackingPolicyName, text, outValue );
    }

    bool AbilitySystemEnumUtil::parseStackExpirationPolicy( string_view text, EffectStackExpirationPolicy& outValue )
    {
        return AbilitySystemTypesInternal::parse( AbilitySystemTypesInternal::kArrStackExpirationPolicyName, text, outValue );
    }

    bool AbilitySystemEnumUtil::parseMagnitudeSource( string_view text, EffectMagnitudeSource& outValue )
    {
        return AbilitySystemTypesInternal::parse( AbilitySystemTypesInternal::kArrMagnitudeSourceName, text, outValue );
    }

    const utf8* AbilitySystemEnumUtil::toString( AttributeModOp value )
    {
        return AbilitySystemTypesInternal::findName( AbilitySystemTypesInternal::kArrModOpName, value );
    }

    const utf8* AbilitySystemEnumUtil::toString( EffectDurationPolicy value )
    {
        return AbilitySystemTypesInternal::findName( AbilitySystemTypesInternal::kArrDurationPolicyName, value );
    }

    const utf8* AbilitySystemEnumUtil::toString( EffectStackingPolicy value )
    {
        return AbilitySystemTypesInternal::findName( AbilitySystemTypesInternal::kArrStackingPolicyName, value );
    }

    const utf8* AbilitySystemEnumUtil::toString( EffectStackExpirationPolicy value )
    {
        return AbilitySystemTypesInternal::findName( AbilitySystemTypesInternal::kArrStackExpirationPolicyName, value );
    }

    const utf8* AbilitySystemEnumUtil::toString( EffectMagnitudeSource value )
    {
        return AbilitySystemTypesInternal::findName( AbilitySystemTypesInternal::kArrMagnitudeSourceName, value );
    }
} // namespace sw
