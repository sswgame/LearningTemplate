/**
 * @file ServerConsole.h
 * @brief 전용 서버의 표준 입력 명령 줄입니다 — 읽기 스레드가 줄을 모으고, 게임 스레드가 틱마다 꺼내 처리합니다.
 * @details 표준 입력이 닫혀도(EOF — systemd · 서비스 · `< /dev/null`) 서버를 끝내지 않습니다(그 환경에서 서버가 기동 직후 내려간다).
 *          읽기 스레드는 멈출 수 있어야 합니다: POSIX 는 `poll( 0, 100 ms )` 뒤 `read`, Windows 는 표준 입력 종류마다 —
 *          콘솔이면 `WaitForSingleObject( 입력, 100 ms )` 로 키 입력이 있을 때만 읽고, 파이프면 `PeekNamedPipe` 를 50 ms 마다,
 *          그 밖(파일 · NUL)은 끝까지 읽고 스레드를 끝냅니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include <thread>

namespace sw
{
    /**
     * @class ServerConsole
     * @brief 표준 입력의 줄을 읽기 스레드에서 모아 게임 스레드에 넘깁니다.
     */
    class ServerConsole
    {
    public:
        ServerConsole();
        ~ServerConsole();

        ServerConsole( const ServerConsole& )            = delete;
        ServerConsole& operator=( const ServerConsole& ) = delete;

        /** @brief 읽기 스레드를 띄웁니다. 두 번 불러도 한 번입니다. */
        void start();
        /** @brief 읽기 스레드를 멈추고 기다립니다. 두 번 불러도 됩니다. */
        void stop();
        /** @brief 모인 줄을 @p outListLine 뒤에 옮깁니다(게임 스레드). */
        void drainLines( vector<string>& outListLine );

    private:
        void readLoop();
        /** @brief 한 줄을 읽습니다. 멈춤 요청 · EOF 면 false — EOF 는 `_endOfInput` 을 세운다(종료가 아니다). 플랫폼 파일이 정의한다. */
        [[nodiscard]] bool readLinePlatform( string& outLine );
        /** @brief 쌓인 조각에서 줄 하나를 떼어 냅니다(끝의 CR 은 버린다). 줄이 아직 없으면 false 입니다. */
        bool takeLine( string& outLine );

    private:
        std::thread    _thread;
        mutex          _mutex;
        vector<string> _listLine;    ///< 읽기 스레드가 넣고 게임 스레드가 꺼낸다(_mutex)
        string         _partialLine; ///< 읽기 스레드만 — 줄바꿈을 기다리는 조각
        atomic<uint32> _stopRequested;
        atomic<uint32> _endOfInput;
    };
} // namespace sw
