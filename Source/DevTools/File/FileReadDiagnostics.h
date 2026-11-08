/**
 * @file FileReadDiagnostics.h
 * @brief 파일 읽기의 개발 진단입니다 — 디스크 흉내(지연 · 대역폭)와 읽기 기록(개발 도구 `DevTools` — 배포 빌드는 짓지 않는다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @class FileReadDiagnostics
     * @brief 파일 계층의 읽기 관찰자(`IFileReadObserver`)로 붙어 디스크 흉내와 읽기 기록을 합니다. 개발 도구 진입점(`DevToolsHost`)이 만들며 걸고, 소멸하며 뗀다.
     * @details - 디스크 흉내(`-gv_ioEmulationReadLatencyMs` · `-gv_ioEmulationReadKilobytesPerSecond`, 부하 검증의 하드웨어 프로필): 장치 읽기 한 번만큼 부른 스레드를 재운다.
     *            느린 디스크의 지연 · 대역폭을 소프트웨어로 흉내 낸 것이라 OS 캐시는 흉내 내지 않는다. 장치는 하나이고 큐 깊이는 1 이다 —
     *            여러 스레드의 읽기가 줄을 서므로 읽기를 나눠 부른다고 빨라지지 않는다. 대기는 고해상도 마감(`MonotonicClock::sleepUntilNanoseconds`)이라
     *            1 ms 가 Windows 기본 타이머 주기(약 15.6 ms)로 늘지 않는다.
     *          - 읽기 기록(`-gv_ioLogReads=1`): 읽으려고 연 파일마다 경로 · 바이트 · 스레드를 로그로 남긴다(언리얼 `-FileOpenLog` 와 같은 진단).
     *          값은 부를 때마다 전역 변수에서 읽는다 — 명령줄이 풀리기 전의 읽기도 같은 길로 지나고, 실행 중에 바꿔도 다음 읽기부터 든다.
     *          언리얼은 같은 일을 개발 모듈의 `IPlatformFile` 장식 층(`FLoggedPlatformFile`)으로 끼운다 — 파일 계층은 관찰자 창구만 갖는다.
     */
    class SW_DEVTOOLS_API FileReadDiagnostics final : public IFileReadObserver
    {
    public:
        FileReadDiagnostics();
        ~FileReadDiagnostics() override;

        void recordDeviceRead( size_t byteCount ) override;
        void recordFileOpenedForRead( string_view filePath, size_t byteCount ) override;

    private:
        atomic<int64> _emulatedDiskBusyUntilNano; ///< 디스크 흉내의 장치가 비는 시각(`MonotonicClock` 나노초) — 읽기마다 이 뒤에 제 지연을 붙인다
    };
} // namespace sw
