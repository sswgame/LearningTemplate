/**
 * @file ServiceStore.h
 * @brief 서비스(계정, 경제, 거래)의 저장 계약을 정의합니다.
 * @details (테이블, 키)로 레코드(바이트와 버전)를 찾고, 조건부 쓰기를 트랜잭션으로 묶고, 키 범위를 읽는다.
 *          이 모든 것을 저장소 스레드에서 도는 "작업"으로 요청하는 비동기 인터페이스도 여기서 정의한다.
 *
 *          - 버전(`ServiceRecord::_version`)은 저장소 전체에서 커밋마다 오르는 수이다. 지웠다 다시 만든 키도 옛 버전을 다시 받지 않으므로 ABA 가 없다. 빈틈은 있어도 된다.
 *          - 쓰기는 모두 트랜잭션이다. 쓰기마다 기대 버전을 준다. `kAnyVersion` 은 조건 없음이고, `kAbsentVersion` 은 "없어야 한다"이다.
 *            하나라도 어긋나면 아무것도 쓰지 않고 `Conflict` 를 반환한다. `requireVersion` 은 쓰지 않고 버전만 본다. 읽은 값이 그새 바뀌지 않았는지 확인할 때 쓴다.
 *          - `Unavailable` 은 "적용됐는지 모른다"를 포함한다. 돈이나 아이템이 움직이는 커밋은 같은 트랜잭션에 멱등 기록(`ServiceIdempotency`)을 넣어 결과를 가려야 한다.
 *          - 키는 ASCII(`[0-9a-z_./-]`, 256 B 이하)이고 순서는 바이트 사전순이다. 숫자는 `ServiceKeyUtil::appendHex64`(고정 16자)로 써야 순서가 맞는다.
 *            테이블 이름은 `hashed_string` 이라 대소문자를 가리지 않으므로 소문자로만 쓴다.
 *          - 비동기: 서비스는 저장 왕복 하나를 `IServiceStoreWork` 로 묶어 `submit` 한다.
 *            `run` 은 저장소 스레드에서 돌고(동기 연결 API), `complete` 는 맡긴 스레드가 `pollCompletions` 를 부를 때 돈다.
 *            게임 스레드와 네트워크 스레드는 DB 를 기다리지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogContext.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
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
            Require ///< 쓰지 않고 판만 본다
        };

        vector<uint8> _bytes{};
        string        _key{};
        hashed_string _table{};
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
        static constexpr int32 kMaxWriteCount = 64;

        ServiceTransaction();

        void put( const hashed_string& table, string_view key, vector<uint8> bytes, uint64 expectedVersion = ServiceRecord::kAnyVersion );
        void erase( const hashed_string& table, string_view key, uint64 expectedVersion = ServiceRecord::kAnyVersion );
        void requireVersion( const hashed_string& table, string_view key, uint64 expectedVersion );
        void clear() { _listWrite.clear(); }

        bool                        isEmpty() const { return _listWrite.empty(); }
        const vector<ServiceWrite>& getWrites() const { return _listWrite; }
        /** @brief 트랜잭션이 올바른지 검사합니다. 비었거나, 상한을 넘거나, 같은 키가 둘이거나, 키 규칙을 어기면 false 입니다. 구현이 커밋 전에 부르며 결과는 `Invalid` 입니다. */
        bool isWellFormed() const;

        /** @brief 키가 키 규칙(`[0-9a-z_./-]`, 1..256 B)을 지키면 true 입니다. */
        static bool isValidKey( string_view key );

    private:
        vector<ServiceWrite> _listWrite;
    };
} // namespace sw

namespace sw
{
    /** @brief 커밋 결과를 담는 구조체입니다. */
    struct ServiceCommitInfo
    {
        uint64 _commitVersion{ 0 };  ///< Ok 일 때 이 커밋이 쓴 레코드들의 새 버전
        int32  _conflictIndex{ -1 }; ///< Conflict 일 때 어긋난 쓰기 번호(`getWrites` 순서). 알 수 없으면 -1 이다(SQL 직렬화 실패)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServiceStoreConnection
     * @brief 저장소 스레드에서 `IServiceStoreWork::run` 이 받는 동기 연결입니다.
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
        /**
         * @brief @p keyPrefix 로 시작하는 키를 키 순서로 @p outListRecord 뒤에 붙입니다. 키, 바이트, 버전을 담습니다.
         * @param cursorKey 비어 있지 않으면 이 키 다음부터(오름차순) 또는 이전부터(내림차순) 읽는다. 이 키 자체는 뺀다.
         * @param maxCount 1 이상
         * @param bDescending true 면 큰 키부터("마지막 N 개")
         */
        [[nodiscard]] virtual ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                              vector<ServiceRecord>& outListRecord ) = 0;
        /** @brief 트랜잭션을 모두 적용하거나 아무것도 적용하지 않습니다. */
        [[nodiscard]] virtual ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServiceStoreCompletion
     * @brief 서비스의 저장소 완료 함수(`complete` 가 부르는 `apply*` · `complete*`)가 일의 결과와 함께 받는 끝 상태입니다.
     * @details 저장소 실패를 로그로 내는 완료 함수는 이것을 받아 `isCancelled` 가 false 일 때만 경고 · 오류를 낸다. 종료 때문에 취소된 일은 고장이 아니기 때문이다.
     *          `bool` 둘(`bReadOk`, `bCancelled`)이 나란히 오면 뒤바뀌어도 컴파일되므로 이름 있는 형식으로 받는다.
     *          린트 `CheckStoreCompletionLog` 가 이 형식을 받는 함수(와 `IServiceStoreWork` 의 `complete`)가 로그를 내면서 `isCancelled` 를 한 번도 묻지 않는 것을 막는다.
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
     * @class IServiceStoreWork
     * @brief 저장 왕복 하나입니다.
     * @details 입력은 맡기기 전에 이 객체에 복사해 둔다.
     *          `run` 은 저장소 스레드에서 정확히 한 번 불린다. 저장소를 종료하는 중이면 모든 호출이 Unavailable 인 연결로 불린다.
     *          그 뒤 `complete` 가 맡긴 쪽의 `pollCompletions` 에서 정확히 한 번 불린다.
     *          `run` 은 서비스 멤버를 만지지 않고, `complete` 는 저장소를 부르지 않는다. 필요하면 새 일을 맡긴다.
     *          저장소는 `submit` 을 부른 스레드의 로그 컨텍스트(요청 추적 id 와 주체)를 저장해 두었다가 `run` 과 `complete` 동안 다시 설정한다.
     *          그래서 저장소 스레드의 로그 줄에도 같은 요청 꼬리표가 붙는다.
     */
    class SW_GF_API IServiceStoreWork
    {
    public:
        IServiceStoreWork()          = default;
        virtual ~IServiceStoreWork() = default;

        IServiceStoreWork( const IServiceStoreWork& )            = delete;
        IServiceStoreWork& operator=( const IServiceStoreWork& ) = delete;

        virtual void run( IServiceStoreConnection& connection ) = 0;
        virtual void complete()                                 = 0;

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
        /** @brief 서비스의 완료 함수에 넘기는 끝 상태입니다. `complete` 에서 부릅니다. */
        ServiceStoreCompletion getCompletion() const { return ServiceStoreCompletion{ _bCancelled }; }

    private:
        LogContext _logContext{};
        bool       _bCancelled{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServiceStore
     * @brief 저장소의 앞(front)입니다. 일을 맡고(`submit`, 아무 스레드) 끝난 일을 거둡니다(`pollCompletions`, 맡긴 서비스 스레드 하나).
     * @details 서버 프로세스 하나에 앞 하나가 있다. 프로세스 둘이 같은 DB 를 쓰면 앞이 둘이다.
     *          구현으로 `MemoryServiceStore`(테스트와 개발)와 SQL 구현(GF_Server_SQLStore)이 있다.
     */
    class IServiceStore
    {
    public:
        IServiceStore()          = default;
        virtual ~IServiceStore() = default;

        IServiceStore( const IServiceStore& )            = delete;
        IServiceStore& operator=( const IServiceStore& ) = delete;

        virtual void submit( unique_ptr<IServiceStoreWork> work ) = 0;
        /** @brief 끝난 일의 `complete` 를 이 스레드에서 부르고, 부른 수를 반환합니다. */
        virtual int32 pollCompletions() = 0;
        /** @brief 받았지만 아직 회수하지 않은 작업의 수를 반환합니다. */
        virtual int32 getPendingCount() const = 0;
        /** @brief 새 일을 받지 않고, 돌지 못한 일은 닫힌 연결로 돌려 완료 큐에 넣습니다. 그 뒤 `pollCompletions` 한 번으로 모두 거둘 수 있습니다. */
        virtual void shutdown() = 0;
    };
} // namespace sw
