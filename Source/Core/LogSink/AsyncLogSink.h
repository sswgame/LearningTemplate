/**
 * @file AsyncLogSink.h
 * @brief 기본 로그 싱크입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, 한 줄은 출력 장치(`ILogOutput`)에 넘깁니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ConcurrentQueue.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogTypes.h"
#include "Core/Log/Logger.h"

namespace sw
{
    class FileLogOutput;
    class ILogOutput;

    // ------------------------------------------------------------------------------
    // 1) AsyncLogSink — 기본 싱크. 만들면 전역 싱크가 비어 있을 때 자신을 건다(`Logger::registerGlobalSink`)
    // ------------------------------------------------------------------------------
    /**
     * @class AsyncLogSink
     * @brief 기본 로그 싱크입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, **출력은 `ILogOutput` 에 넘깁니다.**
     * @details 장치마다 자기 락을 가지므로 파일 I/O 가 느려도 콘솔이 멈추지 않습니다. 기본으로 콘솔 · 파일 출력을 하나씩 달고
     *          시작하며, `addOutput` 으로 더 붙일 수 있습니다(에디터 패널 · 네트워크 등. 그때도 이 클래스를 고칠 필요는 없습니다).
     *          매크로가 부르는 전역 창구(`Logger`)는 아래층(Log)에 있고, 이 싱크는 파일 · 크래시 · 모듈을 알아야 해서 위층(LogSink)에 있습니다.
     */
    class SW_API AsyncLogSink final : public ILogSink
    {
    public:
        /** @brief 기본 출력 장치(콘솔 · 파일)를 하나씩 달고, 전역 싱크가 비어 있으면 자신을 등록합니다. */
        AsyncLogSink();
        /** @brief 전역 싱크가 자신이면 등록을 해제합니다. 출력 장치를 닫는 일은 shutdown 이 합니다. */
        virtual ~AsyncLogSink() override;

        AsyncLogSink( const AsyncLogSink& )            = delete;
        AsyncLogSink& operator=( const AsyncLogSink& ) = delete;

        /** @brief 출력 장치를 열고(로그 폴더 생성 포함) 비동기 I/O 작업 스레드를 시작합니다. */
        void initialize() override;
        /** @brief 큐를 모두 비우고 작업 스레드를 안전하게 끝냅니다. */
        void shutdown() override;

        /** @brief 한 줄을 포맷해 리스너와 출력 장치에 넘깁니다. */
        void writeLog( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) override;
        /** @brief 한 줄이 쓰일 때 호출할 리스너를 붙입니다. */
        DelegateHandle addLogWrittenListener( const LogWrittenDelegate& listener ) override;
        /** @brief 핸들로 리스너를 뗍니다. */
        void removeLogWrittenListener( const DelegateHandle& handle ) override;
        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 뗍니다. */
        uint32 releaseListenerCodeWithin( const void* pBegin, const void* pEnd ) override;

        /**
         * @brief 출력 장치를 하나 더 답니다. 이미 초기화된 뒤라면 바로 `open` 합니다.
         * @param output 소유권을 가져갑니다. nullptr 이면 무시합니다.
         * @return 실제로 달았으면 true. **상한(`_s_kMaxOutput`)을 넘으면 false** 이고, 그때 `output` 은 그대로 파괴됩니다.
         * @warning 상한을 넘기면 받아서 `open` 해 놓고 디스패치에서 말없이 빠뜨리는 대신, 거절하고 **거절했다고 알립니다.**
         *          조용히 무시하든 조용히 파괴하든 호출하는 쪽에게는 똑같이 보이지 않는 실패이기 때문입니다.
         */
        bool addOutput( unique_ptr<ILogOutput> output );

        /** @brief 로그 파일이 있는 폴더의 경로입니다. 파일 출력 장치에 물어 답합니다. */
        const string& getLogFolderPath() override;
        /**
         * @brief 큐에 남은 줄을 이 스레드에서 모두 쓰고 장치 버퍼를 내보냅니다. 싱크 락을 바로 잡지 못하면 포기합니다(기다리면 크래시가 멈춤이 된다).
         * @details 크래시 리포트 · "로그 + std::abort" 치명 경로가 부릅니다 — 큐에 넣기만 하고 프로세스가 끝나면 크래시 직전의 경고 · 정보
         *          줄과 파일의 stdio 버퍼가 사라집니다.
         */
        void flushForCrash() override;

    private:
        /** @brief 백그라운드 I/O 작업 스레드의 루프입니다. */
        void workerLoop();
        /** @brief 큐에 남은 로그를 모두 비우고 기록합니다. */
        void flushQueue();
        /** @brief 타임스탬프를 붙여 큐에 넣거나 바로 씁니다. */
        void writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line );
        /** @brief 달려 있는 모든 출력 장치에 한 줄을 넘깁니다. 장치마다 자기 락을 가집니다. */
        void dispatchToOutputs( const LogRecord& record );
        /**
         * @brief 리스너를 뗀 뒤, 떼기 **전에** 시작한 방송이 모두 끝날 때까지 기다립니다.
         * @details 방송은 리스너 목록을 복사해 락 밖에서 부릅니다. 기다리지 않으면 떼기가 돌아온 뒤에도 다른 스레드의 방송이 뗀 리스너를
         *          부를 수 있습니다(에디터 콘솔 패널이 파괴되거나 모듈이 내려간 뒤에 그 코드로 들어감). 이 스레드가 방송 도중(리스너 안)이면
         *          자기 자신을 기다리게 되므로 기다리지 않습니다. 끊임없는 로그로 세대가 비지 않으면 2 초 뒤 경고하고 돌아옵니다.
         * @param retiredSlot 떼기 직전 세대 칸(_mutex 안에서 읽은 값)
         */
        void waitForRetiredBroadcasts( uint32 retiredSlot );
        /** @brief 세대를 넘기고 넘기기 전 칸을 돌려줍니다. _mutex 를 쥐고 부릅니다. */
        uint32 retireBroadcastSlot();

        /**
         * @brief 달 수 있는 출력 장치의 최대 개수입니다.
         * @details `dispatchToOutputs` 는 잠금 안에서 포인터만 고정 배열로 복사해 오고 쓰기는 **락 밖에서** 합니다(느린 파일 I/O
         *          가 콘솔을 막지 않도록). 그 배열의 크기가 곧 이 상한이고, `addOutput` 도 이 값으로 거절합니다.
         */
        static constexpr uint32 _s_kMaxOutput = 8;

        LogWrittenMulticast              _onLogWritten;
        vector<unique_ptr<ILogOutput>>   _listOutput;
        FileLogOutput*                   _pFileOutput; ///< _listOutput 이 소유한다. getLogFolderPath 용 비소유 포인터
        ConcurrentQueue<LogRecord, 4096> _queue;
        std::thread                      _workerThread;
        std::condition_variable_any      _cv;
        mutex                            _mutex;         ///< 리스너 목록과 출력 목록을 보호한다
        mutex                            _cvMutex;       ///< 조건 변수 대기용 뮤텍스
        mutex                            _timeMutex;     ///< 타임스탬프 계산과 문자열 캐시를 보호한다
        std::time_t                      _cachedTimeSec; ///< 초 단위로 캐시한 시스템 시각
        int32                            _cachedYear;
        int32                            _cachedMonth;
        int32                            _cachedDay;
        int32                            _cachedHour;
        atomic<uint32>                   _arrBroadcastInFlight[2]; ///< 리스너를 부르는 중인 방송 수(세대 두 칸). 떼기가 이전 세대가 빌 때까지 기다린다
        uint32                           _broadcastEpoch;          ///< 방송 세대. _mutex 로 보호한다
        atomic<bool>                     _bIsRunning;
        atomic<bool>                     _bInitialized;                             ///< 쓰는 스레드는 락 없이 읽는다(초기화 · 종료와 경쟁하지 않게 원자값)
        utf8                             _arrCachedDateStr[constant::kMaxBuffer32]; ///< 캐시한 날짜 문자열(YYYY-M-D H:M: 형식)
    };
} // namespace sw
