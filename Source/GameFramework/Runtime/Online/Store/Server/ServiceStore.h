/**
 * @file ServiceStore.h
 * @brief 서비스(계정, 경제, 거래)의 저장 계약을 정의합니다.
 * @details (테이블, 키)로 레코드(바이트와 버전)를 찾고, 조건부 쓰기를 트랜잭션으로 묶고, 키 범위를 읽는다.
 *          이 모든 것을 저장소 스레드에서 도는 "작업"으로 요청하는 비동기 인터페이스도 여기서 정의한다.
 *
 *          - 버전(`ServiceRecord::_version`)은 저장소 전체에서 커밋마다 오르는 수이다. 지웠다 다시 만든 키도 옛 버전을 다시 받지 않으므로 ABA 가 없다. 빈틈은 있어도 된다.
 *          - 쓰기는 모두 트랜잭션이다. 쓰기마다 기대 버전을 준다. `kAnyVersion` 은 조건 없음이고, `kAbsentVersion` 은 "없어야 한다"이다.
 *            하나라도 어긋나면 아무것도 쓰지 않고 `Conflict` 를 반환한다. `requireVersion` 은 쓰지 않고 버전만 본다. 읽은 값이 그새 바뀌지 않았는지 확인할 때 쓴다.
 *            `requireRange` 는 목록 읽기 하나가 지금도 같은 결과(키 · 버전 순서열)인지 본다 — 그사이 범위에 든 레코드(팬텀)를 막는다. 저장소는 같은 트랜잭션 안에서 다시 읽어 견준다.
 *          - `Unavailable` 은 "적용됐는지 모른다"를 포함한다. 돈이나 아이템이 움직이는 커밋은 같은 트랜잭션에 멱등 기록(`ServiceIdempotency`)을 넣어 결과를 가려야 한다.
 *          - 키는 ASCII(`[0-9a-z_./-]`, 256 B 이하)이고 순서는 바이트 사전순이다. 숫자는 `ServiceKeyUtil::appendHex64`(고정 16자)로 써야 순서가 맞는다.
 *            테이블 이름은 `hashed_string` 이라 대소문자를 가리지 않으므로 소문자로만 쓴다.
 *          - 비동기: 서비스는 저장 왕복 하나를 `ServiceStoreJob` 로 묶어 `submit` 한다.
 *            `run` 은 저장소 스레드에서 돌고(동기 연결 API), `complete` 는 맡긴 스레드가 `pollCompletions` 를 부를 때 돈다.
 *            게임 스레드와 네트워크 스레드는 DB 를 기다리지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogContext.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

namespace sw
{
    class ServiceReadSetConnection;

    /** @brief 저장소 호출의 결과입니다. */
    enum class ServiceStoreResult : uint8
    {
        Ok = 0,
        NotFound,    ///< 읽기에서 키가 없다
        Conflict,    ///< 커밋에서 기대 버전이 어긋났다(아무것도 쓰지 않았다). SQL 의 직렬화 실패도 이것이다(다시 읽고 다시 하면 된다)
        Unavailable, ///< 저장소에 닿지 못했다. 커밋이라면 적용됐는지 모른다
        Invalid      ///< 빈 테이블이나 키, 키 규칙이나 상한 위반, 한 트랜잭션에 같은 키 둘
    };
} // namespace sw

namespace sw
{
    /** @brief 레코드 하나입니다. */
    struct ServiceRecord
    {
        static constexpr uint64 kAnyVersion    = ~0ull; ///< 쓰기 조건 없음
        static constexpr uint64 kAbsentVersion = 0;     ///< "없어야 한다" 조건이다. 읽은 버전이 이 값이면 레코드가 없다는 뜻이다

        vector<uint8> _bytes{};
        string        _key{}; ///< `listRecords` 만 채운다
        uint64        _version{ kAbsentVersion };
    };
} // namespace sw

namespace sw
{
    /** @brief 트랜잭션의 쓰기 하나입니다. 조건일 수도 있습니다. */
    struct ServiceWrite
    {
        enum class Kind : uint8
        {
            Put = 0,
            Erase,
            Require,     ///< 쓰지 않고 판만 본다
            RequireRange ///< 쓰지 않고 목록 읽기 하나(`_key` 가 접두어, `_range`)가 지금도 같은 결과인지 본다 — 범위의 판이다
        };

        /** @brief 범위 조건(`Kind::RequireRange`)의 목록 읽기 인자와 그때 본 결과의 지문입니다. */
        struct Range
        {
            string _cursorKey{};
            uint64 _fingerprint{ 0 }; ///< `ServiceTransaction::computeRangeFingerprint` — 읽은 레코드의 키 · 버전 순서열
            int32  _maxCount{ 0 };
            uint8  _bDescending{ SW_FALSE };
        };

        vector<uint8> _bytes{};
        string        _key{};
        hashed_string _table{};
        Range         _range{};
        uint64        _expectedVersion{ ServiceRecord::kAnyVersion };
        Kind          _kind{ Kind::Put };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceTransaction
     * @brief 한 번에 적용할 쓰기와 조건의 그룹(트랜잭션)입니다.
     */
    class SW_GF_API ServiceTransaction
    {
    public:
        /** @brief 한 트랜잭션의 쓰기(put · erase) 상한입니다. 버전 조건(`requireVersion`)은 세지 않는다 — 읽기 집합 연결(`ServiceReadSetConnection`)이 읽은 만큼 덧붙이므로, 함께 세면 쓰기를 상한까지 짠 일(정산 페이지 · 죽은 신청 지우기)이 커밋에서 Invalid 였다. */
        static constexpr int32 kMaxWriteCount = 64;
        /** @brief 한 트랜잭션의 버전 조건 상한입니다(부르는 쪽이 건 것 + 읽기 집합이 덧붙인 것). */
        static constexpr int32 kMaxConditionCount = 256;

        ServiceTransaction();

        void put( const hashed_string& table, string_view key, vector<uint8> bytes, uint64 expectedVersion = ServiceRecord::kAnyVersion );
        void erase( const hashed_string& table, string_view key, uint64 expectedVersion = ServiceRecord::kAnyVersion );
        void requireVersion( const hashed_string& table, string_view key, uint64 expectedVersion );
        /**
         * @brief 목록 읽기 하나(인자 그대로)가 커밋 때도 지문 @p fingerprint 의 결과(키 · 버전 순서열)를 내야 한다는 조건입니다. 그사이 범위에 레코드가 들거나 빠지거나 바뀌면 Conflict 다.
         * @details 저장소는 같은 트랜잭션 안에서 범위를 다시 읽어 견준다(이 트랜잭션의 쓰기 전에). 버전 조건 상한(`kMaxConditionCount`)에 든다.
         */
        void requireRange( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending, uint64 fingerprint );
        /** @brief 쓰기 하나를 그대로 덧붙입니다(다른 트랜잭션에서 짠 쓰기를 옮길 때). */
        void addWrite( const ServiceWrite& write ) { _listWrite.push_back( write ); }
        void clear() { _listWrite.clear(); }

        bool                        isEmpty() const { return _listWrite.empty(); }
        const vector<ServiceWrite>& getWrites() const { return _listWrite; }
        /** @brief 트랜잭션이 올바른지 검사합니다. 비었거나, 상한을 넘거나, 같은 키가 둘이거나, 키 규칙을 어기면 false 입니다. 구현이 커밋 전에 부르며 결과는 `Invalid` 입니다. */
        bool isWellFormed() const;

        /** @brief 키가 키 규칙(`[0-9a-z_./-]`, 1..256 B)을 지키면 true 입니다. */
        static bool isValidKey( string_view key );
        /** @brief 목록 읽기 결과 @p listRecord 의 @p first 번부터 끝까지의 지문(개수 · 키 · 버전)입니다. 범위 조건을 거는 쪽과 견주는 저장소가 같은 함수를 쓴다. */
        static uint64 computeRangeFingerprint( const vector<ServiceRecord>& listRecord, size_t first );

    private:
        vector<ServiceWrite> _listWrite;
    };
} // namespace sw

namespace sw
{
    /** @brief 커밋 결과를 담는 구조체입니다. */
    struct ServiceCommitInfo
    {
        uint64 _commitVersion{ 0 };                               ///< Ok 일 때 이 커밋이 쓴 레코드들의 새 버전
        int32  _conflictIndex{ constant::invalid_index::kInt32 }; ///< Conflict 일 때 어긋난 쓰기 번호(`getWrites` 순서). 알 수 없으면 -1 이다(SQL 직렬화 실패)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServiceStoreConnection
     * @brief 저장소 스레드에서 `ServiceStoreJob::run` 이 받는 동기 연결입니다.
     * @details 그 호출 동안만 유효하므로 보관하면 안 된다.
     */
    class SW_GF_API IServiceStoreConnection
    {
    public:
        static constexpr int32 kMaxRecordSize = 64 * 1024;

        IServiceStoreConnection()          = default;
        virtual ~IServiceStoreConnection() = default;

        IServiceStoreConnection( const IServiceStoreConnection& )            = delete;
        IServiceStoreConnection& operator=( const IServiceStoreConnection& ) = delete;

        /** @brief 레코드 하나를 읽습니다. 없으면 NotFound 를 반환하고, @p outRecord 는 비며 버전은 `kAbsentVersion` 입니다. */
        [[nodiscard]] virtual ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) = 0;
        /** @brief 트랜잭션을 모두 적용하거나 아무것도 적용하지 않습니다. */
        [[nodiscard]] virtual ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) = 0;

    protected:
        // 목록 읽기는 `ServiceRecordReader` 만 부른다 — 결과에 다음 쪽 · 잘림을 싣지 않는 한 쪽 읽기를 서비스 코드가 직접 부르지 못하게 한다(감사 O-18). 구현(메모리 · SQL)은 겹쳐 쓴다.
        friend struct ServiceRecordReader;

        /**
         * @brief @p keyPrefix 로 시작하는 키를 키 순서로 @p outListRecord 뒤에 붙입니다. 키, 바이트, 버전을 담습니다.
         * @param cursorKey 비어 있지 않으면 이 키 다음부터(오름차순) 또는 이전부터(내림차순) 읽는다. 이 키 자체는 뺀다.
         * @param maxCount 1 이상
         * @param bDescending true 면 큰 키부터("마지막 N 개")
         */
        [[nodiscard]] virtual ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                              vector<ServiceRecord>& outListRecord ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServiceStoreCompletion
     * @brief 서비스의 저장소 완료 함수(`complete` 가 부르는 `apply*` · `complete*`)가 일의 결과와 함께 받는 끝 상태입니다.
     * @details 저장소 실패를 로그로 내는 완료 함수는 이것을 받아 `isCancelled` 가 false 일 때만 경고 · 오류를 낸다. 종료 때문에 취소된 일은 고장이 아니기 때문이다.
     *          `bool` 둘(`bReadOk`, `bCancelled`)이 나란히 오면 뒤바뀌어도 컴파일되므로 이름 있는 형식으로 받는다.
     *          이 형식을 받는 함수(와 `ServiceStoreJob` 의 `complete`)는 로그를 내면서 `isCancelled` 를 한 번도 묻지 않으면 안 된다.
     */
    struct ServiceStoreCompletion
    {
        bool _bCancelled{ false };

        bool isCancelled() const { return _bCancelled; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServiceStoreCommitStats
     * @brief 저장소 앞 하나가 받은 일들의 커밋 수와 그 가운데 버전 충돌(`Conflict`) 수입니다. 충돌은 서비스가 다시 읽고 다시 하므로 충돌 / 커밋이 재시도율이다.
     * @details 일의 입구(`ServiceStoreJob::execute`)가 거는 읽기 집합 포장만 센다 — 저장 함수 안에서 겹쳐 둔 포장은 세지 않아 한 커밋을 두 번 세지 않는다.
     *          저장소 스레드 여럿(SQL 풀)이 함께 올리므로 원자 값이다. 부하 봇이 끝에 읽어 보고서에 낸다(감사 O-12). 엔진 비교: PostgreSQL `pg_stat_database.xact_rollback` 과 같은 자리의 수.
     */
    struct ServiceStoreCommitStats
    {
        atomic<int64> _commitCount{ 0 };   ///< 커밋 시도 수
        atomic<int64> _conflictCount{ 0 }; ///< 그 가운데 `Conflict` 로 끝난 수
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 일의 종류입니다. 저장소 구현이 `run` 에 넘길 연결을 이것으로 고른다.
     * @details 스냅샷 읽기는 일 하나의 모든 읽기가 한 시점의 상태를 본다 — 테이블 전체를 여러 쪽으로 훑는 검사(원장 감사)가 그사이 끼는 다른 서버의 커밋 때문에
     *          앞쪽(분개)과 뒤쪽(잔액)을 다른 시점으로 읽어 거짓 불균형을 보지 않게 한다. PostgreSQL `REPEATABLE READ READ ONLY` 트랜잭션, SQLite WAL 의 읽기 트랜잭션,
     *          메모리 저장소는 한 잠금 구간에서 뜬 사본이다. 스냅샷 읽기의 커밋은 `Invalid` 다(읽기 전용). 언리얼 · 유니티 · Godot 엔 서버 저장소가 없고, Nakama 의 저장소 읽기도 DB 격리를 그대로 쓴다.
     */
    enum class ServiceStoreJobKind : uint8
    {
        ReadWrite = 0, ///< 읽고 커밋한다 — 읽기는 호출마다 그때의 상태다(조건은 커밋이 본다)
        SnapshotRead   ///< 읽기만 한다 — 모든 읽기가 일이 시작한 시점의 상태다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceStoreJob
     * @brief 저장 왕복 하나입니다.
     * @details 입력은 맡기기 전에 이 객체에 복사해 둔다.
     *          `run` 은 저장소 스레드에서 정확히 한 번 불린다. 저장소를 종료하는 중이면 모든 호출이 Unavailable 인 연결로 불린다.
     *          그 뒤 `complete` 가 맡긴 쪽의 `pollCompletions` 에서 정확히 한 번 불린다.
     *          `run` 은 서비스 멤버를 만지지 않고, `complete` 는 저장소를 부르지 않는다. 필요하면 새 일을 맡긴다.
     *          저장소는 `submit` 을 부른 스레드의 로그 컨텍스트(요청 추적 id 와 주체)를 저장해 두었다가 `run` 과 `complete` 동안 다시 설정한다.
     *          그래서 저장소 스레드의 로그 줄에도 같은 요청 꼬리표가 붙는다.
     */
    class SW_GF_API ServiceStoreJob
    {
    public:
        ServiceStoreJob() = default;
        explicit ServiceStoreJob( ServiceStoreJobKind kind )
            : _kind{ kind }
        {
        }
        virtual ~ServiceStoreJob() = default;

        ServiceStoreJob( const ServiceStoreJob& )            = delete;
        ServiceStoreJob& operator=( const ServiceStoreJob& ) = delete;

        /**
         * @brief 저장소 구현이 부르는 입구입니다. 연결을 읽기 집합 연결(`ServiceReadSetConnection`)로 감싸 `run` 에 넘기고, 그 커밋을 @p inoutStats(저장소 앞의 것)에 셉니다.
         * @details 일은 감싸지 않은 연결을 받을 길이 없다 — 판단에 쓴 읽기는 모두 다음 커밋의 버전 조건이 된다(손으로 `requireVersion` 을 빠뜨리는 모양을 막는다, 감사 "다음 할 일" 5 번).
         *          연결은 커밋마다 읽은 것을 비우므로 재시도는 판단에 쓸 레코드를 시도 안에서 다시 읽는다.
         */
        void         execute( IServiceStoreConnection& connection, ServiceStoreCommitStats& inoutStats );
        virtual void complete() = 0;

        /** @brief 맡긴 스레드의 로그 문맥입니다. 저장소 구현이 `submit` 에서 잡습니다. */
        void              bindLogContext( const LogContext& context ) { _logContext = context; }
        const LogContext& getLogContext() const { return _logContext; }

        /**
         * @brief 저장소가 종료 중이라 이 일을 실행하지 않고 닫힌 연결로 끝냈음을 표시합니다(저장소 구현이 `run` 앞에서 부릅니다).
         * @details 닫힌 연결의 결과(Unavailable)는 쓰지 않았다는 사실 그대로지만, 진짜 저장소 고장과 같은 값이라 `complete` 가 둘을 가르지 못했다.
         *          종료 때문에 취소된 일은 오류가 아니므로 `complete` 가 `isCancelled` 로 가르고 경고를 남기지 않는다. 결과 열거를 늘리면 76 개 파일의 호출처가
         *          저마다 다시 판단해야 해서, 취소를 결과 값이 아니라 일의 상태로 둔다.
         */
        void markCancelled() { _bCancelled = true; }
        /** @brief 종료 중이라 취소됐는지입니다. `complete` 에서 읽습니다(저장소 스레드의 표시는 완료 큐의 락을 거쳐 보인다). */
        bool isCancelled() const { return _bCancelled; }
        /** @brief 스냅샷 읽기 일인지입니다. 저장소 구현은 그렇다면 한 시점의 읽기 전용 연결로 `execute` 한다(`ServiceStoreJobKind`). */
        bool isSnapshotRead() const { return _kind == ServiceStoreJobKind::SnapshotRead; }
        /** @brief 서비스의 완료 함수에 넘기는 끝 상태입니다. `complete` 에서 부릅니다. */
        ServiceStoreCompletion getCompletion() const { return ServiceStoreCompletion{ _bCancelled }; }

    private:
        /** @brief 저장소 스레드에서 정확히 한 번 불립니다(`execute` 만 부른다). */
        virtual void run( ServiceReadSetConnection& connection ) = 0;

    private:
        LogContext          _logContext{};
        ServiceStoreJobKind _kind{ ServiceStoreJobKind::ReadWrite };
        bool                _bCancelled{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceStore
     * @brief 저장소의 앞(front)입니다. 일을 맡고(`submit`, 아무 스레드) 끝난 일을 거둡니다(`pollCompletions`, 맡긴 서비스 스레드 하나).
     * @details 서버 프로세스 하나에 앞 하나가 있다. 프로세스 둘이 같은 DB 를 쓰면 앞이 둘이다.
     *          구현으로 `MemoryServiceStore`(테스트와 개발)와 SQL 구현(GF_Server_SQLStore)이 있다.
     */
    class ServiceStore
    {
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kBase; ///< 런타임의 공통 기반이 키트 범위에 건다. 게임 모듈 묶음(`ServerKitDependencies`)의 의존 이름이 여기서 나온다

        ServiceStore()          = default;
        virtual ~ServiceStore() = default;

        ServiceStore( const ServiceStore& )            = delete;
        ServiceStore& operator=( const ServiceStore& ) = delete;

        virtual void submit( unique_ptr<ServiceStoreJob> job ) = 0;
        /** @brief 끝난 일의 `complete` 를 이 스레드에서 부르고, 부른 수를 반환합니다. */
        virtual int32 pollCompletions() = 0;
        /** @brief 받았지만 아직 회수하지 않은 작업의 수를 반환합니다. */
        virtual int32 getPendingCount() const = 0;
        /** @brief 끝난 일이 생기거나(또는 남은 일이 없거나) @p deadline 이 지날 때까지 기다린 뒤 `pollCompletions` 합니다. `submit` 안에서 바로 실행하는 구현은 기다릴 것이 없다. */
        virtual int32 waitCompletions( const Deadline& deadline )
        {
            (void)deadline;
            return pollCompletions();
        }
        /** @brief 받은 일(완료가 맡긴 새 일까지)을 모두 실행하고 거둡니다 — 멈추기 전에 부른다. @p deadline 까지 남은 일이 있으면 false 입니다. */
        [[nodiscard]] bool drain( const Deadline& deadline )
        {
            while ( getPendingCount() > 0 )
            {
                if ( deadline.isExpired() )
                    return false;
                (void)waitCompletions( deadline );
            }
            return true;
        }
        /** @brief 새 일을 받지 않고, 돌지 못한 일은 닫힌 연결로 돌려 완료 큐에 넣습니다(그 쓰기는 사라진다 — 먼저 `drain` 한다). 그 뒤 `pollCompletions` 한 번으로 모두 거둘 수 있습니다. */
        virtual void shutdown() = 0;
        /** @brief 멈추기 전에 남은 일을 기다리는 최대 시간(밀리초)입니다 — 저장소에 닿지 못하는 종료가 끝없이 서지 않게 한다. */
        static constexpr int64 kStopDrainMilliseconds = 10000;

        /** @brief 이 앞이 받은 일들의 커밋 · 충돌 수입니다(아무 스레드에서 읽는다). */
        const ServiceStoreCommitStats& getCommitStats() const { return _commitStats; }

    protected:
        ServiceStoreCommitStats _commitStats{}; ///< 구현이 일마다 `execute` 에 넘긴다
    };
} // namespace sw
