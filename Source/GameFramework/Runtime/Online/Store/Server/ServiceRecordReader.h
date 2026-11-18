/**
 * @file ServiceRecordReader.h
 * @brief 저장소 레코드 목록 읽기의 유일한 입구입니다. 한 쪽 읽기는 "다음 쪽이 있을 수 있다" 를, 다 읽기는 "상한에서 잘렸다" 를 결과에 반드시 싣습니다.
 * @details - `IServiceStoreConnection::listRecords` 는 보호 멤버라 이 형식만 부른다(접근 제어) — 한 쪽만 읽고 그 뒤를 조용히 버리는 모양(감사 O-18: 우편함 모두 받기 ·
 *            점검 표 · 라이브 이벤트 · 공지 · 길드 창고)을 형식으로 막는다. 결과 형식은 `[[nodiscard]]` 라 받지 않으면 컴파일 경고(오류)이다.
 *          - `readPage`: 커서로 직접 넘기는 쪽 읽기. 쪽이 가득 찼으면 `_nextCursor` 가 그 쪽의 마지막 키다(비면 끝).
 *          - `readAll`: 처음부터 끝까지 쪽으로 넘기며 읽는다. `maxTotal` 에 닿으면 `_bTruncated` 를 켠다 — 부른 쪽이 그 뜻(오류 · 이전 상태 유지 · 다음에 이어 읽기)을 정한다.
 *          - `visitAll`: 상한 없이 끝까지 쪽으로 넘기며 레코드마다 방문한다. 전체를 봐야 맞는 일(해산 · 정산 · 이전 · 삭제)은 이것을 쓴다 — 한 쪽만 읽고 나머지를 두는 모양이 결과를 틀리게 한다.
 *          언리얼 · 유니티 백엔드 SDK 의 페이지 토큰(PlayFab `ContinuationToken` · Nakama `cursor`)과 같은 계약이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    /** @brief 한 쪽 읽기의 결과입니다. */
    struct [[nodiscard]] ServiceRecordPage
    {
        string             _nextCursor{}; ///< 쪽이 가득 찼으면 그 쪽의 마지막 키(다음 쪽의 커서)이다. 비면 더 없다
        ServiceStoreResult _result{ ServiceStoreResult::Unavailable };

        [[nodiscard]] bool isOk() const { return _result == ServiceStoreResult::Ok; }
        [[nodiscard]] bool hasMore() const { return _nextCursor.empty() == false; }
    };

    /** @brief 다 읽기의 결과입니다. */
    struct [[nodiscard]] ServiceRecordRead
    {
        ServiceStoreResult _result{ ServiceStoreResult::Unavailable };
        uint8              _bTruncated{ SW_FALSE }; ///< `maxTotal` 에 닿아 뒤를 읽지 않았다

        [[nodiscard]] bool isOk() const { return _result == ServiceStoreResult::Ok; }
    };

    /**
     * @struct ServiceRecordReader
     * @brief 레코드 목록 읽기입니다. 저장소 스레드에서 부릅니다.
     */
    struct ServiceRecordReader
    {
        /** @brief @p keyPrefix 의 키를 @p cursorKey 다음부터 @p maxCount 개까지 @p outListRecord 뒤에 붙입니다(`IServiceStoreConnection::listRecords` 와 같은 뜻). */
        static ServiceRecordPage readPage( IServiceStoreConnection& connection, const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount,
                                           bool bDescending, vector<ServiceRecord>& outListRecord )
        {
            ServiceRecordPage page;
            const size_t      sizeBefore = outListRecord.size();
            page._result                 = connection.listRecords( table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord );
            const size_t readCount       = outListRecord.size() - sizeBefore;
            if ( page.isOk() && maxCount > 0 && readCount == static_cast<size_t>( maxCount ) )
                page._nextCursor = outListRecord.back()._key;
            return page;
        }

        /**
         * @brief @p keyPrefix 의 키를 처음부터 끝까지 @p pageSize 씩 넘기며 레코드마다 @p visitor(`bool( const ServiceRecord& )`)를 부릅니다. 상한이 없습니다.
         * @details @p visitor 가 false 면 거기서 멈추고 Ok 다(부른 쪽이 자기 오류를 따로 들고 멈출 때도 쓴다). 읽기가 실패하면 그 결과다.
         */
        template <typename Visitor>
        [[nodiscard]] static ServiceStoreResult visitAll( IServiceStoreConnection& connection, const hashed_string& table, string_view keyPrefix, int32 pageSize, Visitor&& visitor )
        {
            string cursorKey;
            for ( ;; )
            {
                vector<ServiceRecord>   listRecord;
                const ServiceRecordPage page = readPage( connection, table, keyPrefix, cursorKey, pageSize, false, listRecord );
                if ( page.isOk() == false )
                    return page._result;
                for ( const ServiceRecord& record : listRecord )
                {
                    if ( visitor( record ) == false )
                        return ServiceStoreResult::Ok;
                }
                if ( page.hasMore() == false )
                    return ServiceStoreResult::Ok;
                cursorKey = page._nextCursor;
            }
        }

        /**
         * @brief @p keyPrefix 의 키를 처음부터 @p pageSize 씩 넘기며 @p maxTotal 개까지 @p outListRecord 뒤에 붙입니다. 더 남았으면 `_bTruncated` 입니다.
         * @details 실패하면 그때까지 붙인 것은 남고 결과가 실패다 — 부른 쪽이 반쯤 읽은 목록을 쓰지 않는다.
         */
        static ServiceRecordRead readAll( IServiceStoreConnection& connection, const hashed_string& table, string_view keyPrefix, int32 pageSize, int32 maxTotal, bool bDescending,
                                          vector<ServiceRecord>& outListRecord )
        {
            ServiceRecordRead read;
            string            cursorKey;
            const size_t      sizeStart  = outListRecord.size();
            int32             totalCount = 0;
            for ( ;; )
            {
                const int32             remaining = maxTotal - totalCount;
                const int32             request   = remaining < pageSize ? remaining : pageSize;
                const ServiceRecordPage page      = readPage( connection, table, keyPrefix, cursorKey, request + ( remaining <= pageSize ? 1 : 0 ), bDescending, outListRecord );
                read._result                      = page._result;
                if ( page.isOk() == false )
                    return read;
                const int32 pageCount = static_cast<int32>( outListRecord.size() - sizeStart ) - totalCount;
                if ( remaining <= pageSize )
                {
                    // 마지막 쪽은 하나 더 읽어 남았는지 본다 — 남았으면 그 하나를 빼고 잘림을 싣는다
                    if ( pageCount > remaining )
                    {
                        outListRecord.pop_back();
                        read._bTruncated = SW_TRUE;
                    }
                    return read;
                }
                totalCount += pageCount;
                if ( page.hasMore() == false )
                    return read;
                cursorKey = page._nextCursor;
            }
        }
    };
} // namespace sw
