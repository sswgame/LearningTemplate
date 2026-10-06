/**
 * @file ServerSelection.h
 * @brief 접속 분배 — 서버 목록에서 손님 하나(또는 파티 · 경기 인원)를 받을 서버를 고르는 순수 함수입니다. 클라이언트 배정(서버 디렉터리)과 매칭의 전용 서버 배정이 같이 쓴다.
 * @details 후보: Open(점검 허용이면 Maintenance 도) · 같은 종류 · 빌드 판 일치(질의 0 = 무관) · 하트비트가 `_staleMs` 안 · 부하 + 자리 ≤ 정원.
 *          같은 지역 후보가 있으면 그중에서, 없고 `_bAllowOtherRegion` 이면 전체에서. 찬 비율(부하 × 1024 / 정원)이 낮은 것, 같으면 서버 id 가 작은 것(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 고르기 질의입니다. */
    struct ServerSelectionQuery
    {
        string _kind{};
        string _region{};
        int64  _staleMs{ 15000 }; ///< 하트비트가 이보다 오래되면 죽은 것으로 본다
        uint32 _buildVersion{ 0 };
        int32  _seatCount{ 1 }; ///< 한 번에 들어갈 자리(파티 · 경기 인원)
        uint8  _bAllowOtherRegion{ SW_TRUE };
        uint8  _bIncludeMaintenance{ SW_FALSE }; ///< 점검 중 허용 계정 — Maintenance 상태 서버도 후보
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerSelection
     * @brief 고르기입니다.
     */
    struct SW_GF_API ServerSelection
    {
        /** @brief 고른 서버의 번호(@p listStatus 안)입니다. 후보가 없으면 false 이고 @p outIndex 는 -1. */
        [[nodiscard]] static bool pickServer( const vector<ServerStatus>& listStatus, const ServerSelectionQuery& query, int64 nowMs, int32& outIndex );
        /** @brief @p status 가 질의의 후보인가입니다(지역은 보지 않는다). */
        static bool isCandidate( const ServerStatus& status, const ServerSelectionQuery& query, int64 nowMs );
    };
} // namespace sw
