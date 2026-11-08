/**
 * @file DevTools/DevToolsClientPart.h
 * @brief 개발 도구의 클라이언트 전용 부분(디버그 HUD · 셰이더 라이브 리로드 · 콘솔 오버레이)을 개발 도구 호스트에 붙이는 자리입니다.
 * @details 구현은 `DevTools/Client/` 소스가 정적 등록으로 하나 건다. 전용 서버는 그 소스를 짓지 않아(`SW_CLIENT_ONLY_SOURCE_PATTERNS`) 창구가 없고,
 *          호스트 · 콘솔은 그 부분 없이 선다(설정 리로드 · 콘솔의 입력 · 시작 명령은 서버에도 선다). 예전에는 `#if SW_WITH_CLIENT_CODE` 구역이었다.
 *          DevTools 는 리로드하지 않는 공유 라이브러리라 등록은 이미지 수명 동안 하나다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/EngineFrameHooks.h"

namespace sw
{
    class IDevConsoleView;
    class InputMap;
    class SubsystemScope;

    namespace engine
    {
        struct StartupParts;
    } // namespace engine

    /** @class IDevToolsClientPart @brief 개발 도구의 클라이언트 전용 부분입니다. 게임 스레드에서 부릅니다. */
    class SW_DEVTOOLS_API IDevToolsClientPart
    {
    public:
        virtual ~IDevToolsClientPart() = default;

        /** @brief 기동 단계에 클라이언트 전용 개발 서브시스템(셰이더 라이브 리로드 · 디버그 HUD)을 더합니다. */
        virtual void addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts ) = 0;
        /** @brief 씬 틱 뒤 · 런타임 UI 앞입니다(디버그 HUD). */
        virtual void onAfterSceneTick( InputMap* pShellMap ) = 0;
        /** @brief 프레임 끝입니다(셰이더 재컴파일 · 강제 리로드 단축키 · 시작 인자 사진). 프로세스를 끝낼 종료 코드(`-gv_screenshotQuit`)를 돌려줍니다. */
        [[nodiscard]] virtual EngineFrameExit onFrameEnd() = 0;
        /** @brief 콘솔의 그리는 쪽(런타임 UI 오버레이)입니다. UI 가 없으면 nullptr 입니다. */
        [[nodiscard]] virtual unique_ptr<IDevConsoleView> createConsoleView() = 0;

        /** @brief 등록된 창구입니다. 클라이언트 전용 소스가 없으면(전용 서버) nullptr 입니다. */
        [[nodiscard]] static IDevToolsClientPart* find();

        /** @struct Registration @brief 클라이언트 전용 소스가 정적 객체로 창구를 겁니다(이미지에 하나). */
        struct SW_DEVTOOLS_API Registration
        {
            explicit Registration( IDevToolsClientPart& part );
        };
    };
} // namespace sw
