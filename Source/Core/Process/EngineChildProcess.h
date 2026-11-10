/**
 * @file EngineChildProcess.h
 * @brief 엔진 실행 파일(App · Server)을 자식으로 띄우는 명령의 한 입구입니다 — 실행 파일 · 인자 · 자동화 판정 전달을 함께 쥡니다.
 * @details 자동화 실행(`-unattended` · `-scenario` — `CommandLineManager::isAutomatedRun`)의 자식도 자동화 실행이어야 한다. 빠지면 자식이 사람의 실행으로 떠
 *          사용자 설정을 읽고 사양으로 품질을 골라(WSL 의 클라이언트가 low) 서버와 합성이 갈렸고, 사람의 설정 · 세이브를 건드렸다.
 *          엔진 자식은 이것으로만 만든다 — `ProcessCommand::at` 을 직접 부르는 모양은 린트(common-shapes 의 engineChildCommand)가 막고,
 *          엔진이 아닌 도구(Tracy · tint · 편집기)는 그 예외 표에 까닭과 함께 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Process/Process.h"

namespace sw
{
    class CommandLineManager;

    /**
     * @struct EngineChildProcess
     * @brief 엔진 자식 명령을 만듭니다. 상태가 없습니다.
     */
    struct SW_API EngineChildProcess
    {
        /**
         * @brief 엔진 실행 파일 @p executablePath 를 @p listArgument 로 띄우는 명령입니다. @p parent 가 자동화 실행이면 `-unattended` 를 더합니다(이미 있으면 그대로).
         * @return 경로가 절대 경로가 아니면 빈 명령입니다 — 띄우기가 실패로 알린다.
         */
        [[nodiscard]] static ProcessCommand makeCommand( const CommandLineManager& parent, string_view executablePath, vector<string> listArgument );
        /** @brief 이 프로세스의 실행 파일(`FileUtil::getExecutablePath`)을 띄우는 `makeCommand` 입니다. */
        [[nodiscard]] static ProcessCommand makeSelfCommand( const CommandLineManager& parent, vector<string> listArgument );
    };
} // namespace sw
