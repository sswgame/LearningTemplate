#include "pch.h"

#include "Editor/Common/Widgets/ViewportInputOverlay.h"

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
        if ( pDrawList == nullptr || config._bEnabled == SW_FALSE || pInput == nullptr )
            return;

        const float32 boxW      = 260.0f * config._scale;
        const float32 boxH      = 130.0f * config._scale;
        const float32 margin    = 15.0f;
        const uint8   alphaByte = static_cast<uint8>( 255.0f * config._opacity );
        const float32 alpha     = static_cast<float32>( alphaByte ) / 255.0f;

        ImVec2 boxMin{ 0.0f, 0.0f };
        switch ( config._position )
        {
            case ViewportOverlayPosition::BottomRight:
            {
                boxMin = ImVec2( viewportScreenPos.x + viewportSize.x - boxW - margin, viewportScreenPos.y + viewportSize.y - boxH - margin );
                break;
            }
            case ViewportOverlayPosition::BottomLeft:
            {
                boxMin = ImVec2( viewportScreenPos.x + margin, viewportScreenPos.y + viewportSize.y - boxH - margin );
                break;
            }
            case ViewportOverlayPosition::TopRight:
            {
                boxMin = ImVec2( viewportScreenPos.x + viewportSize.x - boxW - margin, viewportScreenPos.y + margin );
                break;
            }
            case ViewportOverlayPosition::TopLeft:
            {
                boxMin = ImVec2( viewportScreenPos.x + margin, viewportScreenPos.y + margin );
                break;
            }
        }
        const ImVec2 boxMax = ImVec2( boxMin.x + boxW, boxMin.y + boxH );

        // 1) 반투명 배경 및 테두리
        pDrawList->AddRectFilled( boxMin, boxMax, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kBackground, alpha ), 8.0f );
        pDrawList->AddRect( boxMin, boxMax, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kBorder, alpha ), 8.0f, 0, 1.5f );

        // 2) 타이틀 텍스트
        const ImVec2 titlePos = ImVec2( boxMin.x + 10.0f, boxMin.y + 6.0f );
        pDrawList->AddText( titlePos, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kTitle, alpha ), "INPUT OVERLAY" );

        GamepadDevice* pGamepad = pInput->getGamepad( 0 );
        float32        lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f, lt = 0.0f, rt = 0.0f;
        if ( pGamepad != nullptr && pGamepad->isConnected() )
        {
            const float2 leftStick  = pGamepad->getLeftStick();
            const float2 rightStick = pGamepad->getRightStick();
            lx                      = leftStick._x;
            ly                      = leftStick._y;
            rx                      = rightStick._x;
            ry                      = rightStick._y;
            lt                      = pGamepad->getLeftTrigger();
            rt                      = pGamepad->getRightTrigger();
        }

        // 3) 좌측/우측 스틱 2D 캔버스
        if ( config._bShowStick == SW_TRUE )
        {
            const float32 stickRadius = 24.0f * config._scale;
            const ImVec2  centerL     = ImVec2( boxMin.x + 40.0f * config._scale, boxMin.y + 55.0f * config._scale );
            const ImVec2  centerR     = ImVec2( boxMin.x + 105.0f * config._scale, boxMin.y + 75.0f * config._scale );

            // LS
            pDrawList->AddCircleFilled( centerL, stickRadius, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickBase, alpha ) );
            pDrawList->AddCircle( centerL, stickRadius, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickRing, alpha ), 16, 1.0f );
            const ImVec2 dotL = ImVec2( centerL.x + lx * stickRadius, centerL.y - ly * stickRadius );
            pDrawList->AddCircleFilled( dotL, 4.0f, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickDot, alpha ) );

            // RS
            pDrawList->AddCircleFilled( centerR, stickRadius * 0.8f, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickBase, alpha ) );
            pDrawList->AddCircle( centerR, stickRadius * 0.8f, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickRing, alpha ), 16, 1.0f );
            const ImVec2 dotR = ImVec2( centerR.x + rx * stickRadius * 0.8f, centerR.y - ry * stickRadius * 0.8f );
            pDrawList->AddCircleFilled( dotR, 3.5f, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kStickDot, alpha ) );

            // LT / RT Bar
            const ImVec2 ltBarMin = ImVec2( boxMin.x + 10.0f, boxMin.y + 100.0f * config._scale );
            const ImVec2 ltBarMax = ImVec2( boxMin.x + 70.0f * config._scale, boxMin.y + 110.0f * config._scale );
            pDrawList->AddRectFilled( ltBarMin, ltBarMax, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kTriggerTrack, alpha ) );
            pDrawList->AddRectFilled( ltBarMin, ImVec2( ltBarMin.x + ( ltBarMax.x - ltBarMin.x ) * lt, ltBarMax.y ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kTriggerFill, alpha ) );

            const ImVec2 rtBarMin = ImVec2( boxMin.x + 80.0f * config._scale, boxMin.y + 100.0f * config._scale );
            const ImVec2 rtBarMax = ImVec2( boxMin.x + 140.0f * config._scale, boxMin.y + 110.0f * config._scale );
            pDrawList->AddRectFilled( rtBarMin, rtBarMax, ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kTriggerTrack, alpha ) );
            pDrawList->AddRectFilled( rtBarMin, ImVec2( rtBarMin.x + ( rtBarMax.x - rtBarMin.x ) * rt, rtBarMax.y ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kTriggerFill, alpha ) );
        }

        // 4) 페이스 버튼 다이아몬드 (A, B, X, Y)
        if ( config._bShowButtons == SW_TRUE )
        {
            const ImVec2  faceCenter = ImVec2( boxMin.x + 200.0f * config._scale, boxMin.y + 60.0f * config._scale );
            const float32 btnDist    = 18.0f * config._scale;
            const float32 btnRadius  = 7.0f * config._scale;

            const bool bA = pGamepad != nullptr && pGamepad->isButtonDown( GamepadButton::A );
            const bool bB = pGamepad != nullptr && pGamepad->isButtonDown( GamepadButton::B );
            const bool bX = pGamepad != nullptr && pGamepad->isButtonDown( GamepadButton::X );
            const bool bY = pGamepad != nullptr && pGamepad->isButtonDown( GamepadButton::Y );

            // A (Bottom)
            const ImVec2 posA = ImVec2( faceCenter.x, faceCenter.y + btnDist );
            pDrawList->AddCircleFilled( posA, btnRadius, bA ? ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonADown, alpha ) : ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonAUp, alpha ) );
            pDrawList->AddText( ImVec2( posA.x - 3.0f, posA.y - 6.0f ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonLabel, alpha ), "A" );

            // B (Right)
            const ImVec2 posB = ImVec2( faceCenter.x + btnDist, faceCenter.y );
            pDrawList->AddCircleFilled( posB, btnRadius, bB ? ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonBDown, alpha ) : ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonBUp, alpha ) );
            pDrawList->AddText( ImVec2( posB.x - 3.0f, posB.y - 6.0f ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonLabel, alpha ), "B" );

            // X (Left)
            const ImVec2 posX = ImVec2( faceCenter.x - btnDist, faceCenter.y );
            pDrawList->AddCircleFilled( posX, btnRadius, bX ? ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonXDown, alpha ) : ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonXUp, alpha ) );
            pDrawList->AddText( ImVec2( posX.x - 3.0f, posX.y - 6.0f ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonLabel, alpha ), "X" );

            // Y (Top)
            const ImVec2 posY = ImVec2( faceCenter.x, faceCenter.y - btnDist );
            pDrawList->AddCircleFilled( posY, btnRadius, bY ? ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonYDown, alpha ) : ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonYUp, alpha ) );
            pDrawList->AddText( ImVec2( posY.x - 3.0f, posY.y - 6.0f ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kButtonLabel, alpha ), "Y" );
        }

        // 5) 활성 액션 표시
        if ( pInputMap != nullptr && config._bShowCommandHistory == SW_TRUE )
        {
            const vector<hashed_string>& listAction = pInputMap->getActionNames();
            float32                      offsetY    = 100.0f * config._scale;
            for ( const hashed_string& act : listAction )
            {
                if ( pInputMap->isActionDown( act ) )
                {
                    const string text = pInputMap->getGlyphForAction( sw::hashed_string( act.view() ) ) + " " + string( act.c_str() );
                    pDrawList->AddText( ImVec2( boxMin.x + 150.0f * config._scale, boxMin.y + offsetY ), ViewportInputOverlayInternal::fade( ViewportInputOverlayInternal::kActiveAction, alpha ), text.c_str() );
                    offsetY += 14.0f;
                    if ( offsetY > boxH - 10.0f )
                        break;
                }
            }
        }
    }
} // namespace sw::editor
