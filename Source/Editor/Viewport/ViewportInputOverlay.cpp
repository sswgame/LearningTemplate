#include "pch.h"

#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Editor/Common/GUI/EditorColorUtil.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        ViewportInputOverlayConfig s_overlayConfig{};

        struct ViewportInputOverlayInternal
        {
            static constexpr Color kBackground{ Color::fromBytes( 15, 18, 25 ) };
            static constexpr Color kBorder{ Color::fromBytes( 50, 160, 240 ) };
            static constexpr Color kTitle{ Color::fromBytes( 180, 220, 255 ) };
            static constexpr Color kStickBase{ Color::fromBytes( 30, 35, 45 ) };
            static constexpr Color kStickRing{ Color::fromBytes( 80, 120, 180 ) };
            static constexpr Color kStickDot{ Color::fromBytes( 50, 255, 120 ) };
            static constexpr Color kTriggerTrack{ Color::fromBytes( 40, 45, 55 ) };
            static constexpr Color kTriggerFill{ Color::fromBytes( 240, 180, 40 ) };
            static constexpr Color kButtonADown{ Color::fromBytes( 50, 255, 100 ) };
            static constexpr Color kButtonAUp{ Color::fromBytes( 30, 80, 40 ) };
            static constexpr Color kButtonBDown{ Color::fromBytes( 255, 60, 60 ) };
            static constexpr Color kButtonBUp{ Color::fromBytes( 90, 30, 30 ) };
            static constexpr Color kButtonXDown{ Color::fromBytes( 60, 140, 255 ) };
            static constexpr Color kButtonXUp{ Color::fromBytes( 30, 40, 90 ) };
            static constexpr Color kButtonYDown{ Color::fromBytes( 255, 230, 50 ) };
            static constexpr Color kButtonYUp{ Color::fromBytes( 90, 80, 20 ) };
            static constexpr Color kButtonLabel{ Color::fromBytes( 255, 255, 255 ) };
            static constexpr Color kActiveAction{ Color::fromBytes( 255, 220, 100 ) };

            /** @brief 오버레이 불투명도를 알파로 씌운 ImGui 색입니다. */
            static uint32 fade( const Color& color, const float32 alpha )
            {
                return EditorColorUtil::toImU32( color.withAlpha( alpha ) );
            }

            /** @brief 한 번 그리는 동안 단계 함수들이 함께 보는 값입니다(그릴 곳, 상자 위치, 크기 배율, 알파). */
            struct DrawFrame
            {
                ImDrawList* _pDrawList{ nullptr };
                ImVec2      _boxMin{};
                float32     _scale{ 1.0f };
                float32     _alpha{ 1.0f };

                ImVec2 at( const float32 x, const float32 y ) const { return ImVec2( _boxMin.x + x * _scale, _boxMin.y + y * _scale ); }
                uint32 color( const Color& base ) const { return fade( base, _alpha ); }
            };

            /** @brief 페이스 버튼 하나의 자리와 색입니다. 다이아몬드의 네 버튼이 이 표 한 줄씩입니다. */
            struct FaceButton
            {
                GamepadButton _button;
                float32       _offsetX; ///< 다이아몬드 중심에서의 방향(-1 · 0 · 1) — 거리는 `btnDist`
                float32       _offsetY;
                Color         _downColor;
                Color         _upColor;
                const utf8*   _pLabel;
            };

            static constexpr FaceButton kArrFaceButton[] = {
                {GamepadButton::A,  0.0f,  1.0f, kButtonADown, kButtonAUp, "A"}, // 아래
                {GamepadButton::B,  1.0f,  0.0f, kButtonBDown, kButtonBUp, "B"}, // 오른쪽
                {GamepadButton::X, -1.0f,  0.0f, kButtonXDown, kButtonXUp, "X"}, // 왼쪽
                {GamepadButton::Y,  0.0f, -1.0f, kButtonYDown, kButtonYUp, "Y"}, // 위
            };

            /** @brief 설정의 모서리에 맞춰 상자의 왼쪽 위를 정합니다. */
            static ImVec2 computeBoxMin( const ViewportInputOverlayConfig& config, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const float32 boxW,
                                         const float32 boxH )
            {
                constexpr float32 kMargin = 15.0f;
                const float32     left    = viewportScreenPos.x + kMargin;
                const float32     right   = viewportScreenPos.x + viewportSize.x - boxW - kMargin;
                const float32     top     = viewportScreenPos.y + kMargin;
                const float32     bottom  = viewportScreenPos.y + viewportSize.y - boxH - kMargin;
                switch ( config._position )
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

            /** @brief 스틱 하나(바탕 원 · 테 · 기울기 점)를 그립니다. */
            static void drawStick( const DrawFrame& frame, const ImVec2& center, const float32 radius, const float2& stick, const float32 dotRadius )
            {
                frame._pDrawList->AddCircleFilled( center, radius, frame.color( kStickBase ) );
                frame._pDrawList->AddCircle( center, radius, frame.color( kStickRing ), 16, 1.0f );
                const ImVec2 dot = ImVec2( center.x + stick._x * radius, center.y - stick._y * radius );
                frame._pDrawList->AddCircleFilled( dot, dotRadius, frame.color( kStickDot ) );
            }

            /** @brief 트리거 막대 하나(바탕 · 눌린 만큼 채움)를 그립니다. */
            static void drawTrigger( const DrawFrame& frame, const ImVec2& barMin, const ImVec2& barMax, const float32 value )
            {
                frame._pDrawList->AddRectFilled( barMin, barMax, frame.color( kTriggerTrack ) );
                frame._pDrawList->AddRectFilled( barMin, ImVec2( barMin.x + ( barMax.x - barMin.x ) * value, barMax.y ), frame.color( kTriggerFill ) );
            }

            /** @brief 양쪽 스틱과 두 트리거를 그립니다. 패드가 없으면 모두 가운데 · 0 입니다. */
            static void drawSticksAndTriggers( const DrawFrame& frame, GamepadDevice* pGamepad )
            {
                const bool    bConnected  = pGamepad != nullptr && pGamepad->isConnected();
                const float2  leftStick   = bConnected ? pGamepad->getLeftStick() : float2{};
                const float2  rightStick  = bConnected ? pGamepad->getRightStick() : float2{};
                const float32 stickRadius = 24.0f * frame._scale;
                drawStick( frame, frame.at( 40.0f, 55.0f ), stickRadius, leftStick, 4.0f );
                drawStick( frame, frame.at( 105.0f, 75.0f ), stickRadius * 0.8f, rightStick, 3.5f );

                const ImVec2 ltBarMin = ImVec2( frame._boxMin.x + 10.0f, frame.at( 0.0f, 100.0f ).y );
                drawTrigger( frame, ltBarMin, frame.at( 70.0f, 110.0f ), bConnected ? pGamepad->getLeftTrigger() : 0.0f );
                drawTrigger( frame, frame.at( 80.0f, 100.0f ), frame.at( 140.0f, 110.0f ), bConnected ? pGamepad->getRightTrigger() : 0.0f );
            }

            /** @brief 페이스 버튼 다이아몬드(A · B · X · Y)를 그립니다(`kArrFaceButton`). */
            static void drawFaceButtons( const DrawFrame& frame, GamepadDevice* pGamepad )
            {
                const ImVec2  faceCenter = frame.at( 200.0f, 60.0f );
                const float32 btnDist    = 18.0f * frame._scale;
                const float32 btnRadius  = 7.0f * frame._scale;
                for ( const FaceButton& face : kArrFaceButton )
                {
                    const bool   bDown = pGamepad != nullptr && pGamepad->isButtonDown( face._button );
                    const ImVec2 pos   = ImVec2( faceCenter.x + face._offsetX * btnDist, faceCenter.y + face._offsetY * btnDist );
                    frame._pDrawList->AddCircleFilled( pos, btnRadius, frame.color( bDown ? face._downColor : face._upColor ) );
                    frame._pDrawList->AddText( ImVec2( pos.x - 3.0f, pos.y - 6.0f ), frame.color( kButtonLabel ), face._pLabel );
                }
            }

            /** @brief 지금 눌린 입력 맵 액션을 상자 안에 들어가는 만큼 적습니다. */
            static void drawActiveActions( const DrawFrame& frame, const InputMap& inputMap, const float32 boxH )
            {
                float32 offsetY = 100.0f * frame._scale;
                for ( const hashed_string& act : inputMap.getActionNames() )
                {
                    if ( inputMap.isActionDown( act ) == false )
                        continue;
                    const string text = inputMap.getGlyphForAction( sw::hashed_string( act.view() ) ) + " " + string( act.c_str() );
                    frame._pDrawList->AddText( ImVec2( frame.at( 150.0f, 0.0f ).x, frame._boxMin.y + offsetY ), frame.color( kActiveAction ), text.c_str() );
                    offsetY += 14.0f;
                    if ( offsetY > boxH - 10.0f )
                        break;
                }
            }
        };
    } // namespace

    ViewportInputOverlayConfig& ViewportInputOverlay::getConfig()
    {
        return s_overlayConfig;
    }

    void ViewportInputOverlay::draw( ImDrawList* pDrawList, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const InputManager* pInput, const InputMap* pInputMap )
    {
        draw( pDrawList, viewportScreenPos, viewportSize, pInput, pInputMap, s_overlayConfig );
    }

    void ViewportInputOverlay::draw( ImDrawList* pDrawList, const ImVec2& viewportScreenPos, const ImVec2& viewportSize, const InputManager* pInput, const InputMap* pInputMap, const ViewportInputOverlayConfig& config )
    {
        const bool bOverlayHidden = pDrawList == nullptr || config._bEnabled == SW_FALSE || pInput == nullptr;
        if ( bOverlayHidden )
            return;

        using Internal          = ViewportInputOverlayInternal;
        const float32 boxW      = 260.0f * config._scale;
        const float32 boxH      = 130.0f * config._scale;
        const uint8   alphaByte = static_cast<uint8>( 255.0f * config._opacity );

        Internal::DrawFrame frame;
        frame._pDrawList    = pDrawList;
        frame._boxMin       = Internal::computeBoxMin( config, viewportScreenPos, viewportSize, boxW, boxH );
        frame._scale        = config._scale;
        frame._alpha        = static_cast<float32>( alphaByte ) / 255.0f;
        const ImVec2 boxMax = ImVec2( frame._boxMin.x + boxW, frame._boxMin.y + boxH );

        // 반투명 배경 · 테두리 · 제목
        pDrawList->AddRectFilled( frame._boxMin, boxMax, frame.color( Internal::kBackground ), 8.0f );
        pDrawList->AddRect( frame._boxMin, boxMax, frame.color( Internal::kBorder ), 8.0f, 0, 1.5f );
        pDrawList->AddText( ImVec2( frame._boxMin.x + 10.0f, frame._boxMin.y + 6.0f ), frame.color( Internal::kTitle ), "INPUT OVERLAY" );

        GamepadDevice* pGamepad = pInput->getGamepad( 0 );
        if ( config._bShowStick == SW_TRUE )
            Internal::drawSticksAndTriggers( frame, pGamepad );
        if ( config._bShowButtons == SW_TRUE )
            Internal::drawFaceButtons( frame, pGamepad );
        if ( pInputMap != nullptr && config._bShowCommandHistory == SW_TRUE )
            Internal::drawActiveActions( frame, *pInputMap, boxH );
    }
} // namespace sw::editor
