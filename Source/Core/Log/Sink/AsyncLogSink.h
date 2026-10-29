/**
 * @file AsyncLogSink.h
 * @brief 기본 로그 싱크입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, 한 줄은 출력 장치(`ILogOutput`)에 넘깁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/ConcurrentQueue.h"
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include <condition_variable>

namespace sw
{
    class FileLogOutput;
    class ILogOutput;

    // ------------------------------------------------------------------------------
    // 1) AsyncLogSink — 기본 싱크. 만들면 전역 싱크가 비어 있을 때 자신을 등록한다(`Logger::registerGlobalSink`)
    // ------------------------------------------------------------------------------
    /**
     * @class AsyncLogSink
     * @brief 기본 로그 싱크입니다. 포맷 · 타임스탬프 · 리스너 · 비동기 큐를 맡고, **출력은 `ILogOutput` 에 넘깁니다.**
     * @details 장치마다 자기 락을 가지므로 파일 I/O 가 느려도 콘솔이 멈추지 않습니다. 기본으로 콘솔 · 파일 출력을 하나씩 달고
     *          시작하며, `addOutput` 으로 더 붙일 수 있습니다(에디터 패널 · 네트워크 등. 그때도 이 클래스를 고칠 필요는 없습니다).
     *          매크로가 부르는 전역 진입점(`Logger`)는 아래층(Log)에 있고, 이 싱크는 파일 · 크래시 · 모듈을 알아야 해서 위층(LogSink)에 있습니다.
     */
    class SW_API AsyncLogSink final : public ILogSink
    {
    public:
        /**
         * @brief 기본 출력 장치(콘솔 · 파일)를 하나씩 달고, 전역 싱크가 비어 있으면 자신을 등록합니다.
         * @param fixedLogFilePath 파일 장치가 쓸 파일 하나(`-log-file`). 비면 `Saved/Logs` 에 날짜 · 세션 이름으로 쓴다(`FileLogOutput`).
         */
        explicit AsyncLogSink( string fixedLogFilePath = {} );
        /** @brief 전역 싱크가 자신이면 등록을 해제합니다. 출력 장치를 닫는 일은 shutdown 이 합니다. */
        virtual ~AsyncLogSink() override;

        AsyncLogSink( const AsyncLogSink& )            = delete;
        AsyncLogSink& operator=( const AsyncLogSink& ) = delete;

        /** @brief 출력 장치를 열고(로그 폴더 생성 포함) 비동기 I/O 작업 스레드를 시작합니다. */
        void initialize() override;
        /** @brief 큐를 모두 비우고 작업 스레드를 안전하게 끝냅니다. */
        void shutdown() override;

        /** @brief 한 줄을 포맷해 리스너와 출력 장치에 넘깁니다. */
        void writeLogWithID( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID ) override;
        /** @brief 한 줄이 쓰일 때 호출할 리스너를 붙이고 이 싱크가 소유한 구독을 돌려줍니다. 어느 스레드에서나 부를 수 있습니다(`_mutex`). */
        Subscription registerLogWrittenListener( const LogWrittenDelegate& listener ) override;
#if SW_WITH_DYNAMIC_MODULES // 모듈 코드 범위의 리스너 떼기 — 배포 빌드는 모듈을 내리지 않는다
        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 리스너를 모두 뗍니다. */
        uint32 removeListenerCodeWithin( const void* pBegin, const void* pEnd ) override;
#endif

        /**
         * @brief 출력 장치를 하나 더 답니다. 이미 초기화된 뒤라면 바로 `open` 합니다.
         * @param output 소유권을 가져갑니다. nullptr 이면 무시합니다.
         * @return 실제로 달았으면 true. **상한(`_s_kMaxOutput`)을 넘으면 false** 이고, 그때 `output` 은 그대로 파괴됩니다.
         * @warning 상한을 넘기면 받아서 `open` 해 놓고 디스패치에서 말없이 빠뜨리는 대신, 거절하고 **거절했다고 알립니다.**
         *          조용히 무시하든 조용히 파괴하든 호출하는 쪽에게는 똑같이 보이지 않는 실패이기 때문입니다.
         */
        bool addOutput( unique_ptr<ILogOutput> output );

        /** @brief 로그 파일이 있는 폴더의 경로입니다. 파일 출력 장치에 물어 답합니다. */
        string_view getLogFolderPath() override;
        string      getLogFilePath() override;
        /**
         * @brief 큐에 남은 줄을 이 스레드에서 모두 쓰고 장치 버퍼를 내보냅니다. 싱크 락을 바로 잡지 못하면 포기합니다(기다리면 크래시가 멈춤이 된다).
         * @details 크래시 리포트 · "로그 + std::abort" 치명 경로가 부릅니다 — 큐에 넣기만 하고 프로세스가 끝나면 크래시 직전의 경고 · 정보
         *          줄과 파일의 stdio 버퍼가 사라집니다.
         */
        void flushForCrash() override;
        /** @brief `JSONLinesLogOutput` 을 달고 콘솔 장치를 끕니다(두 번째부터는 true 만 돌려줍니다). 장치 상한에 걸리면 false 입니다. */
        bool enableJSONLinesOutput() override;

    private:
        /** @brief 백그라운드 I/O 작업 스레드의 루프입니다. */
        void workerLoop();
        /** @brief 큐에 남은 로그를 모두 비우고 기록합니다. */
        void flushQueue();
        /** @brief 타임스탬프를 붙여 큐에 넣거나 바로 씁니다. */
        void writeLogInternal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID );
        /** @brief 메모리 리스너(에디터 콘솔 UI · 테스트 캡처)에게 한 줄을 알립니다. 락 밖에서 부르며 그 세대 칸에 "부르는 중" 을 올려 둡니다. */
        void broadcastToListeners( const LogEntry& entry );
        /** @brief 레코드를 비동기 I/O 큐에 넣고 작업 스레드를 깨웁니다. 초기화 전이거나 큐가 가득 차면 이 스레드에서 바로 씁니다. */
        void submitRecord( LogRecord&& record );
        /** @brief 달려 있는 모든 출력 장치에 한 줄을 넘깁니다. 장치마다 자기 락을 가집니다. */
        void dispatchToOutputs( const LogRecord& record );
        /**
         * @brief 리스너를 제거한 뒤, 제거 **전에** 시작한 방송이 모두 끝날 때까지 기다립니다.
         * @details 방송은 리스너 목록을 복사해 락 밖에서 부릅니다. 기다리지 않으면 제거가 돌아온 뒤에도 다른 스레드의 방송이 제거한 리스너를
         *          부를 수 있습니다(에디터 콘솔 패널이 파괴되거나 모듈이 언로드된 뒤에 그 코드로 들어감). 이 스레드가 방송 도중(리스너 안)이면
         *          자기 자신을 기다리게 되므로 기다리지 않습니다. 끊임없는 로그로 세대가 비지 않으면 2 초 뒤 경고하고 돌아옵니다.
         * @param retiredSlot 제거 직전 세대 슬롯(_mutex 안에서 읽은 값)
         */
        void waitForRetiredBroadcasts( uint32 retiredSlot );
        /** @brief 세대를 넘기고 넘기기 전 칸을 돌려줍니다. _mutex 를 쥐고 부릅니다. */
        uint32 retireBroadcastSlot() SW_REQUIRES_LOCK( _mutex );
        /**
         * @brief 리스너 목록이 바뀐 뒤 생산자가 볼 사본(`_listenerSnapshot`)을 새로 만듭니다. _mutex 를 쥐고 부릅니다.
         * @details 생산자는 줄마다 목록을 복사하지 않고 이 사본의 참조 하나만 잡습니다(리스너가 바뀔 때만 복사한다).
         */
        void rebuildListenerSnapshot() SW_REQUIRES_LOCK( _mutex );
        /** @brief 리스너 구독 @p id 가 풀렸습니다. 항목을 떼고 그 전에 시작한 방송이 끝나기를 기다립니다(`waitForRetiredBroadcasts`). */
        void releaseSubscription( uint64 id );

        /**
         * @brief 달 수 있는 출력 장치의 최대 개수입니다.
         * @details `dispatchToOutputs` 는 잠금 안에서 포인터만 고정 배열로 복사해 오고 쓰기는 **락 밖에서** 합니다(느린 파일 I/O
         *          가 콘솔을 막지 않도록). 그 배열의 크기가 곧 이 상한이고, `addOutput` 도 이 값으로 거절합니다.
         */
        static constexpr uint32 _s_kMaxOutput = 8;

        /**
         * @brief 달려 있는 출력 장치의 포인터를 @p outArrDevice 에 복사하고 개수를 돌려줍니다. 장치를 부르는 일(`open` · `close` · `write`)은 모두 이 복사본으로 **락 밖에서** 합니다.
         * @details `_mutex` 는 잎 락입니다 — 락을 쥔 채 로그 · 장치 · 리스너를 부르지 않습니다. 그 호출이 남기는 로그가 이 싱크로 되돌아와 같은 `_mutex` 를 잡으면
         *          재진입 불가라 같은 스레드가 영구 정지합니다(예: 로그 폴더를 못 만들어 남긴 오류가 `initialize` 를 멈춤). 장치는 `addOutput` 으로 붙기만 하고
         *          떼지 않으므로(싱크가 소유) 복사한 포인터는 싱크 수명 동안 유효합니다.
         */
        uint32 snapshotOutputs( ILogOutput* ( &outArrDevice )[_s_kMaxOutput] );

        LogWrittenMulticast _onLogWritten SW_GUARDED_BY( _mutex );
        /**
         * @brief 등록된 리스너의 구독과, 그 구독을 찾는 핸들입니다. _mutex 로 보호한다.
         * @details 바깥에 돌려준 구독은 이 싱크가 낸 것(id = 핸들)이다. 그것이 풀리면 핸들로 안쪽 구독(멀티캐스트 항목)을 찾아 지운다 —
         *          멀티캐스트의 구독을 그대로 내주면 해제가 `_mutex` 와 방송 대기를 지나지 않는다.
         */
        vector<pair<DelegateHandle, Subscription>> _listListenerSubscription SW_GUARDED_BY( _mutex );
        vector<unique_ptr<ILogOutput>> _listOutput                           SW_GUARDED_BY( _mutex );
        FileLogOutput*                                                       _pFileOutput; ///< _listOutput 이 소유한다. getLogFolderPath 용 비소유 포인터
        ConcurrentQueue<LogRecord, 4096>                                     _queue;
        sw::thread                                                           _workerThread;
        std::condition_variable_any                                          _cv;
        mutex                                                                _mutex;   ///< 리스너 목록과 출력 목록을 보호한다
        mutex                                                                _cvMutex; ///< 조건 변수 대기용 뮤텍스
        /** @brief 리스너 목록의 불변 사본입니다. 리스너가 없으면 nullptr 입니다. _mutex 로 보호하고, 생산자는 참조만 잡아 락 밖에서 부른다. */
        shared_ptr<const LogWrittenMulticast> _listenerSnapshot SW_GUARDED_BY( _mutex );
        atomic<uint32>                                          _arrBroadcastInFlight[2]; ///< 리스너를 부르는 중인 방송 수(세대 두 슬롯). 제거가 이전 세대가 빌 때까지 기다린다
        uint32 _broadcastEpoch                                  SW_GUARDED_BY( _mutex );  ///< 방송 세대. _mutex 로 보호한다
        atomic<bool>                                            _bIsRunning;
        atomic<bool>                                            _bInitialized;      ///< 쓰는 스레드는 락 없이 읽는다(초기화 · 종료와 경쟁하지 않게 원자값)
        atomic<bool>                                            _bHasListener;      ///< 리스너가 하나라도 있으면 true. 생산자가 없을 때 _mutex 를 건너뛴다
        atomic<bool>                                            _bWantsStructured;  ///< `needsStructuredFields` 장치가 달렸으면 true. 생산자가 줄마다 JSON 을 만들지 정한다
        atomic<bool>                                            _bJSONLinesEnabled; ///< `enableJSONLinesOutput` 이 이미 장치를 달았다
        /**
         * @brief 작업 스레드가 조건 변수에서 잠들려 하거나 잠들어 있으면 true 입니다. 생산자는 이때만 깨웁니다.
         * @details 작업 스레드는 `_cvMutex` 를 쥔 채 이 값을 올리고(seq_cst) 큐를 확인한 뒤 잠들고, 생산자는 넣기 → 펜스 → 이 값을 봅니다(Dekker 짝).
         *          생산자는 깨우기 전에 `_cvMutex` 를 한 번 잡았다 놓아, 작업 스레드가 확인과 잠들기 사이에 있을 때 깨우기를 놓치지 않게 합니다.
         */
        atomic<bool> _bWorkerSleeping;

    private:
        /** @brief 이 객체가 내준 구독입니다(믹스인 상속 대신 합성). 풀리면 `releaseSubscription` 이 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&AsyncLogSink::releaseSubscription>( this ) };
    };
} // namespace sw
