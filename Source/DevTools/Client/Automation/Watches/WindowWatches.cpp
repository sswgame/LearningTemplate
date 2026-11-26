/**
 * @file WindowWatches.cpp
 * @brief 앱 루프 · 주 창의 자동화 감시 값입니다 — `Engine.FramePacingState` · `Window.RenderPausedTicks` · `Window.Minimized`. 읽기는 주 창 서비스(`MainWindowSubsystem`)의 공개 조회뿐이다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        struct WindowWatchesInternal
        {
            /** @brief 감시 값 `Engine.FramePacingState` — 앱 루프가 이번 프레임에 고른 페이싱 상태 번호입니다. 주 창 서비스가 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readFramePacingState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                if ( pWindowHost == nullptr )
                    return false;
                outValue = static_cast<float64>( static_cast<uint32>( pWindowHost->getFramePacer().getState() ) );
                return true;
            }

            /** @brief 감시 값 `Window.RenderPausedTicks` — 표면을 잃거나 앱이 멈춰 프레임을 돌지 않은 메인 루프 틱의 누적입니다. 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowRenderPausedTicks( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
                if ( pWindow == nullptr )
                    return false;
                outValue = static_cast<float64>( pWindow->getRenderPausedTickCount().get() );
                return true;
            }

            /** @brief 감시 값 `Window.Minimized` — 주 창이 최소화돼 있으면 1 입니다(플랫폼 통보 기준). 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowMinimized( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
                if ( pWindow == nullptr )
                    return false;
                outValue = pWindow->isMinimized() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( engineFramePacingState, "Engine.FramePacingState",
                         "App loop pacing this frame: 0 Foreground, 1 Background (gv_backgroundFrameRate), 2 Minimized, 3 Automation, 4 Server, 5 FullSpeed (gv_framePacing), 6 EditorIdle",
                         &WindowWatchesInternal::readFramePacingState );
    SW_AUTOMATION_WATCH( windowRenderPausedTicks, "Window.RenderPausedTicks", "Main loop ticks that ran no frame because the surface was lost or the app was suspended", &WindowWatchesInternal::readWindowRenderPausedTicks );
    SW_AUTOMATION_WATCH( windowMinimized, "Window.Minimized", "1 while the main window is minimized (as the platform reported it)", &WindowWatchesInternal::readWindowMinimized );
} // namespace sw
