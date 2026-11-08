/**
 * @file BodyChannel.h
 * @brief 몸 채널 층입니다. 장비 · 캐릭터 프리셋 · 꾸미기 슬라이더가 이름 붙은 몸 채널(`Bust.Compression`, `Shoulder.Width`, `Heel.Height`, `Hair.Length`)에 값을 얹고, 채널마다 우선순위와 섞기로 하나로 모읍니다.
 * @details 채널 이름은 데이터다(C++ 표가 없다). 모은 값은 해석 결과의 체형 축 값(`ResolvedBoneProportion`, 주인이 빈 것)이 되고, 몸 부품의 체형 축 데이터(`*.bodyshape.xml`)가
 *          스켈레톤마다 채널을 본(이동 · 회전 · 스케일) · 모프로 옮긴다 — 아이템은 뼈 이름을 모른다. 다른 스켈레톤(사람 · 작은 몸 · 짐승)은 자기 매핑을 가지고 아이템 데이터는 그대로다.
 *          언리얼의 커브 → 포즈 드라이버 / 모프 매핑, 검은사막 · inZOI 의 "옷이 몸을 누르는" 보정과 같은 자리다. 채널의 중립은 0 이다(체형 축의 기본 체형).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 채널 층이 아래 값과 섞이는 방법입니다. */
    ENUM()
    enum class BodyChannelBlend : uint8
    {
        Override = 0, ///< 아래 값을 바꾼다(꽉 끼는 갑옷이 가슴 조임을 정한다)
        Additive,     ///< 아래 값에 더한다(어깨 패드가 어깨 폭을 늘린다)
        Max           ///< 아래 값과 큰 쪽(여러 신발 중 가장 높은 굽)
    };

    /** @brief 채널 값 하나입니다(모은 결과). */
    struct BodyChannelValue
    {
        hashed_string _channel{};
        float32       _value{ 0.0f };
    };

    /**
     * @brief 채널 층 하나입니다(`<BodyChannel name value priority blend/>`). 아이템 외형 · 몸 프로필이 가진다.
     * @details 우선순위가 낮은 층부터 차례로 얹는다. 같은 우선순위는 적힌 순서(몸 프로필, 슬롯 표 순서의 장비)다 — 결과가 결정적이다.
     */
    REFLECT()
    struct SW_GF_API BodyChannelLayerDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        float32 _value{ 0.0f };
        PROPERTY()
        int32 _priority{ 0 };
        PROPERTY()
        BodyChannelBlend _blend{ BodyChannelBlend::Override };

        /** @brief 값 @p below 위에 이 층을 얹은 값입니다. */
        float32 applyTo( float32 below ) const;
        /**
         * @brief @p listLayer 를 우선순위 오름차순(같으면 적힌 순서)으로 @p inoutListValue 에 얹습니다. 값이 없는 채널은 0(중립) 위에 얹어 새로 만든다.
         * @details @p inoutListValue 의 처음 값은 꾸미기 슬라이더(체형 축 드라이브)다 — 장비 층이 슬라이더 위에 놓인다.
         */
        static void blendLayers( span<const BodyChannelLayerDef> listLayer, vector<BodyChannelValue>& inoutListValue );
    };
} // namespace sw
