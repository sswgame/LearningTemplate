/**
 * @file QuitRequest.h
 * @brief 프로세스를 이 종료 코드로 끝내 달라는 요청입니다(서비스 범위 서브시스템). 호스트 · 엔진 · 모듈(편집기 · 게임) 누구나 `engine::getSubsystem<QuitRequest>()` 로 건다.
 * @details 예전에는 `Engine::requestQuit` 하나라 엔진을 쥔 호스트만 부를 수 있었고, 편집기 자체 테스트는 창만 닫아 실패해도 종료 코드가 0 이었다.
 *          루프는 엔진이 이 요청을 함께 보고 끝내며(`Engine::isQuitRequested`), 처음 요청의 코드가 이긴다(언리얼 `FPlatformMisc::RequestExitWithStatus` 와 같은 자리).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class QuitRequest
     * @brief 종료 요청과 그 코드입니다. 파일 머리말 참고. 게임 스레드에서 걸고 읽는다.
     */
    class SW_API QuitRequest final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "QuitRequest";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 이번 프레임이 끝나면 루프를 끝내라고 요청합니다. 이미 요청이 있으면 처음 것의 코드를 둔다. */
        void request( int32 exitCode );
        bool isRequested() const { return _bRequested; }
        /** @brief 요청한 종료 코드입니다(요청이 없었으면 0). */
        int32 getExitCode() const { return _exitCode; }

    private:
        int32 _exitCode{ 0 };
        bool  _bRequested{ false };
    };
} // namespace sw
