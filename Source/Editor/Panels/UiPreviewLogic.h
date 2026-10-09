/**
 * @file UiPreviewLogic.h
 * @brief UI 미리보기 패널(`UiPreviewPanel`)의 판단입니다 — 해상도 견본 → 뷰포트, 이미지 맞춤 · 점 옮기기, 위젯 트리 줄. ImGui 를 모른다(EditorTest 가 시험한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    struct UiScaleSettings;

    class WidgetTree;
} // namespace sw

namespace sw::editor
{
    /** @brief 해상도 견본 하나입니다(물리 픽셀). */
    struct UiPreviewResolution
    {
        const utf8* _pName{ "" };
        uint32      _width{ 0 };
        uint32      _height{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 위젯 트리 목록의 줄 하나입니다(문서 순서). */
    struct UiPreviewWidgetRow
    {
        string   _label{}; ///< "타입 #이름"(이름이 없으면 타입만)
        WidgetId _widget{ kInvalidWidgetId };
        uint32   _depth{ 0 }; ///< 루트 = 0
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct UiPreviewLogic
     * @brief 미리보기 패널이 부르는 순수 함수들입니다(유니티 UI Builder · UMG 디자이너의 미리보기 해상도 · 위젯 계층 칸 자리).
     */
    struct UiPreviewLogic
    {
        /** @brief 해상도 견본 수와 @p index 의 견본입니다 — 1280×720 · 1920×1080 · 2560×1440 · 3840×2160 · 21:9 · 세로. */
        static uint32                     getResolutionCount();
        static const UiPreviewResolution& getResolution( uint32 index );
        /**
         * @brief 견본 @p index 를 게임과 같은 배율 규칙(@p settings)으로 UI 뷰포트로 만듭니다 — 사용자 배율 @p userScale 을 곱하고 각 변을 물리 크기의
         *        @p safeZoneRatio 만큼 안쪽으로(안전 영역 보기). 범위 밖 견본은 첫 견본입니다.
         */
        static UiViewport makeViewport( const UiScaleSettings& settings, uint32 index, float32 userScale, float32 safeZoneRatio );
        /** @brief 크기 @p imageSize 의 그림을 @p available 안에 비율을 지켜 맞춘 크기입니다(늘리지 않는다 — 1 배가 상한). */
        static float2 fitImage( const float2& imageSize, const float2& available );
        /** @brief 그린 그림 위의 점 @p imagePoint(그림 왼쪽 위 원점, 그린 크기 @p drawSize)를 UI 단위 점으로 옮깁니다. */
        static float2 mapImageToUi( const float2& imagePoint, const float2& drawSize, const UiViewport& viewport );
        /** @brief UI 단위 사각형 @p rect 를 그린 그림 위 사각형(그림 왼쪽 위 원점)으로 옮깁니다. */
        static UiRect mapUiToImage( const UiRect& rect, const float2& drawSize, const UiViewport& viewport );
        /** @brief 트리의 위젯을 문서 순서로 한 줄씩(깊이 · "타입 #이름") 모읍니다. */
        static void collectWidgetRows( const WidgetTree& tree, vector<UiPreviewWidgetRow>& outListRow );
        /** @brief UI 점 @p point 아래의 맨 위(문서 순서로 마지막) 보이는 위젯입니다. 없으면 무효입니다. */
        static WidgetId findWidgetAt( const WidgetTree& tree, const float2& point );
        /** @brief 미리보기 렌더 텍스처 경로입니다 — 크기마다 다르다(렌더 텍스처 크기는 처음 만들 때 정해진다). */
        static string makeTargetPath( const UiViewport& viewport );
    };
} // namespace sw::editor
