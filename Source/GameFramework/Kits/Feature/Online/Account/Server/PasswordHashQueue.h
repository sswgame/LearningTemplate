/**
 * @file PasswordHashQueue.h
 * @brief 비밀번호 해시(Argon2id)를 저장소 워커가 아닌 작업 풀(`TaskManager`) 위에서 돌리는 상한 있는 줄입니다(감사 O-3).
 * @details 해시 하나가 19 MiB · 2 회라 저장소 워커에서 돌리면 로그인 폭주가 원장 · 우편 · 거래의 일을 뒤에 세웠다. 이 줄은 해시를 저장소 밖으로 빼고 두 상한을 둔다:
 *          동시에 도는 해시 `_maxConcurrentPasswordHashes`(메모리 상한 = 그 수 × 19 MiB × 2)와 기다리는 해시 `_maxQueuedPasswordHashes`.
 *          - **자리는 티켓으로만 얻는다.** `tryReserve` 가 (도는 + 기다리는 + 티켓) 수가 두 상한의 합이면 빈 값을 주고 부른 쪽은 `kBusy` 로 답한다. `submit` 은 티켓을
 *            요구하므로 수락 검사를 건너뛰는 제출은 쓸 수 없다. 티켓을 쓰지 않고 버리면 소멸이 자리를 돌려준다.
 *          - **완료는 주인 스레드(서비스 틱)가 거둔다**(`pollCompletions`). 워커는 자기 일 칸만 쓰고 끝난 번호를 잠금 아래 넘긴다.
 *          - **종료**(`shutdown`)는 도는 해시를 끝까지 기다리고(`waitStage`) 기다리던 일은 취소 완료로 돌려준다.
 *          OWASP Password Storage(KDF 동시 실행 제한), Keycloak Argon2 제공자의 `cpu-cores` 상한, 수락 제어는 Envoy overload manager 와 같은 자리다(기억에 의존).
 *          전용 스레드 풀은 두지 않는다 — 스레드 주인이 늘고 프레임 일과 코어를 다툰다(동시 실행 상한이 그것을 지킨다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetSecurityTypes.h"
#include "Core/Task/TaskTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountTypes.h"

namespace sw
{
    class IPasswordHasher;
    class PasswordHashQueue;
    class TaskManager;

    /** @brief 해시 줄의 상한입니다. 기본값이 없다 — 키트 설정(`AccountKitConfig`)이 정하고, 1 미만이면 `initialize` 가 기동 오류를 낸다. */
    struct PasswordHashQueueConfig
    {
        int32 _maxConcurrentPasswordHashes{ 0 }; ///< 동시에 도는 해시 수(작업 풀 워커를 이만큼까지 잡는다)
        int32 _maxQueuedPasswordHashes{ 0 };     ///< 자리를 기다리는 해시 수. 넘는 요청은 받지 않고 `kBusy` 로 끝난다
    };

    /** @brief 해시 하나의 솔트와 매개변수입니다. */
    struct PasswordHashSpec
    {
        NetPasswordHashParams _params{};
        uint8                 _arrSalt[LoginConstant::kSaltSize]{};
    };

    /** @brief 해시 일 하나입니다 — 비밀번호 하나로 해시를 하나 또는 둘(로그인 확인 + 매개변수가 바뀐 계정의 다시 해시) 계산합니다. */
    struct PasswordHashRequest
    {
        static constexpr int32 kMaxSpecCount = 2;

        SecretText       _password{}; ///< 워커가 해시를 끝내면 지운다
        PasswordHashSpec _arrSpec[kMaxSpecCount]{};
        int32            _specCount{ 0 };
    };

    /** @brief 해시 일 하나의 결과입니다. `_bComputed` 가 거짓이면 실패(제공자 오류) 또는 취소(`_bCancelled`)입니다. */
    struct PasswordHashCompletion
    {
        uint8  _arrHash[PasswordHashRequest::kMaxSpecCount][LoginConstant::kPasswordHashSize]{};
        uint64 _jobID{ 0 };
        uint8  _bComputed  : 1;
        uint8  _bCancelled : 1;

        PasswordHashCompletion()
            : _bComputed{ SW_FALSE }
            , _bCancelled{ SW_FALSE }
        {
        }
    };

    /**
     * @class PasswordHashTicket
     * @brief 줄의 자리 하나입니다. 이동만 됩니다. `PasswordHashQueue::submit` 이 받아 가고, 쓰지 않고 사라지면 자리를 돌려줍니다.
     */
    class [[nodiscard]] SW_KIT_SERVER_API PasswordHashTicket
    {
    public:
        PasswordHashTicket( PasswordHashTicket&& other ) noexcept;
        PasswordHashTicket& operator=( PasswordHashTicket&& other ) noexcept;
        PasswordHashTicket( const PasswordHashTicket& )            = delete;
        PasswordHashTicket& operator=( const PasswordHashTicket& ) = delete;
        ~PasswordHashTicket();

    private:
        friend class PasswordHashQueue;
        explicit PasswordHashTicket( PasswordHashQueue* pQueue )
            : _pQueue{ pQueue }
        {
        }
        /** @brief 자리를 줄에 넘깁니다(제출). 이 뒤의 소멸은 아무것도 돌려주지 않는다. */
        void release() { _pQueue = nullptr; }

    private:
        PasswordHashQueue* _pQueue;
    };

    /**
     * @class PasswordHashQueue
     * @brief 서비스 틱 스레드가 주인인 상한 있는 해시 줄입니다(파일 머리말).
     */
    class SW_KIT_SERVER_API PasswordHashQueue
    {
    public:
        PasswordHashQueue();
        ~PasswordHashQueue();
        PasswordHashQueue( const PasswordHashQueue& )            = delete;
        PasswordHashQueue& operator=( const PasswordHashQueue& ) = delete;

        /** @brief 작업 풀과 해시 구현(스레드 안전)을 빌리고 상한을 정합니다. 상한이 1 미만이면 오류입니다(기본값으로 대신하지 않는다). */
        [[nodiscard]] expected<void> initialize( TaskManager& taskManager, IPasswordHasher& hasher, const PasswordHashQueueConfig& settings );
        /** @brief 자리 하나를 잡습니다. (도는 + 기다리는 + 쓰지 않은 티켓) 수가 두 상한의 합이면 빈 값입니다 — 부른 쪽이 `kBusy` 로 답한다. */
        [[nodiscard]] optional<PasswordHashTicket> tryReserve();
        /** @brief 티켓의 자리로 해시를 맡깁니다. 돌 자리가 있으면 바로 작업 풀에, 없으면 기다리는 줄에 넣습니다. 반환값은 완료의 `_jobID` 입니다. */
        uint64 submit( PasswordHashTicket&& ticket, PasswordHashRequest&& request );
        /** @brief 끝난 해시를 @p outListCompletion 뒤에 옮기고 기다리던 일을 돌 자리에 올립니다. 옮긴 수를 돌려줍니다. 주인 스레드에서 부릅니다. */
        int32 pollCompletions( vector<PasswordHashCompletion>& outListCompletion );
        /**
         * @brief 도는 해시가 없거나 @p timeoutMs 가 지날 때까지 기다립니다(작업 풀 일을 돕지 않고 잔다 — 해시가 주인 스레드에서 돌지 않게). 기다릴 것이 없으면 true 입니다.
         * @details 테스트 · 종료 드레인이 완료를 기다리는 데 쓴다. 완료는 그 뒤 `pollCompletions` 가 거둔다.
         */
        bool waitForRunning( int64 timeoutMs );
        /** @brief 잡힌 자리 수입니다(도는 + 기다리는 + 쓰지 않은 티켓 + 거두지 않은 완료). 서비스의 `getPendingCount` 에 더한다. */
        int32 getOpenCount() const { return _reservedCount; }
        int32 getRunningCount() const { return _runningCount; }
        /** @brief 도는 해시를 끝까지 기다리고(완료는 @p outListCompletion 에), 기다리던 일은 취소 완료(`_bCancelled`)로 돌려줍니다. 주인 스레드에서 부릅니다. */
        void shutdown( vector<PasswordHashCompletion>& outListCompletion );

    private:
        friend class PasswordHashTicket;

        /** @brief 작업 풀에 맡긴 해시 하나입니다. 워커는 이 칸만 쓴다. */
        struct HashJob
        {
            PasswordHashQueue*     _pOwner{ nullptr };
            PasswordHashRequest    _request{};
            PasswordHashCompletion _completion{};

            void run();
        };

        void returnSeat();
        void launch( unique_ptr<HashJob> pJob );
        /** @brief 워커가 해시를 끝낸 일을 넘깁니다(잠금 아래). */
        void markFinished( HashJob* pJob );

    private:
        mutable mutex                  _finishedMutex;
        vector<HashJob*> _listFinished SW_GUARDED_BY( _finishedMutex ); ///< 워커가 끝낸 일 — 주인이 `pollCompletions` 에서 거둔다
        vector<unique_ptr<HashJob>>    _listRunning;                    ///< 작업 풀에 맡긴 일 — 주인만 넣고 뺀다(워커는 자기 칸만 쓴다)
        deque<unique_ptr<HashJob>>     _listWaiting;                    ///< 돌 자리를 기다리는 일(맡긴 순서)
        TaskStageHandle                _stage;                          ///< 맡긴 해시 전부 — 종료가 기다린다
        TaskManager*                   _pTaskManager;
        IPasswordHasher*               _pHasher;
        PasswordHashQueueConfig        _settings;
        uint64                         _nextJobID;
        int32                          _reservedCount;
        int32                          _runningCount;
    };
} // namespace sw
