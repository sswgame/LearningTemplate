/**
 * @file ServiceReloadSchedule.h
 * @brief 운영자가 고치는 표(라이브 이벤트 · 점검 · 공지)를 저장소에서 다시 읽는 일정 — 요청되었거나 주기가 지났고 읽는 중이 아니면 읽는다.
 * @details LiveOps · ServerDirectory 서비스가 같은 상태 기계(요청 표시 · 읽는 중 · 마지막 읽은 시각)를 각자 적었다(StructureRefactor 8 절 11).
 *          처음에는 요청된 상태로 시작한다 — 서비스가 서자마자 한 번 읽는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    /** @brief 다시 읽기 일정입니다. 서비스의 틱(게임 · 서버 스레드)에서만 씁니다. */
    class ServiceReloadSchedule
    {
    public:
        ServiceReloadSchedule()
            : _bRequested{ SW_TRUE }
            , _bReloading{ SW_FALSE }
        {
        }
        /** @brief 다음 틱에 다시 읽게 합니다(다른 서버가 바꿨다는 알림 · 이 서버의 쓰기 뒤). */
        void request() { _bRequested = SW_TRUE; }
        /** @brief 지금 읽기를 시작해야 하면 true 입니다 — 읽는 중이 아니고, 요청되었거나 @p periodMs 가 지났다. */
        bool shouldStart( UnixMilliseconds nowMs, int64 periodMs ) const { return _bReloading == false && ( _bRequested || nowMs - _lastStartMs >= periodMs ); }
        /** @brief 읽기를 시작했습니다(요청 표시를 지우고 시각을 적는다). */
        void markStarted( UnixMilliseconds nowMs )
        {
            _bRequested  = SW_FALSE;
            _bReloading  = SW_TRUE;
            _lastStartMs = nowMs;
        }
        /** @brief 읽기가 끝났습니다(실패여도 — 이전 값을 두고 다음 주기에 다시 읽는다). */
        void markFinished() { _bReloading = SW_FALSE; }

    private:
        UnixMilliseconds _lastStartMs{ 0 };
        uint8            _bRequested : 1;
        uint8            _bReloading : 1;
    };
} // namespace sw
