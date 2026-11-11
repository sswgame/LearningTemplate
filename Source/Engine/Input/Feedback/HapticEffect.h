/**
 * @file HapticEffect.h
 * @brief 시간에 따른 진동 데이터(`*.haptic.xml`)입니다.
 * @details 언리얼 `UForceFeedbackEffect`(채널마다 곡선 · 길이) · 유니티 `Gamepad.SetMotorSpeeds` 를 프레임마다 부르는 재생과 같은 자리다.
 *          두 모터(저주파 · 고주파) 곡선을 키 사이 선형 보간으로 읽는다. 재생은 `HapticPlayback`(장치가 든다) 이고 출력은 `IInputFeedbackDevice` 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;
} // namespace sw

namespace sw
{
    /** @brief 진동 곡선의 키 하나입니다(그 시각의 두 모터 세기). */
    REFLECT()
    struct SW_API HapticKeyDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Time from the start", Units = s )
        float32 _time{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Low-frequency (left, strong) motor strength" )
        float32 _low{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "High-frequency (right, weak) motor strength" )
        float32 _high{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 진동 효과 하나입니다. 길이는 마지막 키의 시각입니다.
     * @code
     *     <HapticEffectDef _bLoop="false">
     *         <_listKey>
     *             <HapticKeyDef _time="0" _low="1" _high="0.6" />
     *             <HapticKeyDef _time="0.25" _low="0" _high="0" />
     *         </_listKey>
     *     </HapticEffectDef>
     * @endcode
     */
    REFLECT()
    struct SW_API HapticEffectDef
    {
        REFLECT_BODY();

        static constexpr string_view kExtension = ".haptic.xml";
        /** @brief 진동 파일의 접미사입니다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 진동 에셋 종류입니다(등록은 `HapticEffect.cpp` 의 공유 캐시 줄). 판 0. */
        static constexpr AssetTypeDef kAssetType{ "HapticEffect", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        PROPERTY( Tooltip = "Keys in time order; values between keys are interpolated linearly" )
        vector<HapticKeyDef> _listKey{};
        PROPERTY( Tooltip = "Repeats from the start until stopped" )
        bool _bLoop{ SW_FALSE };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );
        /** @brief 키가 있고, 시각이 0 부터 늘어나며, 세기가 [0, 1] 인지 봅니다. 고리면 길이가 0 보다 커야 합니다. */
        [[nodiscard]] bool validate( string_view sourceName ) const;

        /** @brief 길이(마지막 키의 시각, 초)입니다. */
        float32 getDuration() const;
        /** @brief @p time 초의 두 모터 세기(x 저주파, y 고주파)입니다. 고리면 길이로 감고, 아니면 끝 값을 유지합니다. */
        float2 evaluate( float32 time ) const;

        /** @brief @p durationSeconds 동안 같은 세기인 효과를 만듭니다(`GamepadDevice::playVibration`). */
        static shared_ptr<const HapticEffectDef> makeConstant( float32 lowFrequency, float32 highFrequency, float32 durationSeconds );

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다(`AudioMusicDef::loadComposed` 와 같은 모양). */
        [[nodiscard]] bool loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context );
    };

    /** @brief 이 종류의 공유 캐시입니다 — 인스턴스화는 `HapticEffect.cpp` 의 정의 줄이 한다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, HapticEffectDef );
} // namespace sw
