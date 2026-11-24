#include "pch.h"

#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @class ViewportInputOverlayFrame
         * @brief 오버레이를 한 번 그리는 자리(그릴 곳, 상자 위치, 크기 배율, 알파)와 그 자리에 그리는 단계들입니다.
         */
        class ViewportInputOverlayFrame
        {
        public:
            static constexpr float32 kBoxWidth  = 260.0f; ///< 배율 1 의 상자 너비(픽셀)
            static constexpr float32 kBoxHeight = 130.0f;

            /** @brief 설정의 모서리 · 배율 · 불투명도로 상자 자리를 정합니다. */
            ViewportInputOverlayFrame( ImDrawList* pDrawList, const ViewportInputOverlay& overlay, const ImVec2& viewportScreenPos, const ImVec2& viewportSize )
                : _pDrawList{ pDrawList }
                , _boxMin{ computeBoxMin( overlay, viewportScreenPos, viewportSize ) }
                , _scale{ overlay._scale }
                , _alpha{ static_cast<float32>( static_cast<uint8>( 255.0f * overlay._opacity ) ) / 255.0f }
            {
            }

            /** @brief 반투명 배경 · 테두리 · 제목을 그립니다. */
            void drawBackground() const
            {
                const ImVec2 boxMax = ImVec2( _boxMin.x + kBoxWidth * _scale, _boxMin.y + kBoxHeight * _scale );
                _pDrawList->AddRectFilled( _boxMin, boxMax, color( SW_THEME_COLOR( "InputOverlay", "Background" ) ), 8.0f );
                _pDrawList->AddRect( _boxMin, boxMax, color( SW_THEME_COLOR( "InputOverlay", "Border" ) ), 8.0f, 0, 1.5f );
                _pDrawList->AddText( ImVec2( _boxMin.x + 10.0f, _boxMin.y + 6.0f ), color( SW_THEME_COLOR( "InputOverlay", "Title" ) ), "INPUT OVERLAY" );
            }

            /** @brief 양쪽 스틱과 두 트리거를 그립니다. 패드가 없으면 모두 가운데 · 0 입니다. */
            void drawSticksAndTriggers( const ViewportInputOverlayState& state ) const
            {
                const float32 stickRadius = 24.0f * _scale;
                drawStick( at( 40.0f, 55.0f ), stickRadius, state._leftStick, 4.0f );
                drawStick( at( 105.0f, 75.0f ), stickRadius * 0.8f, state._rightStick, 3.5f );

                const ImVec2 ltBarMin = ImVec2( _boxMin.x + 10.0f, at( 0.0f, 100.0f ).y );
                drawTrigger( ltBarMin, at( 70.0f, 110.0f ), state._leftTrigger );
                drawTrigger( at( 80.0f, 100.0f ), at( 140.0f, 110.0f ), state._rightTrigger );
            }

            /** @brief 페이스 버튼 다이아몬드(A · B · X · Y)를 그립니다(`kArrFaceButton`). */
            void drawFaceButtons( const ViewportInputOverlayState& state ) const
            {
                const ImVec2  faceCenter = at( 200.0f, 60.0f );
                const float32 btnDist    = 18.0f * _scale;
                const float32 btnRadius  = 7.0f * _scale;
                for ( const FaceButton& face : kArrFaceButton )
                {
                    const bool   bDown = state.isFaceButtonDown( face._button );
                    const ImVec2 pos   = ImVec2( faceCenter.x + face._offsetX * btnDist, faceCenter.y + face._offsetY * btnDist );
                    _pDrawList->AddCircleFilled( pos, btnRadius, color( EditorThemeUtil::findColor( bDown ? face._downColorKey : face._upColorKey ) ) );
                    _pDrawList->AddText( ImVec2( pos.x - 3.0f, pos.y - 6.0f ), color( SW_THEME_COLOR( "InputOverlay", "ButtonLabel" ) ), face._pLabel );
                }
            }

            /** @brief 지금 눌린 입력 맵 액션을 상자 안에 들어가는 만큼 적습니다. */
            void drawActiveActions( const ViewportInputOverlayState& state ) const
            {
                const float32 boxH    = kBoxHeight * _scale;
                float32       offsetY = 100.0f * _scale;
                for ( const string& text : state._listActiveAction )
                {
                    _pDrawList->AddText( ImVec2( at( 150.0f, 0.0f ).x, _boxMin.y + offsetY ), color( SW_THEME_COLOR( "InputOverlay", "ActiveAction" ) ), text.c_str() );
                    offsetY += 14.0f;
                    if ( offsetY > boxH - 10.0f )
                        break;
                }
            }

        private:
            /** @brief 페이스 버튼 하나의 위치와 색입니다. 다이아몬드의 네 버튼이 이 테이블 한 줄씩입니다. */
            struct FaceButton
            {
                ViewportOverlayFaceButton _button;
                float32                   _offsetX; ///< 다이아몬드 중심에서의 방향(-1 · 0 · 1) — 거리는 `btnDist`
                float32                   _offsetY;
                EditorThemeColorKey       _downColorKey; ///< 눌렸을 때의 테마 색 토큰(`InputOverlay.*`)
                EditorThemeColorKey       _upColorKey;   ///< 뗐을 때의 테마 색 토큰
                const utf8*               _pLabel;
            };

            static constexpr FaceButton kArrFaceButton[] = {
                {ViewportOverlayFaceButton::A,  0.0f,  1.0f, SW_THEME_COLOR_KEY( "InputOverlay", "ButtonADown" ), SW_THEME_COLOR_KEY( "InputOverlay", "ButtonAUp" ), "A"}, // 아래
                {ViewportOverlayFaceButton::B,  1.0f,  0.0f, SW_THEME_COLOR_KEY( "InputOverlay", "ButtonBDown" ), SW_THEME_COLOR_KEY( "InputOverlay", "ButtonBUp" ), "B"}, // 오른쪽
                {ViewportOverlayFaceButton::X, -1.0f,  0.0f, SW_THEME_COLOR_KEY( "InputOverlay", "ButtonXDown" ), SW_THEME_COLOR_KEY( "InputOverlay", "ButtonXUp" ), "X"}, // 왼쪽
                {ViewportOverlayFaceButton::Y,  0.0f, -1.0f, SW_THEME_COLOR_KEY( "InputOverlay", "ButtonYDown" ), SW_THEME_COLOR_KEY( "InputOverlay", "ButtonYUp" ), "Y"}, // 위
            };

            /** @brief 설정의 모서리에 맞춰 상자의 왼쪽 위를 정합니다. */
            static ImVec2 computeBoxMin( const ViewportInputOverlay& overlay, const ImVec2& viewportScreenPos, const ImVec2& viewportSize )
            {
                constexpr float32 kMargin = 15.0f;
                const float32     boxW    = kBoxWidth * overlay._scale;
                const float32     boxH    = kBoxHeight * overlay._scale;
                const float32     left    = viewportScreenPos.x + kMargin;
                const float32     right   = viewportScreenPos.x + viewportSize.x - boxW - kMargin;
                const float32     top     = viewportScreenPos.y + kMargin;
                const float32     bottom  = viewportScreenPos.y + viewportSize.y - boxH - kMargin;
                switch ( overlay._position )
                {
                    case ViewportOverlayPosition::BottomRight:
                        return ImVec2( right, bottom );
                    case ViewportOverlayPosition::BottomLeft:
                        return ImVec2( left, bottom );
                    case ViewportOverlayPosition::TopRight:
                        return ImVec2( right, top );
                    case ViewportOverlayPosition::TopLeft:
                        return ImVec2( left, top );
                }
                return ImVec2( left, top );
            }

            ImVec2 at( const float32 x, const float32 y ) const { return ImVec2( _boxMin.x + x * _scale, _boxMin.y + y * _scale ); }
            /** @brief 오버레이 불투명도를 알파로 씌운 ImGui 색입니다. */
            uint32 color( const Color& base ) const { return EditorColorUtil::toImU32( base.withAlpha( _alpha ) ); }

            /** @brief 스틱 하나(바탕 원 · 테 · 기울기 점)를 그립니다. */
            void drawStick( const ImVec2& center, const float32 radius, const float2& stick, const float32 dotRadius ) const
            {
                _pDrawList->AddCircleFilled( center, radius, color( SW_THEME_COLOR( "InputOverlay", "StickBase" ) ) );
                _pDrawList->AddCircle( center, radius, color( SW_THEME_COLOR( "InputOverlay", "StickRing" ) ), 16, 1.0f );
                const ImVec2 dot = ImVec2( center.x + stick._x * radius, center.y - stick._y * radius );
                _pDrawList->AddCircleFilled( dot, dotRadius, color( SW_THEME_COLOR( "InputOverlay", "StickDot" ) ) );
            }

            /** @brief 트리거 막대 하나(바탕 · 눌린 만큼 채움)를 그립니다. */
            void drawTrigger( const ImVec2& barMin, const ImVec2& barMax, const float32 value ) const
            {
                _pDrawList->AddRectFilled( barMin, barMax, color( SW_THEME_COLOR( "InputOverlay", "TriggerTrack" ) ) );
                _pDrawList->AddRectFilled( barMin, ImVec2( barMin.x + ( barMax.x - barMin.x ) * value, barMax.y ), color( SW_THEME_COLOR( "InputOverlay", "TriggerFill" ) ) );
            }

        private:
            ImDrawList* _pDrawList;
            ImVec2      _boxMin;
            float32     _scale;
            float32     _alpha;
        };
    } // namespace

    void ViewportInputOverlay::draw( ImDrawList* pDrawList, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const ViewportInputOverlayState& state ) const
    {
        const bool bOverlayHidden = pDrawList == nullptr || _bEnabled == SW_FALSE;
        if ( bOverlayHidden )
            return;

        const ViewportInputOverlayFrame frame{ pDrawList, *this, viewportScreenPos, viewportSize };
        frame.drawBackground();
        if ( _bShowStick == SW_TRUE )
            frame.drawSticksAndTriggers( state );
        if ( _bShowButtons == SW_TRUE )
            frame.drawFaceButtons( state );
        if ( _bShowCommandHistory == SW_TRUE )
            frame.drawActiveActions( state );
    }
} // namespace sw::editor
