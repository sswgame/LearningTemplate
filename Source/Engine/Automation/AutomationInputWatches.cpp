/**
 * @file AutomationInputWatches.cpp
 * @brief 입력 관리자의 자동화 감시 값입니다 — `Input.OSDeviceInputBlocked` · `Input.VirtualCursorEnabled` · `Input.MouseX` · `Input.MouseY`.
 * @details 입력 층(티어 10)은 감시 값 레지스트리(티어 10)를 볼 수 없어 레지스트리를 볼 수 있는 가장 가까운 층인 Automation 이 등록합니다. 읽기는 `InputManager` 의 공개 getter 뿐입니다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Input.OSDeviceInputBlocked` 입니다. 무인 자동화가 사람의 마우스와 키보드를 창 메시지 단계에서 버리는 중이면 1 입니다. */
        [[nodiscard]] bool readOSDeviceInputBlocked( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            if ( engine::areEngineServicesBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().isOSDeviceInputBlocked() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Input.VirtualCursorEnabled` 입니다. 패드 가상 커서가 켜져 있으면 1 입니다(명령형 디렉터가 플레이 동안 켠다). */
        [[nodiscard]] bool readVirtualCursorEnabled( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            if ( engine::areEngineServicesBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().getVirtualCursor().isEnabled() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Input.MouseX` · `Input.MouseY` 입니다. 마우스(가상 커서가 옮긴 것 포함)의 창 클라이언트 픽셀 위치입니다. */
        [[nodiscard]] bool readMousePosition( bool bX, float64& outValue )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            const MouseDevice* pMouse = engine::getSubsystem<InputManager>().getMouse();
            if ( pMouse == nullptr )
                return false;
            const int2 position = pMouse->getPosition();
            outValue            = static_cast<float64>( bX ? position._x : position._y );
            return true;
        }

        [[nodiscard]] bool readMouseX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readMousePosition( true, outValue );
        }

        [[nodiscard]] bool readMouseY( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readMousePosition( false, outValue );
        }
    } // namespace

    SW_AUTOMATION_WATCH( inputOSDeviceInputBlocked, "Input.OSDeviceInputBlocked", "1 while unattended automation drops the person's mouse and keyboard window events",
                         &readOSDeviceInputBlocked );
    SW_AUTOMATION_WATCH( inputVirtualCursorEnabled, "Input.VirtualCursorEnabled", "1 while the gamepad virtual cursor is on (command directors turn it on for play)",
                         &readVirtualCursorEnabled );
    SW_AUTOMATION_WATCH( inputMouseX, "Input.MouseX", "Mouse pointer X in window client pixels (the gamepad virtual cursor moves it too)", &readMouseX );
    SW_AUTOMATION_WATCH( inputMouseY, "Input.MouseY", "Mouse pointer Y in window client pixels (the gamepad virtual cursor moves it too)", &readMouseY );
} // namespace sw
