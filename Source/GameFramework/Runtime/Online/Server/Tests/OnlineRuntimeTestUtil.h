/**
 * @file OnlineRuntimeTestUtil.h
 * @brief 설정 글로 서는 실제 서버 조립(`OnlineServerRuntime`)의 테스트 하니스입니다. 서버 코드라 클라이언트 구성은 이 폴더를 짓지 않습니다.
 * @details 클라이언트 묶음 · 테스트 로그인 서비스는 서비스 틀 하니스(`OnlineHostTestUtil.h`)의 것을 씁니다 — 그쪽은 서버 조립을 모르고 클라이언트 구성에서도 섭니다.
 *          스위트 파일이 아니다 — 조립을 지나는 끝단 테스트(우편 · 영수증 · 푸시 · Postgres · RESP)가 include 한다.
 */
#pragma once
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/Clock.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

namespace sw::test
{
    /**
     * @brief 설정 글로 서는 실제 서버 조립(`OnlineServerRuntime`) + 클라이언트 묶음입니다. 키트 · 저장소 · 캐시 · 제공자를 전용 서버와 같은 설정 파일 모양으로 고릅니다(전송만 루프백 · 평문이다).
     * @details `OnlineTestFixture` 는 서비스를 손으로 올리는 호스트 묶음이고, 이것은 조립(키트 의존 정렬 · 기동 · 종료)까지 지납니다. 로그인 없이 계정을 붙이려면 설정에 `testLogin` 키트를 적습니다.
     *          선언 순서가 수명이다(전송이 조립보다, 조립이 클라이언트보다 오래 산다).
     */
    struct OnlineRuntimeFixture
    {
        static constexpr uint16 kPort = 7600;

        LoopbackStreamNetwork        _network{ 5u };
        SystemMonotonicClock         _clock{};
        unique_ptr<IStreamTransport> _serverTransport{ _network.createTransport() };
        OnlineServerRuntime          _runtime{};
        OnlineTestClients            _clients{ _network };

        /**
         * @brief 온라인 설정 글 @p onlineJSON(`{` 로 시작) 맨 앞에 기반 표 TTL 칸(감사 · 원장 · 닫힌 신고 · 보낸 우편 · 쓸기 시각)을 "지우지 않음"(기간 0)으로 넣은 글입니다.
         * @details 그 칸들은 필수라(코드 기본값 없음) 쓸기를 보지 않는 끝단 테스트도 적어야 한다. 조립이 빠진 칸을 채워 주지는 않는다 — 테스트가 이 함수로 드러내 적는다.
         */
        static string withBaseTTL( string_view onlineJSON )
        {
            constexpr string_view kBaseTTLJSON = R"( "_auditRetention": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_ledgerRetention": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_closedReportTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_mailSentTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 }, "_scheduleRunTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 }, "_ttlSweepMinuteOfDay": 300, "_ttlBackfillRolloutHours": 0,)";
            string                text{ onlineJSON.substr( 0, 1 ) };
            text += kBaseTTLJSON;
            text += onlineJSON.substr( 1 );
            return text;
        }

        /** @brief @p serverJSON(`ServerConfig`)과 @p onlineJSON(`OnlineServerConfig`)으로 조립을 세웁니다. 실패하면 그 까닭입니다. */
        [[nodiscard]] SW_GF_API expected<void> fromConfig( string_view serverJSON, string_view onlineJSON, int64 nowMs );

        void updateAll( int64 nowMs, int32 roundCount = 6 )
        {
            for ( int32 round = 0; round < roundCount; ++round )
            {
                _clients.update( nowMs );
                _runtime.update( UnixMilliseconds{ nowMs } );
            }
        }
    };
} // namespace sw::test
