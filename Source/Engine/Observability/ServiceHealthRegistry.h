/**
 * @file ServiceHealthRegistry.h
 * @brief 서버 상태 — 틱 박동(살아 있음) · 이름 붙은 검사(준비 필수 여부 · 상태 · 설명) · 비우는 중 표시를 모아 `/healthz` · `/readyz` 에 답합니다.
 * @details - 살아 있음: 서버 루프가 `_livenessTimeoutMs`(기본 10 초) 안에 돌았나 — 멈춘 프로세스를 감시자(systemd · k8s liveness · 윈도우 서비스 감시)가 재시작한다.
 *          - 준비됨: 살아 있고 · 비우는 중이 아니고 · 준비 필수 검사가 모두 Ok/Degraded — 부하 분산기가 새 접속을 보낼지. 종료 요청을 받으면 먼저
 *            `setDraining( true )` 로 준비되지 않음을 알린 뒤 내린다.
 *          - 쓰기(서버 · 서비스 스레드)와 읽기(운영 HTTP 의 I/O 스레드)가 잠금 하나를 나눈다 — 초당 몇 번이라 경합이 없다. 시각은 단조 밀리초를 넘겨받는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 검사 하나의 상태입니다. */
    enum class HealthState : uint8
    {
        Ok = 0,
        Degraded, ///< 돌지만 나빠졌다(캐시 없이 영속만 — 준비됨은 유지)
        Failing   ///< 준비 필수 검사면 준비되지 않음
    };

    SW_API const utf8* toString( HealthState state );
} // namespace sw

namespace sw
{
    /** @class ServiceHealthRegistry @brief 서버 상태 모음입니다(프로세스에 하나 — 서버 조립이 들고 넘긴다). */
    class SW_API ServiceHealthRegistry
    {
    public:
        ServiceHealthRegistry();

        /** @brief 검사를 등록하고 번호를 돌려줍니다. 처음 상태는 Failing(아직 확인하지 않음)입니다. */
        int32 registerCheck( string_view name, bool bRequiredForReady );
        /** @brief 범위 밖 번호는 무시합니다. */
        void setCheck( int32 checkIndex, HealthState state, string_view detail );
        /** @brief 서버 루프가 돌았습니다(틱마다). */
        void markTick( int64 monotonicMs );
        void setDraining( bool bDraining );
        void setLivenessTimeoutMs( int64 timeoutMs );

        bool isLive( int64 monotonicMs ) const;
        bool isReady( int64 monotonicMs ) const;
        bool isDraining() const;
        /** @brief 사람이 읽는 보고(`live 1` · `ready 0` · `draining 0` · `check <이름> <상태> [설명]` 줄)를 @p outText 뒤에 붙입니다. */
        void writeReport( string& outText, int64 monotonicMs ) const;

    private:
        struct Check
        {
            string      _name{};
            string      _detail{};
            HealthState _state{ HealthState::Failing };
            uint8       _bRequiredForReady{ SW_TRUE };
        };

        bool isLiveLocked( int64 monotonicMs ) const;
        bool isReadyLocked( int64 monotonicMs ) const;

        vector<Check> _listCheck;
        mutable mutex _mutex;
        int64         _lastTickMs;
        int64         _livenessTimeoutMs;
        uint8         _bTicked;
        uint8         _bDraining;
    };
} // namespace sw
