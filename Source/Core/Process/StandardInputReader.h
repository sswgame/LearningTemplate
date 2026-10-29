/**
 * @file StandardInputReader.h
 * @brief 표준 입력의 명령 줄을 읽습니다 — 읽기 스레드가 줄을 모으고, 게임 스레드가 틱 · 프레임마다 꺼내 처리합니다.
 * @details 전용 서버의 콘솔 명령과, 부모 프로세스(에디터의 멀티 프로세스 재생)가 띄운 App 의 명령 채널이 같이 씁니다.
 *          표준 입력이 닫혀도(EOF — systemd · 서비스 · `< /dev/null`) 읽기 스레드만 끝나고 프로세스는 계속 돕니다(그 환경에서 서버가 기동 직후 종료된다).
 *          읽기 스레드는 멈출 수 있어야 합니다: POSIX 는 `poll( 0, 100 ms )` 뒤 `read`, Windows 는 표준 입력 종류마다 —
 *          콘솔이면 `WaitForSingleObject( 입력, 100 ms )` 로 키 입력이 있을 때만 읽고, 파이프면 `PeekNamedPipe` 를 50 ms 마다,
 *          그 밖(파일 · NUL)은 끝까지 읽고 스레드를 끝냅니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include <thread>

namespace sw
{
    /**
     * @class StandardInputReader
     * @brief 표준 입력의 줄을 읽기 스레드에서 모아 게임 스레드에 넘깁니다.
     */
    class SW_API StandardInputReader
    {
    public:
        StandardInputReader();
        ~StandardInputReader();

        StandardInputReader( const StandardInputReader& )            = delete;
        StandardInputReader& operator=( const StandardInputReader& ) = delete;

        /** @brief 읽기 스레드를 띄웁니다. 두 번 불러도 한 번입니다. */
        void start();
        /** @brief 읽기 스레드를 멈추고 기다립니다. 두 번 불러도 됩니다. */
        void stop() SW_REQUIRES_LOCK( _mutex );
        /** @brief 모인 줄을 @p outListLine 뒤에 옮깁니다(게임 스레드). */
        void drainLines( vector<string>& outListLine );

    private:
        /** @brief 읽기 스레드 본문입니다. @p stopToken 이 정지를 받거나 입력이 닫히면 끝납니다. */
        void readLoop( StopToken stopToken );
        /** @brief 한 줄을 읽습니다. 멈춤 요청(@p stopToken) · EOF 면 false — EOF 는 `_endOfInput` 을 켠다(종료가 아니다). 플랫폼 파일이 정의한다. */
        [[nodiscard]] bool readLinePlatform( const StopToken& stopToken, string& outLine );
        /** @brief 쌓인 조각에서 줄 하나를 분리합니다(끝의 CR 은 버린다). 줄이 아직 없으면 false 입니다. */
        bool takeLine( string& outLine );

    private:
        sw::thread               _thread;
        mutex                    _mutex;
        vector<string> _listLine SW_GUARDED_BY( _mutex ); ///< 읽기 스레드가 넣고 게임 스레드가 꺼낸다(_mutex)
        string                   _partialLine;            ///< 읽기 스레드만 — 줄바꿈을 기다리는 조각
        atomic<uint32>           _endOfInput;
    };
} // namespace sw
