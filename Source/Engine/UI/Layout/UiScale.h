/**
 * @file UiScale.h
 * @brief UI 배율 규칙(해상도 → 배율, UMG DPI 곡선 자리)과 뷰포트 계산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/WidgetTypes.h"

/** @brief 안전 영역 흉내(0..0.1) — 각 변을 물리 크기의 이 비율만큼 안쪽으로 민다(언리얼 r.DebugSafeZone.TitleRatio). 시험 · 미리보기용. */
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiDebugSafeZone );

namespace sw
{
    /** @brief 해상도에서 UI 배율을 고르는 규칙입니다. */
    ENUM()
    enum class UiScaleRule : uint8
    {
        ShortestSide, ///< 짧은 변 / 기준 짧은 변(가로 · 세로 화면 모두 같은 크기 — 기본)
        Width,        ///< 너비 / 기준 너비
        Height,       ///< 높이 / 기준 높이
        Fixed         ///< 늘 1(픽셀 그대로)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiScaleSettings
     * @brief 해상도 → UI 배율 규칙입니다(`engine/ui/uiscale.xml`, 게임 프리셋 `_uiScaleSettings` 가 덮어쓴다).
     * @details 레이아웃은 **UI 단위**(기준 해상도의 픽셀)로 하고 화면에 낼 때 배율을 곱합니다. 배율 = clamp( 규칙( 물리 크기 ), 최소, 최대 ) × gv_uiScale
     *          (× 창 콘텐츠 배율 — `_bApplyContentScale` 일 때만). 게임 창은 해상도 규칙이 이미 창 크기를 따르므로 OS 배율까지 곱하면 두 번 곱한다(UMG 기본과 같다).
     */
    REFLECT()
    struct SW_API UiScaleSettings
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Rule" )
        UiScaleRule _rule{ UiScaleRule::ShortestSide };
        PROPERTY( DisplayName = "Reference Width", Min = 1.0 )
        float32 _referenceWidth{ 1920.0f };
        PROPERTY( DisplayName = "Reference Height", Min = 1.0 )
        float32 _referenceHeight{ 1080.0f };
        PROPERTY( DisplayName = "Min Scale", Min = 0.01 )
        float32 _minScale{ 0.5f };
        PROPERTY( DisplayName = "Max Scale", Min = 0.01 )
        float32 _maxScale{ 4.0f };
        PROPERTY( DisplayName = "Apply Content Scale", Tooltip = "Multiply by the OS window scale (desktop-style UI only)" )
        bool _bApplyContentScale{ false };

        /** @brief 리소스 경로의 XML 을 읽습니다. 실패하면(파일 없음 · 모르는 키) 오류를 남기고 false 입니다(읽힌 값은 남는다). */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief 규칙만의 배율입니다(사용자 배율 · 창 배율 전, 최소 · 최대로 묶음). */
        float32 computeResolutionScale( const float2& physicalSize ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiScaleUtil
     * @brief 물리 크기 · 사용자 배율 · 창 배율 · 안전 영역으로 UI 뷰포트를 만듭니다.
     */
    struct SW_API UiScaleUtil
    {
        /** @brief 글자 배율이 줄여도 글이 이 크기(UI 단위) 밑으로 내려가지 않습니다 — Xbox 접근성 지침(XAG 101)의 최소 글 크기. */
        static constexpr float32 kMinScaledFontSize = 12.0f;

        /**
         * @brief 글자 배율 @p textScale(gv_uiTextScale)을 곱한 글 크기입니다.
         * @details 배율이 줄여도 `kMinScaledFontSize` 밑으로는 내려가지 않습니다. 그보다 작게 적은 글(각주 · 아래 첨자)은 적은 크기가 하한입니다 —
         *          배율이 글을 키우기만 하고 디자이너가 고른 크기를 억지로 키우지는 않습니다. 0 이하 배율은 1 입니다.
         */
        static float32 computeScaledFontSize( float32 fontSize, float32 textScale );
        /**
         * @brief 이번 프레임의 UI 뷰포트를 만듭니다.
         * @param physicalSize 그릴 화면의 픽셀 크기.
         * @param userScale 사용자 UI 배율(gv_uiScale).
         * @param contentScale 창의 OS 배율(`IWindow::getContentScale`) — 설정이 켤 때만 곱한다.
         * @param safeZoneRatio 각 변을 물리 크기의 이 비율만큼 안쪽으로(gv_uiDebugSafeZone · 플랫폼 안전 영역).
         */
        static UiViewport makeViewport( const UiScaleSettings& settings, const float2& physicalSize, float32 userScale, float32 contentScale,
                                        float32 safeZoneRatio );
    };
} // namespace sw
