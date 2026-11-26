/**
 * @file InputWatches.cpp
 * @brief 입력 관리자의 자동화 감시 값입니다 — `Input.OSDeviceInputBlocked` · `Input.VirtualCursorEnabled` · `Input.MouseX` · `Input.MouseY` · `Input.RawMouseEvents` · `Input.MouseLockActive` · `Input.WindowFocused`.
 * @details 입력 폴더(층 3)는 감시 값 레지스트리(`Automation/Watch`, 층 5)보다 아래라 이 파일만 층 표에 파일 키(층 5)로 올려 주인 곁에서 등록합니다. 읽기는 `InputManager` 의 공개 getter 뿐입니다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineSubsystems.h"
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
            if ( engine::areEngineSubsystemsBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().isOSDeviceInputBlocked() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Input.VirtualCursorEnabled` 입니다. 패드 가상 커서가 켜져 있으면 1 입니다(명령형 디렉터가 플레이 동안 켠다). */
        [[nodiscard]] bool readVirtualCursorEnabled( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            if ( engine::areEngineSubsystemsBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().getVirtualCursor().isEnabled() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Input.MouseX` · `Input.MouseY` 입니다. 마우스(가상 커서가 옮긴 것 포함)의 창 클라이언트 픽셀 위치입니다. */
        [[nodiscard]] bool readMousePosition( bool bX, float64& outValue )
        {
            if ( engine::areEngineSubsystemsBound() == false )
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

        /** @brief 감시 값 `Input.RawMouseEvents` 입니다. OS 에서 받은 원시 마우스 이동 보고 수입니다(Win32 WM_INPUT — 커서 잠금 중에만 등록한다). 늘기만 한다. */
        [[nodiscard]] bool readRawMouseEvents( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineSubsystemsBound() == false )
                return false;
            outValue = static_cast<float64>( engine::getSubsystem<InputManager>().getRawMouseEventCount().get() );
            return true;
        }

        /** @brief 감시 값 `Input.MouseLockActive` 입니다. 엔진이 지금 OS 에 마우스 잠금을 걸려는 상태(`InputManager::isMouseLockActive`)면 1 입니다 — 시나리오가 잠금 · 풀림을 고정 대기 없이 기다린다. */
        [[nodiscard]] bool readMouseLockActive( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineSubsystemsBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().isMouseLockActive() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Input.WindowFocused` 입니다. 입력 관리자가 받은 창 포커스 상태(포커스 메시지로 갱신)면 1 입니다 — 보낸 포커스 메시지가 처리됐는지 기다린다. */
        [[nodiscard]] bool readWindowFocused( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineSubsystemsBound() == false )
                return false;
            outValue = engine::getSubsystem<InputManager>().isWindowFocused() ? 1.0 : 0.0;
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( inputOSDeviceInputBlocked, "Input.OSDeviceInputBlocked", "1 while unattended automation drops the person's mouse and keyboard window events",
                         &readOSDeviceInputBlocked );
    SW_AUTOMATION_WATCH( inputVirtualCursorEnabled, "Input.VirtualCursorEnabled", "1 while the gamepad virtual cursor is on (command directors turn it on for play)",
                         &readVirtualCursorEnabled );
    SW_AUTOMATION_WATCH( inputMouseX, "Input.MouseX", "Mouse pointer X in window client pixels (the gamepad virtual cursor moves it too)", &readMouseX );
    SW_AUTOMATION_WATCH( inputMouseY, "Input.MouseY", "Mouse pointer Y in window client pixels (the gamepad virtual cursor moves it too)", &readMouseY );
    SW_AUTOMATION_WATCH( inputRawMouseEvents, "Input.RawMouseEvents", "Raw mouse motion reports the OS delivered so far (Win32 WM_INPUT, registered only while the cursor is locked); only grows",
                         &readRawMouseEvents );
    SW_AUTOMATION_WATCH( inputMouseLockActive, "Input.MouseLockActive", "1 while the engine wants the OS mouse lock on (a lock mode is requested and the game owns the pointer)",
                         &readMouseLockActive );
    SW_AUTOMATION_WATCH( inputWindowFocused, "Input.WindowFocused", "1 while the input manager holds window focus (updated by focus messages; true without a window)", &readWindowFocused );
} // namespace sw
