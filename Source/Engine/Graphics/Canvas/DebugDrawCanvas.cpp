#include "pch.h"

#include "Engine/Graphics/Canvas/DebugDrawCanvas.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Text/FontService.h"

namespace sw
{
    namespace
    {
        struct DebugDrawCanvasInternal
        {
            /** @brief 이보다 짧은(픽셀) 선분은 칠하지 않습니다. */
            static constexpr float32 kMinLengthPixels = 0.01f;
            /** @brief 글자 그림자 색입니다(밝은 배경에서도 읽히게 — 에디터 씬 뷰와 같은 값). */
            static constexpr float4 kTextShadow{ 0.0f, 0.0f, 0.0f, 0.78f };

            /** @brief clip 좌표를 w 로 나눠 화면 픽셀로 옮깁니다(왼쪽 위 원점). */
            static float2 toScreen( const float4& clip, const float2& targetSize )
            {
                const float32 invW = 1.0f / clip._w;
                return float2{ ( clip._x * invW * 0.5f + 0.5f ) * targetSize._x, ( 1.0f - ( clip._y * invW * 0.5f + 0.5f ) ) * targetSize._y };
            }

            /** @brief 화면 선분 하나를 두께 있는 사각형으로 칠합니다 — 선 방향을 X 축으로 둔 변환 아래의 사각형이다. */
            static void fillSegment( CanvasPainter& painter, const float2& from, const float2& to, const float4& color )
            {
                const float2  delta  = to - from;
                const float32 length = MathUtil::sqrt( delta._x * delta._x + delta._y * delta._y );
                if ( length < kMinLengthPixels )
                    return;
                const float2    axisX = delta * ( 1.0f / length );
                CanvasTransform transform;
                transform._axisX       = axisX;
                transform._axisY       = float2{ -axisX._y, axisX._x };
                transform._translation = from;
                CanvasBrush brush{};
                brush._color = color;
                painter.pushTransform( transform );
                painter.fillRect( float2{ 0.0f, -DebugDrawCanvasUtil::kLineWidthPixels * 0.5f }, float2{ length, DebugDrawCanvasUtil::kLineWidthPixels }, brush );
                painter.popTransform();
            }
        };
    } // namespace

    /**
     * @brief `-gv_debugDrawGameView=1`: 디버그 드로우 큐(선 · 구 · 상자 · 화살표 · 글자)를 게임 화면(App 창 · 에디터 게임 뷰 · 스크린샷) 위에 겹쳐 그립니다.
     * @details 에디터 게임 뷰 툴바의 단추가 켜고 끈다. 권위 고스트(`-gv_authorityGhostPort`)처럼 큐에 그리는 디버그 도구를 게임 화면에서 본다. Shipping 에는 없다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_debugDrawGameView, false, "디버그 드로우 큐를 게임 화면 위에 겹쳐 그린다 — 선 · 구 · 글자(캔버스)" );
} // namespace sw

namespace sw
{
    bool DebugDrawCanvasUtil::isGameViewOverlayEnabled()
    {
#if defined( SW_SHIPPING )
        return false;
#else
        return gv_debugDrawGameView;
#endif
    }

    void DebugDrawCanvasUtil::setGameViewOverlayEnabled( bool bEnabled )
    {
        gv_debugDrawGameView = bEnabled;
    }

    bool DebugDrawCanvasUtil::projectSegment( const float4x4& viewProj, const float3& from, const float3& to, const float2& targetSize, float2& outFrom, float2& outTo )
    {
        float4 clipA = float4::transform( float4{ from, 1.0f }, viewProj );
        float4 clipB = float4::transform( float4{ to, 1.0f }, viewProj );
        if ( clipA._w <= kNearW && clipB._w <= kNearW )
            return false;
        // 한쪽만 뒤에 있으면 w == kNearW 가 되는 지점까지 당긴다. 동차 좌표는 선형이라 clip 공간에서 바로 보간된다.
        if ( clipA._w <= kNearW )
        {
            const float32 t = ( kNearW - clipA._w ) / ( clipB._w - clipA._w );
            clipA           = clipA + ( clipB - clipA ) * t;
        }
        else if ( clipB._w <= kNearW )
        {
            const float32 t = ( kNearW - clipB._w ) / ( clipA._w - clipB._w );
            clipB           = clipB + ( clipA - clipB ) * t;
        }
        outFrom = DebugDrawCanvasInternal::toScreen( clipA, targetSize );
        outTo   = DebugDrawCanvasInternal::toScreen( clipB, targetSize );
        return true;
    }

    uint32 DebugDrawCanvasUtil::paint( const DebugDrawQueue& queue, const float4x4& viewProj, const float2& targetSize, FontService* pFontService, uint64 frameIndex,
                                       CanvasDrawList& outCanvas )
    {
        // 게임 스레드 하나가 패킷을 채운다 — 재사용 버퍼 하나로 충분하다.
        static vector<DebugLine> s_listLine;
        s_listLine.clear();
        queue.appendVisibleSegments( false, s_listLine ); // 게임 카메라는 3D 로 본다 — 구는 대원 셋
        CanvasPainter painter( outCanvas, 1.0f );
        uint32        paintedCount = 0;
        for ( const DebugLine& line : s_listLine )
        {
            float2 screenFrom;
            float2 screenTo;
            if ( projectSegment( viewProj, line._from, line._to, targetSize, screenFrom, screenTo ) == false )
                continue;
            DebugDrawCanvasInternal::fillSegment( painter, screenFrom, screenTo, line._color );
            ++paintedCount;
        }
        if ( pFontService == nullptr || pFontService->isInitialized() == false )
            return paintedCount;
        for ( const DebugText& text : queue.getVisibleTexts() )
        {
            const float4 clip = float4::transform( float4{ text._position, 1.0f }, viewProj );
            if ( clip._w <= kNearW )
                continue;
            const float2     screen = DebugDrawCanvasInternal::toScreen( clip, targetSize );
            CanvasGlyphStyle style{};
            style._fontSize = kTextFontSize;
            style._color    = DebugDrawCanvasInternal::kTextShadow;
            CanvasTextLineUtil::drawLine( painter, *pFontService, text._text, float2{ screen._x + 1.0f, screen._y + 1.0f }, style, frameIndex );
            style._color = text._color;
            CanvasTextLineUtil::drawLine( painter, *pFontService, text._text, screen, style, frameIndex );
            ++paintedCount;
        }
        return paintedCount;
    }
} // namespace sw
