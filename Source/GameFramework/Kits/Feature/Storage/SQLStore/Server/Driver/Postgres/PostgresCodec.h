/**
 * @file PostgresCodec.h
 * @brief PostgreSQL 드라이버의 변환입니다. 자리표시자 바꾸기, 매개변수 인코딩, 이진 결과 칸 읽기, SQLSTATE 분류를 libpq 없이 합니다(RESP 의 `RespCodec` 과 같은 나눔).
 * @details 서버가 돌려준 바이트와 호출한 쪽이 넘긴 값이 지나는 경계라 DB 없이 테스트한다. libpq 를 부르는 쪽은 `PostgresDriver.cpp` 하나다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"

namespace sw
{
    /** @brief `PQexecPrepared` 에 넘길 칸들입니다. `_listValue` 가 `_listText` 를 가리키므로 함께 살아야 합니다. */
    struct PostgresParameters
    {
        vector<string>      _listText{};
        vector<const utf8*> _listValue{};
        vector<int32>       _listLength{};
        vector<int32>       _listFormat{}; ///< 0 이면 텍스트, 1 이면 이진
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PostgresCodec
     * @brief 파일 머리말을 참고하세요.
     */
    struct PostgresCodec
    {
        /** @brief 텍스트 형식 매개변수로 쓰는 값입니다. */
        static constexpr int32 kFormatText = 0;
        /** @brief 이진 형식 매개변수로 쓰는 값입니다. */
        static constexpr int32 kFormatBinary = 1;

        /** @brief `?` 를 `$1..$n` 으로 바꿉니다. 따옴표 · 줄 주석(`--`) · 블록 주석(겹칠 수 있다) 안은 건너뜁니다. @p outParamCount 는 매개변수 개수입니다. */
        static string convertPlaceholders( string_view sql, int32& outParamCount );
        /**
         * @brief 값을 `PQexecPrepared` 의 칸으로 바꿉니다. 정수와 문자열은 텍스트 형식, 바이트는 이진 형식입니다.
         * @return 값 수가 자리 수와 다르거나 문자열에 NUL 이 있으면 false 와 까닭입니다. 텍스트 형식은 NUL 로 끝나므로 뒤가 말없이 잘려 다른 키를 묻게 된다.
         */
        [[nodiscard]] static expected<void> encodeParameters( const SQLValue* pParam, int32 paramCount, int32 placeholderCount, PostgresParameters& outParameters );
        /**
         * @brief 이진 결과 칸 하나를 읽습니다. 정수(bool · int2 · int4 · int8)는 빅엔디언이고 부호를 확장합니다.
         * @return 아는 타입이 아니거나 정수의 길이가 타입과 맞지 않으면 false 와 까닭입니다. 길이는 서버가 준 값이므로 믿지 않습니다.
         */
        [[nodiscard]] static expected<void> decodeColumn( uint32 typeOid, const uint8* pData, int32 size, SQLValue& outValue );
        /** @brief SQLSTATE 를 결과로 나눕니다. 고유 키 위반, 직렬화 실패 · 교착, 연결 · 서버 내려감, 락 시한이고 그 밖은 Error 입니다. */
        static SQLResult classifySQLState( string_view state );

        // PostgreSQL 타입 OID(pg_type)이다. 이진 결과를 읽는 데 쓰는 것만 둔다.
        static constexpr uint32 kBoolOid    = 16;
        static constexpr uint32 kByteaOid   = 17;
        static constexpr uint32 kInt8Oid    = 20;
        static constexpr uint32 kInt2Oid    = 21;
        static constexpr uint32 kInt4Oid    = 23;
        static constexpr uint32 kTextOid    = 25;
        static constexpr uint32 kVarcharOid = 1043;
    };
} // namespace sw
