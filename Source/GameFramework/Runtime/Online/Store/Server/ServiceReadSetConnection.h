/**
 * @file ServiceReadSetConnection.h
 * @brief 시도 하나에서 읽은 레코드를 다음 커밋의 버전 조건으로 붙이는 연결 포장입니다(낙관적 동시성의 읽기 집합 검증).
 * @details 저장 함수가 "읽고 판단하고 쓴다" 할 때 판단에 쓴 레코드(권한 등급 · 멤버 역할 · 캠페인 활성)를 손으로 `requireVersion` 하지 않으면, 읽기와 커밋 사이에 그 레코드가
 *          바뀌어도 커밋이 통과한다(강등된 운영자 · 임원의 명령, 끝난 캠페인의 수령 — 감사 "다음 할 일" 5 번). 이 포장은 그 조건을 형식으로 붙인다.
 *          - `readRecord` 가 돌려준 버전(없으면 `kAbsentVersion`)을 표 · 키마다 기억한다. 같은 키를 다시 읽으면 새 버전으로 바꾼다.
 *          - `commit` 은 트랜잭션 뒤에 기억한 키마다 `requireVersion` 을 붙인다. 트랜잭션이 이미 그 키를 쓰면 조건을 덧붙이지 않고, 그 쓰기가 조건 없음(`kAnyVersion`)이면
 *            읽은 버전으로 바꾼다(읽고 덮어쓰는 갱신이 남의 커밋을 지우지 않게). 어긋나면 저장소가 `Conflict` 를 돌려주고 부르는 쪽이 다시 읽는다.
 *          - 커밋 뒤에는(결과와 상관없이) 읽은 것을 비운다. 그래서 한 시도(읽기 → 커밋 한 번)마다 새 포장을 두거나, 다시 시도할 때 판단에 쓴 레코드를 다시 읽는다.
 *          - `listRecords` 는 기억하지 않는다. shortcut: 범위 읽기의 "그사이 생긴 레코드"(팬텀)는 막지 못한다 — 목록으로 판단하는 저장 함수가 생기면 범위 판(색인 레코드 버전)을 둔다.
 *          Nakama 저장소 객체 버전 · EF Core 동시성 토큰과 같은 자리이고, 언리얼 · 유니티는 서버 저장소를 두지 않는다. 기억한 조건을 붙여 상한(`ServiceTransaction::kMaxWriteCount`)을
 *          넘으면 저장소가 `Invalid` 를 돌려준다 — 조건을 빼고 쓰지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    /**
     * @class ServiceReadSetConnection
     * @brief 읽은 레코드를 다음 커밋의 조건으로 붙이는 연결입니다. 저장소 스레드의 시도 하나 동안 스택에 두고 안쪽 연결보다 먼저 사라집니다.
     */
    class SW_GF_API ServiceReadSetConnection final : public IServiceStoreConnection
    {
    public:
        explicit ServiceReadSetConnection( IServiceStoreConnection& inner );

        [[nodiscard]] ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override;
        [[nodiscard]] ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                      vector<ServiceRecord>& outListRecord ) override;
        [[nodiscard]] ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override;

    private:
        /** @brief 읽은 레코드 하나의 표 · 키 · 버전입니다. */
        struct ReadEntry
        {
            hashed_string _table{};
            string        _key{};
            uint64        _version{ ServiceRecord::kAbsentVersion };
        };

        IServiceStoreConnection& _inner;
        vector<ReadEntry>        _listRead;
    };
} // namespace sw
