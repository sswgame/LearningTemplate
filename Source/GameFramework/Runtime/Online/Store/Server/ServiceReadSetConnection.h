/**
 * @file ServiceReadSetConnection.h
 * @brief 시도 하나에서 읽은 레코드를 다음 커밋의 버전 조건으로 붙이는 연결 포장입니다(낙관적 동시성의 읽기 집합 검증).
 * @details 저장 함수가 "읽고 판단하고 쓴다" 할 때 판단에 쓴 레코드(권한 등급 · 멤버 역할 · 캠페인 활성)를 손으로 `requireVersion` 하지 않으면, 읽기와 커밋 사이에 그 레코드가
 *          바뀌어도 커밋이 통과한다(강등된 운영자 · 임원의 명령, 끝난 캠페인의 수령 — 감사 "다음 할 일" 5 번). 이 포장은 그 조건을 형식으로 붙인다.
 *          - `readRecord` 가 돌려준 버전(없으면 `kAbsentVersion`)을 표 · 키마다 기억한다. 같은 키를 다시 읽으면 새 버전으로 바꾼다.
 *          - `commit` 은 트랜잭션 뒤에 기억한 키마다 `requireVersion` 을 붙인다. 트랜잭션이 이미 그 키를 쓰면 조건을 덧붙이지 않고, 그 쓰기가 조건 없음(`kAnyVersion`)이면
 *            읽은 버전으로 바꾼다(읽고 덮어쓰는 갱신이 남의 커밋을 지우지 않게). 어긋나면 저장소가 `Conflict` 를 돌려주고 부르는 쪽이 다시 읽는다.
 *          - 커밋 뒤에는(결과와 상관없이) 읽은 것(키 · 범위)을 비운다. 그래서 한 시도(읽기 → 커밋 한 번)마다 새 포장을 두거나, 다시 시도할 때 판단에 쓴 레코드를 다시 읽는다.
 *          - `listRecords` 도 인자(표 · 접두어 · 커서 · 개수 · 방향)와 결과 지문(키 · 버전 순서열)을 기억해 커밋에 범위 조건(`requireRange`)으로 건다. 저장소는 같은 트랜잭션 안에서
 *            범위를 다시 읽어 견주므로, 그사이 범위에 레코드가 들거나(팬텀) 빠지거나 바뀌면 Conflict 다 — 목록 · 개수로 판단하는 저장 함수(신청 수 · 허용 수 · 친구 수)가 부모 레코드를
 *            손으로 다시 쓰지 않아도 막힌다. 직렬화 가능 격리의 술어 잠금 자리이고, DB 의 next-key(간격) 잠금과 달리 잠그지 않고 커밋 때 다시 훑어 견준다(Hekaton 의 스캔 재검증).
 *            쪽 읽기라 본 쪽 밖(개수를 채운 뒤)의 변화는 조건이 아니다 — 같은 인자의 읽기가 같은 답을 내는지가 조건이다. 지문의 버전은 저장소가 매기므로 고른 키로 충돌을 만들 수 없다.
 *          Nakama 저장소 객체 버전 · EF Core 동시성 토큰 · HTTP 조건부 쓰기(ETag · If-Match)와 같은 낙관적 동시성 제어이고, 언리얼 · 유니티 · Godot 엔진엔 계정 서버가 없다(EOS · UGS 관리형). 덧붙인 조건은 쓰기 상한(`kMaxWriteCount`)에 들지 않고
 *          조건 상한(`ServiceTransaction::kMaxConditionCount`)에 든다 — 넘으면 저장소가 `Invalid` 를 돌려준다(조건을 빼고 쓰지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

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
        /** @brief @p pStats 가 있으면 커밋 · 충돌을 거기에 센다 — 일의 입구(`ServiceStoreJob::execute`)만 넘긴다(겹쳐 둔 포장은 null — 한 커밋을 두 번 세지 않는다). */
        explicit ServiceReadSetConnection( IServiceStoreConnection& inner, ServiceStoreCommitStats* pStats = nullptr );

        [[nodiscard]] ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override;
        [[nodiscard]] ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override;

    private:
        // 목록 읽기는 기반의 보호 입구(`ServiceRecordReader`)로만 온다 — 공개로 겹쳐 쓰면 저장 함수가 이 포장에서 한 쪽 읽기를 직접 부를 수 있다(감사 O-18).
        // 읽은 쪽(인자 · 결과 지문)을 기억해 다음 커밋에 범위 조건(`requireRange`)으로 건다
        [[nodiscard]] ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                      vector<ServiceRecord>& outListRecord ) override;

    private:
        /** @brief 읽은 레코드 하나의 표 · 키 · 버전입니다. */
        struct ReadEntry
        {
            hashed_string _table{};
            string        _key{};
            uint64        _version{ ServiceRecord::kAbsentVersion };
        };

        /** @brief 목록 읽기 하나의 인자와 그때 본 결과의 지문입니다. */
        struct RangeEntry
        {
            hashed_string _table{};
            string        _keyPrefix{};
            string        _cursorKey{};
            uint64        _fingerprint{ 0 };
            int32         _maxCount{ 0 };
            uint8         _bDescending{ SW_FALSE };
        };

        IServiceStoreConnection& _inner;
        vector<ReadEntry>        _listRead;
        vector<RangeEntry>       _listRangeRead;
        ServiceStoreCommitStats* _pStats;
    };
} // namespace sw
