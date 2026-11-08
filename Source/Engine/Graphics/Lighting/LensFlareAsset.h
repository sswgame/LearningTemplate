/**
 * @file LensFlareAsset.h
 * @brief 빛 하나의 렌즈 플레어(`*.lensflare.xml`)입니다 — 빛에서 화면 가운데를 지나는 축 위에 놓는 스프라이트 원소 목록과 가림 · 가장자리 감쇠 값.
 * @details 빛 컴포넌트가 경로(`LightComponent::_lensFlarePath`)로 가리키고, 렌더러의 렌즈 플레어 패스(`RenderPassType::LensFlare`, lens_flare.hlsl)가 그린다.
 *          블룸에서 고스트를 뽑는 화면 효과와 달리 빛마다 데이터로 정한 스프라이트다(언리얼 Lens Flare 의 이미지 기반 고스트와 다른, 유니티 SRP Lens Flare(Data Driven) 자리 — 기억에 의존한 비교).
 *          모양은 셰이더가 깎는 절차적 모양 넷이다(텍스처 없음). shortcut: 텍스처 스프라이트가 필요해지면 원소에 텍스처 경로 칸을 더하고 셰이더가 머티리얼 텍스처로 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 플레어 원소의 모양입니다. 셰이더(lens_flare.hlsl)의 `kLensFlareShape*` 와 같은 번호입니다. */
    ENUM()
    enum class LensFlareShape : uint8
    {
        Glow,   ///< 가운데가 밝고 바깥으로 지수로 줄어드는 빛무리
        Disc,   ///< 가장자리가 부드러운 원판(고스트)
        Ring,   ///< 고리(헤일로)
        Streak, ///< 가로로 긴 줄(아나모픽 줄무늬)
    };

    /** @brief 플레어 원소 하나 — 축 위의 자리, 크기, 색, 모양입니다. */
    REFLECT()
    struct SW_API LensFlareElement
    {
        REFLECT_BODY();

        PROPERTY( Required )
        LensFlareShape _shape{ LensFlareShape::Glow };
        /** @brief 빛(0)에서 화면 가운데(1)를 지나는 축 위의 자리입니다. 1 보다 크면 가운데 너머 반대쪽이다(고스트). */
        PROPERTY( Required )
        float32 _axisPosition{ 0.0f };
        /** @brief 반지름(화면 높이에 대한 비율)입니다. */
        PROPERTY( Required, Min = 0 )
        float32 _size{ 0.0f };
        /** @brief 색 × 세기(HDR, 선형)입니다. 빛의 색 × 세기에 곱한다. */
        PROPERTY( Required )
        float3 _color{};
    };

    /** @brief 렌즈 플레어 파일 하나입니다. 파일 머리말을 참고하세요. */
    REFLECT()
    struct SW_API LensFlareAsset
    {
        REFLECT_BODY();

        /** @brief 원소 목록입니다(비면 읽기 오류). */
        PROPERTY()
        vector<LensFlareElement> _listElement;
        /** @brief 화면 가장자리에서 줄어드는 폭(NDC 의 비율, 0 이면 가장자리에서 바로 꺼진다)입니다. */
        PROPERTY( Required, Min = 0, Max = 1 )
        float32 _edgeFade{ 0.0f };
        /** @brief 가림을 볼 깊이 표본 반지름(화면 높이에 대한 비율)입니다 — 빛 자리 둘레 표본 중 가리지 않은 몫이 보이는 정도다. */
        PROPERTY( Required, Min = 0 )
        float32 _occlusionRadius{ 0.0f };

        /** @brief 리소스 경로에서 읽습니다. 못 읽거나 원소가 없으면 false 이고 @p pOutError 에 까닭을 씁니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, string* pOutError = nullptr );
    };
} // namespace sw
