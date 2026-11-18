// 에셋 종류별 카드 썸네일 그리기입니다(MVVM View). 종류의 등록 · 열기 · 뷰포트 놓기는 `AssetActions/Model/<종류>AssetTypeActions.cpp` 다.
#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorAssetThumbnailRendererInternal
        {
            static constexpr Color kWaveform{ Color::fromBytes( 80, 210, 220, 240 ) };

            static constexpr Color kSphere{ Color::fromBytes( 160, 60, 220, 255 ) };
            static constexpr Color kSphereShade{ Color::fromBytes( 230, 180, 255, 200 ) };
            static constexpr Color kSphereSpecular{ Color::fromBytes( 255, 255, 255, 240 ) };

            static constexpr Color kCubeTop{ Color::fromBytes( 90, 160, 255, 255 ) };
            static constexpr Color kCubeLeft{ Color::fromBytes( 50, 120, 230, 255 ) };
            static constexpr Color kCubeRight{ Color::fromBytes( 35, 95, 195, 255 ) };

            static constexpr Color kGlobe{ Color::fromBytes( 70, 200, 140, 200 ) };
            static constexpr Color kAxisVertical{ Color::fromBytes( 240, 80, 80, 220 ) };
            static constexpr Color kAxisHorizontal{ Color::fromBytes( 80, 160, 240, 220 ) };

            static constexpr Color kCornerFill{ Color::fromBytes( 240, 120, 50, 255 ) };
            static constexpr Color kCornerOutline{ Color::fromBytes( 255, 210, 140, 255 ) };

            static constexpr Color kCheckerDark{ Color::fromBytes( 38, 40, 46, 255 ) };
            static constexpr Color kCheckerLight{ Color::fromBytes( 58, 62, 70, 255 ) };
            static constexpr Color kFrame{ Color::fromBytes( 255, 255, 255, 200 ) };
            static constexpr Color kSun{ Color::fromBytes( 240, 200, 80, 230 ) };
            static constexpr Color kHill{ Color::fromBytes( 70, 180, 120, 230 ) };
        };
        using Internal = EditorAssetThumbnailRendererInternal;
    } // namespace

    /** @brief 오디오 — 이퀄라이저 막대. */
    bool EditorAssetThumbnailRenderer::drawAudio( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        constexpr int32   kBarCount               = 5;
        constexpr float32 arrBarHeight[kBarCount] = { 0.25f, 0.55f, 0.95f, 0.65f, 0.35f };
        constexpr float32 kBarWidth               = 3.0f;
        constexpr float32 kBarGap                 = 3.0f;
        constexpr float32 kTotalWidth             = kBarCount * kBarWidth + ( kBarCount - 1 ) * kBarGap;

        const float32 height  = maxPos._y - minPos._y;
        const float32 centerX = minPos._x + ( maxPos._x - minPos._x ) * 0.5f;
        const float32 centerY = minPos._y + height * 0.5f;
        const float32 startX  = centerX - kTotalWidth * 0.5f;
        for ( int32 barIndex = 0; barIndex < kBarCount; ++barIndex )
        {
            const float32 barX      = startX + static_cast<float32>( barIndex ) * ( kBarWidth + kBarGap );
            const float32 barHeight = height * 0.45f * arrBarHeight[barIndex];
            pDrawList->AddRectFilled( ImVec2( barX, centerY - barHeight * 0.5f ), ImVec2( barX + kBarWidth, centerY + barHeight * 0.5f ), EditorColorUtil::toImU32( Internal::kWaveform ), 1.0f );
        }
        return true;
    }

    /** @brief 머티리얼 — 스페큘러 음영을 넣은 구. */
    bool EditorAssetThumbnailRenderer::drawMaterial( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const float32 width   = maxPos._x - minPos._x;
        const float32 centerX = minPos._x + width * 0.5f;
        const float32 centerY = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
        const float32 radius  = width * 0.26f;
        pDrawList->AddCircleFilled( ImVec2( centerX, centerY ), radius, EditorColorUtil::toImU32( Internal::kSphere ), 24 );
        pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.32f, centerY - radius * 0.32f ), radius * 0.35f, EditorColorUtil::toImU32( Internal::kSphereShade ), 16 );
        pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.38f, centerY - radius * 0.38f ), radius * 0.15f, EditorColorUtil::toImU32( Internal::kSphereSpecular ), 12 );
        return true;
    }

    /** @brief 프리팹 — 등각 큐브. */
    bool EditorAssetThumbnailRenderer::drawPrefab( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const float32 width     = maxPos._x - minPos._x;
        const float32 centerX   = minPos._x + width * 0.5f;
        const float32 centerY   = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
        const float32 size      = width * 0.22f;
        ImVec2        arrTop[4] = { ImVec2( centerX, centerY - size * 1.1f ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ),
                                    ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ) };
        pDrawList->AddConvexPolyFilled( arrTop, 4, EditorColorUtil::toImU32( Internal::kCubeTop ) );

        ImVec2 arrLeft[4] = { ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ), ImVec2( centerX, centerY + size * 0.9f ),
                              ImVec2( centerX - size * 0.9f, centerY + size * 0.35f ) };
        pDrawList->AddConvexPolyFilled( arrLeft, 4, EditorColorUtil::toImU32( Internal::kCubeLeft ) );

        ImVec2 arrRight[4] = { ImVec2( centerX, centerY ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX + size * 0.9f, centerY + size * 0.35f ),
                               ImVec2( centerX, centerY + size * 0.9f ) };
        pDrawList->AddConvexPolyFilled( arrRight, 4, EditorColorUtil::toImU32( Internal::kCubeRight ) );
        return true;
    }

    /** @brief 씬 — 나침반. */
    bool EditorAssetThumbnailRenderer::drawScene( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const float32 width   = maxPos._x - minPos._x;
        const float32 centerX = minPos._x + width * 0.5f;
        const float32 centerY = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
        pDrawList->AddCircle( ImVec2( centerX, centerY ), width * 0.26f, EditorColorUtil::toImU32( Internal::kGlobe ), 18, 1.5f );
        pDrawList->AddLine( ImVec2( centerX, centerY - width * 0.28f ), ImVec2( centerX, centerY + width * 0.28f ), EditorColorUtil::toImU32( Internal::kAxisVertical ), 1.5f );
        pDrawList->AddLine( ImVec2( centerX - width * 0.28f, centerY ), ImVec2( centerX + width * 0.28f, centerY ), EditorColorUtil::toImU32( Internal::kAxisHorizontal ), 1.5f );
        return true;
    }

    /** @brief 셰이더 — 다이아몬드(프리즘). */
    bool EditorAssetThumbnailRenderer::drawShader( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const float32 width        = maxPos._x - minPos._x;
        const float32 centerX      = minPos._x + width * 0.5f;
        const float32 centerY      = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
        const float32 radius       = width * 0.24f;
        ImVec2        arrCorner[4] = { ImVec2( centerX, centerY - radius ), ImVec2( centerX + radius * 0.85f, centerY ), ImVec2( centerX, centerY + radius ), ImVec2( centerX - radius * 0.85f, centerY ) };
        pDrawList->AddConvexPolyFilled( arrCorner, 4, EditorColorUtil::toImU32( Internal::kCornerFill ) );
        pDrawList->AddPolyline( arrCorner, 4, EditorColorUtil::toImU32( Internal::kCornerOutline ), ImDrawFlags_Closed, 1.5f );
        return true;
    }

    bool EditorAssetThumbnailRenderer::drawTexture( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const float32 width   = maxPos._x - minPos._x;
        const float32 height  = maxPos._y - minPos._y;
        const float32 centerX = minPos._x + width * 0.5f;
        const float32 centerY = minPos._y + height * 0.5f;

        // 체크무늬 배경. 격자는 정수 인덱스로 돈다 — 위치는 곱셈 한 번, 셀 색은 인덱스 합의 홀짝이다.
        constexpr float32 kCheckerSize = 6.0f;
        const float32     startX       = minPos._x + 4.0f;
        const float32     startY       = minPos._y + 4.0f;
        const int32       countX       = static_cast<int32>( MathUtil::max( ( maxPos._x - 4.0f - startX ) / kCheckerSize, 0.0f ) ) + 1;
        const int32       countY       = static_cast<int32>( MathUtil::max( ( maxPos._y - 4.0f - startY ) / kCheckerSize, 0.0f ) ) + 1;
        for ( int32 indexY = 0; indexY < countY; ++indexY )
        {
            const float32 y = startY + static_cast<float32>( indexY ) * kCheckerSize;
            if ( y >= maxPos._y - 4.0f )
                break;

            for ( int32 indexX = 0; indexX < countX; ++indexX )
            {
                const float32 x = startX + static_cast<float32>( indexX ) * kCheckerSize;
                if ( x >= maxPos._x - 4.0f )
                    break;

                const bool bDark = ( ( indexX + indexY ) % 2 ) == 0;
                pDrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + kCheckerSize, y + kCheckerSize ),
                                          bDark ? EditorColorUtil::toImU32( Internal::kCheckerDark ) : EditorColorUtil::toImU32( Internal::kCheckerLight ) );
            }
        }
        // 액자 테두리 · 해 · 산
        pDrawList->AddRect( ImVec2( minPos._x + width * 0.16f, minPos._y + height * 0.16f ), ImVec2( minPos._x + width * 0.84f, minPos._y + height * 0.84f ), EditorColorUtil::toImU32( Internal::kFrame ),
                            2.0f );
        pDrawList->AddCircleFilled( ImVec2( centerX + width * 0.15f, centerY - height * 0.12f ), width * 0.08f, EditorColorUtil::toImU32( Internal::kSun ) );
        ImVec2 arrTriangle[3] = { ImVec2( centerX - width * 0.22f, centerY + height * 0.22f ), ImVec2( centerX, centerY - height * 0.05f ), ImVec2( centerX + width * 0.22f, centerY + height * 0.22f ) };
        pDrawList->AddConvexPolyFilled( arrTriangle, 3, EditorColorUtil::toImU32( Internal::kHill ) );
        return true;
    }
} // namespace sw::editor
