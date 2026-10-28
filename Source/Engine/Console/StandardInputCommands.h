/**
 * @file Engine/Console/StandardInputCommands.h
 * @brief 호스트(App · Server)의 표준 입력 명령 채널입니다 — 줄을 읽고, 종료 명령(`quit` · `stop`)을 알아보고, 나머지 줄을 호스트에 넘깁니다.
 * @details 종료 낱말을 아는 곳은 여기 하나입니다. 나머지 줄을 무엇으로 처리할지(개발 콘솔, 서버 루프 명령)는 호스트가 델리게이트로 정합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class StandardInputReader;

    /** @brief 종료 명령이 아닌 표준 입력 한 줄(앞뒤 공백을 떼고 비지 않은 것)을 받는 처리 함수입니다. */
    SW_DECLARE_DELEGATE( void, StandardInputCommandDelegate, string_view );
} // namespace sw

namespace sw
{
    /**
     * @class StandardInputCommands
     * @brief 호스트가 값 멤버로 들고 루프마다 `poll` 합니다. 시작하지 않았으면 `poll` 은 아무것도 하지 않습니다.
     * @details 부모 프로세스(에디터의 멀티 프로세스 재생, 서버 운영 도구)가 창 닫기나 종료 신호 없이 호스트를 정상 종료하고 명령을 보내는 채널입니다.
     */
    class SW_API StandardInputCommands
    {
    public:
        StandardInputCommands();
        ~StandardInputCommands();

        StandardInputCommands( const StandardInputCommands& )            = delete;
        StandardInputCommands& operator=( const StandardInputCommands& ) = delete;

        /** @brief 읽기 스레드를 시작합니다. */
        void start();
        /** @brief 읽기 스레드를 멈추고 기다립니다. 줄 버퍼도 놓습니다(엔진 종료의 누수 보고 전에 부른다). */
        void stop();

        /**
         * @brief 쌓인 줄을 처리합니다. `quit` 이나 `stop` 이면 거기서 멈추고 true 를 돌려주며, 그 밖의 비지 않은 줄은 @p onCommand 로 넘깁니다.
         * @return 종료를 요청받았으면 true 입니다. 시작하지 않았으면 false 입니다.
         */
        [[nodiscard]] bool poll( const StandardInputCommandDelegate& onCommand );

    private:
        unique_ptr<StandardInputReader> _reader;   ///< `start` 뒤에만 있다
        vector<string>                  _listLine; ///< 읽기 스레드에서 꺼낸 줄(루프마다 재사용)
    };
} // namespace sw
