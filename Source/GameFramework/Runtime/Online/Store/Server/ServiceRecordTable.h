/**
 * @file ServiceRecordTable.h
 * @brief 저장소 표 하나와 그 표가 싣는 값의 형식입니다. 서비스는 이 형식으로만 레코드 값을 쓰고 읽는다(바이트를 내주는 `put` · `_bytes` 는 비공개).
 * @details 표 종류는 넷이다.
 *          - `ServiceRecordTable<T>`: `REFLECT` 레코드 하나(`ServiceRecordUtil` — 판 머리, 자기 기술형 A, 모르는 칸 보존, 선언 범위 검사). 판은 레코드 타입의 `REFLECT( Version )`
 *            하나이고(출시 전이라 모두 0), 칸 추가 · 삭제는 판 없이 읽힌다. 뜻을 바꿀 때만 판을 올리고 `SW_TYPE_VERSION_PATCH` 로 옛 판을 올린다.
 *          - `ServiceIndexTable`: 값 없는 줄(키가 곧 내용 — TTL 색인 · 역방향 색인 · 차지 표시).
 *          - `ServiceLinkTable`: 다른 레코드의 id 하나(게스트 → 계정, 계정 → 세션). 0 과 깨진 값은 오류다.
 *          - `ServiceOpaqueTable`: 이 서버가 뜻을 읽지 않는 바이트(봉인된 비밀 · 저장해 둔 응답).
 *          표 하나에 레코드 형식 하나라 판을 여러 모양이 나눠 쓸 수 없고(옛 `FormatDef` 하나를 계정 · 프로필 · 세션이 나눠 썼다), 깨진 값은 `Corrupt`(오류 로그)라 없는 것으로 읽히지 않는다.
 *
 *          열거 칸은 열거자 **이름**(해시)으로 싣는다(`SerializerUtil::serializeValueBinary` — XML · JSON 과 같은 규칙). 정한 값으로 싣지 않는 까닭: 열거자를 끼우거나 차례를 바꿔도
 *          저장된 뜻이 그대로이고, 이름 바꾸기는 판 없이 `ENUM( ValueAlias = "Old:New" )` 로 옛 이름을 받는다. 값으로 실으면 끼우기 · 차례 바꾸기가 조용히 다른 뜻이 된다.
 *          지운 열거자를 든 레코드는 손상이다(`readDurable`) — 지울 때 판을 올린다.
 *          Nakama 저장소 객체(컬렉션마다 JSON 값) · PlayFab 의 키마다 형식 있는 값과 같은 자리다. 언리얼 · 유니티 · Godot 엔진엔 서버 저장소가 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

#include <utility>

namespace sw
{
    /**
     * @class ServiceRecordTable
     * @brief 레코드 형식 @p TRecord 하나를 싣는 표입니다. 레코드 형식은 `REFLECT` 이고 `ServiceRecordUnknownFields _unknownProperties{};` 를 든다(`ServiceRecordUtil::readDurable`).
     */
    template <typename TRecord>
    class ServiceRecordTable
    {
    public:
        using Record = TRecord;

        explicit ServiceRecordTable( const utf8* pName )
            : _name{ pName }
        {
        }

        const hashed_string& getName() const { return _name; }
        /** @brief 표 이름이 필요한 곳(지우기 · 조건 · 목록 읽기 · TTL 규칙)에 그대로 넘긴다. 값은 이 이름으로 읽고 쓸 수 없다(바이트가 비공개다). */
        operator const hashed_string&() const { return _name; }

        /** @brief 목록 읽기 등으로 받은 레코드의 값을 읽습니다. 못 읽으면 오류 로그를 남기고 false 이며 @p outRecord 는 그대로입니다. */
        [[nodiscard]] bool decode( string_view key, const ServiceRecord& record, TRecord& outRecord ) const { return ServiceRecordUtil::readDurable( key, record._bytes, outRecord ); }
        /** @brief 목록 읽기로 받은 레코드(키가 채워져 있다)의 값을 읽습니다. */
        [[nodiscard]] bool decode( const ServiceRecord& record, TRecord& outRecord ) const { return decode( record._key, record, outRecord ); }

        /**
         * @brief 레코드 하나를 읽습니다. 있으면 Ok 와 값 · 버전, 없으면 NotFound(@p outRecord 는 그대로, 버전은 `kAbsentVersion`), 값이 깨졌으면 Corrupt 입니다. 그 밖은 저장소 결과입니다.
         * @param pOutVersion 있으면 읽은 버전(없으면 `kAbsentVersion`)을 담는다 — 같은 버전 조건으로 다시 쓸 때.
         */
        [[nodiscard]] ServiceStoreResult read( IServiceStoreConnection& connection, string_view key, TRecord& outRecord, uint64* pOutVersion = nullptr ) const
        {
            ServiceRecord            record;
            const ServiceStoreResult result = connection.readRecord( _name, key, record );
            if ( pOutVersion != nullptr )
                *pOutVersion = record._version;
            if ( result != ServiceStoreResult::Ok )
                return result;
            return decode( key, record, outRecord ) ? ServiceStoreResult::Ok : ServiceStoreResult::Corrupt;
        }

        /** @brief 값을 바이트로 씁니다(`ServiceTransaction::put` 이 부른다). 못 쓰면 오류 로그를 남기고 false 입니다. */
        [[nodiscard]] bool encode( const TRecord& record, vector<uint8>& outBytes ) const { return ServiceRecordUtil::encode( record, outBytes ); }

    private:
        hashed_string _name;
    };

    /** @class ServiceIndexTable @brief 값 없는 줄의 표입니다(키가 곧 내용이다). */
    class SW_GF_API ServiceIndexTable
    {
    public:
        explicit ServiceIndexTable( const utf8* pName )
            : _name{ pName }
        {
        }

        const hashed_string& getName() const { return _name; }
        operator const hashed_string&() const { return _name; }

    private:
        hashed_string _name;
    };

    /** @class ServiceLinkTable @brief 다른 레코드의 id 하나를 가리키는 표입니다(가변 정수). 서비스 id(계정 · 세션 · 거래 · 길드)는 0 을 내지 않는다 — 0 은 깨진 값이다. */
    class SW_GF_API ServiceLinkTable
    {
    public:
        explicit ServiceLinkTable( const utf8* pName )
            : _name{ pName }
        {
        }

        const hashed_string& getName() const { return _name; }
        operator const hashed_string&() const { return _name; }

        /**
         * @brief 받은 레코드의 id 를 읽습니다. 깨졌거나 0 이면 오류 로그를 남기고 false 이며 @p outID 는 그대로입니다.
         * @details 깨진 연결을 "연결 없음" 으로 보면 세션 폐기 · 중복 로그인 검사가 살아 있는 세션을 놓친 채 성공한다 — 부르는 쪽은 요청을 실패로 돌려준다.
         */
        [[nodiscard]] bool decode( string_view key, const ServiceRecord& record, uint64& outID ) const;
        /** @brief 연결 하나를 읽습니다. 있으면 Ok 와 id, 없으면 NotFound, 깨졌으면 Corrupt 입니다. */
        [[nodiscard]] ServiceStoreResult read( IServiceStoreConnection& connection, string_view key, uint64& outID, uint64* pOutVersion = nullptr ) const;
        /** @brief id 를 바이트로 씁니다(`ServiceTransaction::put` 이 부른다). 0 이면 오류 로그와 false 입니다. */
        [[nodiscard]] bool encode( string_view key, uint64 id, vector<uint8>& outBytes ) const;

    private:
        hashed_string _name;
    };

    /** @class ServiceOpaqueTable @brief 이 서버가 뜻을 읽지 않는 바이트의 표입니다(봉인된 비밀 · 저장해 둔 응답). 바이트를 해석하는 코드는 다른 표 형식을 쓴다. */
    class SW_GF_API ServiceOpaqueTable
    {
    public:
        explicit ServiceOpaqueTable( const utf8* pName )
            : _name{ pName }
        {
        }

        const hashed_string& getName() const { return _name; }
        operator const hashed_string&() const { return _name; }

        /** @brief 받은 레코드의 바이트입니다. */
        static const vector<uint8>& getBytes( const ServiceRecord& record ) { return record._bytes; }
        /** @brief 받은 레코드의 바이트를 옮겨 갑니다. */
        static vector<uint8> takeBytes( ServiceRecord& record ) { return std::move( record._bytes ); }

    private:
        hashed_string _name;
    };

    template <typename TRecord>
    void ServiceTransaction::put( const ServiceRecordTable<TRecord>& table, string_view key, const typename ServiceRecordTable<TRecord>::Record& record, uint64 expectedVersion )
    {
        vector<uint8> bytes;
        if ( table.encode( record, bytes ) == false )
            _bMalformed = SW_TRUE; // 쓰지 못한 값을 빈 값으로 싣지 않는다 — 커밋이 Invalid 다
        putBytes( table.getName(), key, std::move( bytes ), expectedVersion );
    }
} // namespace sw
