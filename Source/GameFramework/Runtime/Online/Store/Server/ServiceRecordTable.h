/**
 * @file ServiceRecordTable.h
 * @brief 레코드 표(`ServiceRecordTable` — `ServiceStore.h`)의 바이트 코덱입니다. 표에 레코드를 쓰고 읽는 TU 가 이 헤더를 읽는다(이름만 넘기는 TU 는 읽지 않아도 된다).
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
    /** @brief 레코드 형식 @p TRecord 의 바이트 코덱입니다(`ServiceRecordUtil` — 판 머리, 자기 기술형 A, 모르는 칸 보존, 선언 범위 검사). 표 형식(`ServiceRecordTable`)이 부른다. */
    template <typename TRecord>
    struct ServiceRecordCodec
    {
        [[nodiscard]] static bool decode( string_view key, const vector<uint8>& bytes, TRecord& outRecord ) { return ServiceRecordUtil::readDurable( key, bytes, outRecord ); }
        [[nodiscard]] static bool encode( const TRecord& record, vector<uint8>& outBytes ) { return ServiceRecordUtil::encode( record, outBytes ); }
    };
} // namespace sw
