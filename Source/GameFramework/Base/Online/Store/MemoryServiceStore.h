/**
 * @file MemoryServiceStore.h
 * @brief 메모리 서비스 저장소입니다. 테스트와 한 프로세스 개발 서버에 씁니다.
 *        데이터(`MemoryServiceDatabase`, 실패 주입 포함)와 프런트(`MemoryServiceStore`, 완료 큐)가 나뉩니다.
 * @details - 작업은 `submit` 한 스레드에서 바로 `run` 하고 완료는 큐에 쌓는다. 그래서 결정적이다(SQL 구현은 풀 워커에서 돌고, 계약 테스트는 둘을 같은 결과로 본다).
 *          - "서버 재시작" 테스트는 데이터를 남기고 프런트와 서비스를 새로 만든다. "서버 둘" 테스트는 프런트 둘이 데이터 하나를 쓴다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/map.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 주입할 실패입니다. */
    enum class ServiceStoreFault : uint8
    {
        None = 0,
        RejectCommit,    ///< 적용하지 않고 Unavailable
        LoseCommitReply, ///< 적용하고 Unavailable 을 돌려준다. 응답을 잃은 것과 같다
        RejectRead       ///< 읽기 · 목록이 Unavailable
    };
} // namespace sw

namespace sw
{
    /**
     * @class MemoryServiceDatabase
     * @brief 테이블마다 정렬 맵(키별 바이트와 버전)입니다. 잠금 하나로 모든 호출을 직렬화합니다(프런트 여럿이 같이 쓴다). 그 자체가 동기 연결입니다.
     */
    class SW_GF_API MemoryServiceDatabase final : public IServiceStoreConnection
    {
    public:
        MemoryServiceDatabase();

        [[nodiscard]] ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override;
        [[nodiscard]] ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                      vector<ServiceRecord>& outListRecord ) override;
        [[nodiscard]] ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override;

        /** @brief 해당 종류의 호출을 @p skipCount 번 흘려보낸 뒤 한 번 @p fault 를 일으킵니다(테스트). None 이면 설정해 둔 것을 해제합니다. */
        void   armFault( ServiceStoreFault fault, int32 skipCount = 0 );
        bool   isFaultArmed() const;
        uint64 getCommitCount() const;
        int32  countRecords( const hashed_string& table ) const;
        /** @brief 모든 테이블, 키, 바이트를 정해진 순서로 해시합니다(버전은 뺀다). 실패 주입 전후 "아무것도 바뀌지 않았다" 를 확인합니다. */
        uint64 computeContentHash() const;

    private:
        struct Entry
        {
            vector<uint8> _bytes{};
            uint64        _version{ 0 };
        };
        using Table = map<string, Entry>;

        /** @brief 잠금 안에서 부릅니다. 설정해 둔 실패가 @p fault 이고 차례가 됐으면 해제하고 true 입니다. */
        bool               consumeFault( ServiceStoreFault fault ) SW_REQUIRES_LOCK( _mutex );
        const Entry*       findEntry( const hashed_string& table, string_view key ) const;
        ServiceStoreResult validate( const ServiceTransaction& transaction, ServiceCommitInfo& outInfo ) const;

        mutable mutex                       _mutex;
        unordered_map<hashed_string, Table> _mapTable;
        uint64 _commitVersion               SW_GUARDED_BY( _mutex );
        uint64 _commitCount                 SW_GUARDED_BY( _mutex );
        int32 _faultSkipCount               SW_GUARDED_BY( _mutex );
        ServiceStoreFault _armedFault       SW_GUARDED_BY( _mutex );
    };
} // namespace sw

namespace sw
{
    /**
     * @class MemoryServiceStore
     * @brief 메모리 데이터의 프런트입니다. `submit` 이 즉시 `run` 하고 완료는 `pollCompletions` 까지 쌓는다.
     */
    class SW_GF_API MemoryServiceStore final : public IServiceStore
    {
    public:
        /** @brief @p pDatabase 는 빌려 쓴다(앞보다 오래 산다). */
        explicit MemoryServiceStore( MemoryServiceDatabase* pDatabase );
        ~MemoryServiceStore() override;

        void  submit( unique_ptr<IServiceStoreWork> work ) override;
        int32 pollCompletions() override;
        int32 getPendingCount() const override;
        void  shutdown() override;

        MemoryServiceDatabase& getDatabase() { return *_pDatabase; }

    private:
        mutable mutex                                        _mutex; ///< 완료 큐를 보호한다. `submit` 은 아무 스레드에서나 부른다
        vector<unique_ptr<IServiceStoreWork>> _listCompleted SW_GUARDED_BY( _mutex );
        MemoryServiceDatabase*                               _pDatabase;
        uint8 _bShutdown                                     SW_GUARDED_BY( _mutex );
    };
} // namespace sw
