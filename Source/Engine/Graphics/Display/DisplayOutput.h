/**
 * @file DisplayOutput.h
 * @brief 화면 출력 장치 한 값(`DisplayOutputDesc` — SDR · HDR10 · scRGB, 종이 흰색 · 최대 휘도)과 능력에 맞춰 고르는 함수입니다(설계: DesignBacklog "HDR 디스플레이 출력").
 * @details 언리얼 `r.HDR.Display.OutputDevice` · `r.HDR.UI.Level`, 유니티 `HDROutputSettings`, Godot 4.5 HDR 출력 설정과 같은 자리다. 스왑체인 포맷 · 색 공간 · 셰이더 인코딩은
 *          모두 이 값 하나에서 정한다(따로 받지 않아 맞지 않는 짝을 만들 수 없다). 능력 밖 요청은 정보 로그 한 줄과 SDR 이다 — 능력 표가 정한 길이지 실패의 대체가 아니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 출력 장치의 종류입니다. */
    ENUM()
    enum class DisplayOutputKind : uint8
    {
        SDR,   ///< sRGB 비선형 8 비트(지금 그림)
        HDR10, ///< BT.2020 원색 + PQ(ST 2084), R10G10B10A2
        ScRGB, ///< 선형 Rec.709 원색, 1.0 = 80 nits, R16G16B16A16_FLOAT
    };

    /** @brief 출력 종류 @p kind 의 능력 비트입니다(`DisplayOutputDesc::resolve` 의 능력 마스크). */
    constexpr uint8 makeDisplayOutputBit( DisplayOutputKind kind )
    {
        return static_cast<uint8>( 1u << static_cast<uint32>( kind ) );
    }

    /**
     * @struct DisplayOutputDesc
     * @brief 화면 출력 장치 한 값입니다. 파일 머리말 참고.
     */
    REFLECT()
    struct SW_API DisplayOutputDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Output device: SDR, HDR10 (PQ) or scRGB" )
        DisplayOutputKind _kind{ DisplayOutputKind::SDR };
        PROPERTY( Min = 80.0, Max = 500.0, Tooltip = "Luminance (nits) of scene white and of the UI in HDR" ) // shortcut: 단위 표(ReflectionUnits.h)에 휘도가 없다 — 휘도 차원을 더할 때 Units = nits 로
        float32 _paperWhiteNits{ 200.0f };
        PROPERTY( Min = 100.0, Max = 10000.0, Tooltip = "Peak luminance (nits) the tone curve maps to in HDR" )
        float32 _maxLuminanceNits{ 1000.0f };

        /**
         * @brief 요청 @p requested 를 능력 마스크 @p supportedMask(`makeDisplayOutputBit` 들의 합 — 백엔드 · 모니터가 받는 종류)에 맞춥니다.
         * @details 받지 않는 종류면 정보 로그(`Display.OutputUnsupported`) 한 줄과 SDR 이다(휘도 값은 그대로 둔다 — 다시 HDR 을 켤 때 쓴다). SDR 은 늘 받는다.
         */
        [[nodiscard]] static DisplayOutputDesc resolve( const DisplayOutputDesc& requested, uint8 supportedMask );
    };
} // namespace sw
