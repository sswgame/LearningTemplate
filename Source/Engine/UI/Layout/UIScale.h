/**
 * @file UIScale.h
 * @brief UI 배율 규칙(해상도에서 배율을 구하는 UMG DPI 곡선에 해당)과 뷰포트 계산을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/VectorMath.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

/** @brief 안전 영역 흉내(0..0.1)입니다. 각 변을 물리 크기의 이 비율만큼 안쪽으로 밉니다(언리얼 r.DebugSafeZone.TitleRatio). 테스트와 미리보기용입니다. */
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiDebugSafeZone );

namespace sw
{
    /** @brief 해상도에서 UI 배율을 고르는 규칙입니다. */
    ENUM()
    enum class UIScaleRule : uint8
    {
        ShortestSide,      ///< 짧은 변 / 기준 짧은 변입니다(가로와 세로 화면 모두 같은 크기이며 기본값입니다).
        Width,             ///< 너비 / 기준 너비입니다.
        Height,            ///< 높이 / 기준 높이입니다.
        Fixed,             ///< 항상 1 입니다(픽셀 그대로).
        MatchWidthOrHeight ///< 너비 배율과 높이 배율을 `_matchWidthOrHeight` 로 섞습니다(0 이면 너비, 1 이면 높이, 그 사이는 로그 공간 보간).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIScaleSettings
     * @brief 해상도에서 UI 배율을 구하는 규칙입니다(`engine/ui/uiscale.xml`이며, 팩 설정 `PackEngineSettings::_uiScaleSettings` 가 덮어씁니다).
     * @details 레이아웃은 UI 단위(기준 해상도의 픽셀)로 하고, 화면에 낼 때 배율을 곱한다. 배율 = clamp( 규칙( 물리 크기 ), 최소, 최대 ) × gv_uiScale
     *          (`_bApplyContentScale` 일 때만 × 창 콘텐츠 배율)이다. 게임 창은 해상도 규칙이 이미 창 크기를 따르므로 OS 배율까지 곱하면 두 번 곱하게 된다(UMG 기본과 같다).
     */
    REFLECT()
    struct SW_API UIScaleSettings
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Rule" )
        UIScaleRule _rule{ UIScaleRule::ShortestSide };
        PROPERTY( DisplayName = "Reference Width", Min = 1.0 )
        float32 _referenceWidth{ 1920.0f };
        PROPERTY( DisplayName = "Reference Height", Min = 1.0 )
        float32 _referenceHeight{ 1080.0f };
        PROPERTY( DisplayName = "Match Width Or Height", Min = 0.0, Max = 1.0,
                  Tooltip = "Used by the MatchWidthOrHeight rule. 0 follows the width, 1 follows the height" )
        float32 _matchWidthOrHeight{ 0.5f };
        PROPERTY( DisplayName = "Min Scale", Min = 0.01 )
        float32 _minScale{ 0.5f };
        PROPERTY( DisplayName = "Max Scale", Min = 0.01 )
        float32 _maxScale{ 4.0f };
        PROPERTY( DisplayName = "Apply Content Scale", Tooltip = "Multiply by the OS window scale (desktop-style UI only)" )
        bool _bApplyContentScale{ false };

        /** @brief 리소스 경로의 XML 을 읽습니다. 실패하면(파일이 없거나 모르는 키) 오류를 남기고 false 를 반환하며, 읽은 값은 남습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 규칙만 적용한 배율입니다(사용자 배율과 창 배율을 적용하기 전이며, 최소와 최대로 제한합니다). */
        float32 computeResolutionScale( const float2& physicalSize ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIScaleUtil
     * @brief 물리 크기, 사용자 배율, 창 배율, 안전 영역으로 UI 뷰포트를 만듭니다.
     */
    struct SW_API UIScaleUtil
    {
        /** @brief 글자 배율을 줄여도 글이 이 크기(UI 단위) 밑으로 내려가지 않게 하는 하한입니다. Xbox 접근성 지침(XAG 101)의 최소 글 크기입니다. */
        static constexpr float32 kMinScaledFontSize = 12.0f;

        /**
         * @brief 글자 배율 @p textScale(gv_uiTextScale)을 곱한 글 크기를 반환합니다.
         * @details 배율을 줄여도 `kMinScaledFontSize` 밑으로는 내려가지 않는다. 그보다 작게 적은 글(각주, 아래 첨자)은 적은 크기가 하한이다.
         *          배율은 글을 키우기만 하고, 디자이너가 고른 크기를 억지로 키우지는 않는다. 0 이하 배율은 1 로 본다.
         */
        static float32 computeScaledFontSize( float32 fontSize, float32 textScale );
        /**
         * @brief 이번 프레임의 UI 뷰포트를 만듭니다.
         * @param physicalSize 그릴 화면의 픽셀 크기입니다.
         * @param userScale 사용자 UI 배율입니다(gv_uiScale).
         * @param contentScale 창의 OS 배율입니다(`IWindow::getContentScale`). 설정이 켜져 있을 때만 곱합니다.
         * @param safeZoneRatio 각 변을 물리 크기의 이 비율만큼 안쪽으로 미는 값입니다(gv_uiDebugSafeZone, 플랫폼 안전 영역).
         */
        static UIViewport makeViewport( const UIScaleSettings& settings, const float2& physicalSize, float32 userScale, float32 contentScale,
                                        float32 safeZoneRatio );
    };
} // namespace sw
