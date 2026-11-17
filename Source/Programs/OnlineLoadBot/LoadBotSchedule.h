/**
 * @file LoadBotSchedule.h
 * @brief 봇 하나의 보낼 시각표입니다. 단계의 지연을 어느 시각부터 잴지와, 기다림(`wait`)이 끝나는 시각을 정합니다(감사 O-12).
 * @details - 닫힌 루프(`closed`): 기다림은 앞 응답을 받은 뒤부터 센다. 지연은 실제로 보낸 시각부터 잰다. 서버가 느려지면 봇도 덜 보내 지연이 숨는다(coordinated omission).
 *          - 열린 루프(`open`): 기다림은 앞 단계를 보내려던 시각부터 센다 — 보내는 간격이 서버 속도와 무관하다. 늦게 보낸 단계의 지연은 보내려던 시각부터 잰다.
 *            기다림 없이 이어지는 단계(로그인 뒤 채널 들기)는 앞 단계의 응답이 있어야 보낼 수 있으므로 실제로 보낸 시각부터 잰다.
 *          k6 의 `constant-arrival-rate` 실행기 · Gatling 의 열린 모델 · wrk2 의 보내려던 시각 기준 지연과 같다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 시나리오의 보내기 방식입니다. 시나리오 `_pacing` 칸(`closed` · `open`, 필수). */
    enum class LoadBotPacing : uint8
    {
        Closed = 0, ///< 응답을 받은 뒤부터 기다림을 센다
        Open        ///< 보내려던 시각부터 기다림을 센다 — 지연은 보내려던 시각 기준
    };

    /**
     * @class LoadBotSchedule
     * @brief 봇 하나의 시각표입니다. 시각은 실행기 시계(`LoadBotRunner::getNowUs`)의 마이크로초입니다.
     */
    class LoadBotSchedule
    {
    public:
        explicit LoadBotSchedule( LoadBotPacing pacing )
            : _targetUs{ 0 }
            , _pacing{ pacing }
            , _bWaited{ SW_FALSE }
        {
        }

        /** @brief 봇이 시작하는 시각입니다. */
        void start( int64 nowUs )
        {
            _targetUs = nowUs;
            _bWaited  = SW_FALSE;
        }

        /** @brief 기다림 @p waitUs 를 넣고 깨어날 시각을 돌려줍니다. 열린 루프는 보내려던 시각부터 세므로 이미 지난 시각일 수 있다(바로 보낸다). */
        int64 addWait( int64 waitUs, int64 nowUs )
        {
            if ( _pacing == LoadBotPacing::Closed )
                return nowUs + waitUs;
            _targetUs += waitUs;
            _bWaited = SW_TRUE;
            return _targetUs;
        }

        /** @brief 요청 단계를 시작합니다. 지연을 잴 기준 시각을 돌려줍니다. */
        int64 beginStep( int64 nowUs )
        {
            if ( _pacing == LoadBotPacing::Closed )
                return nowUs;
            const int64 anchorUs = _bWaited == SW_TRUE && _targetUs < nowUs ? _targetUs : nowUs;
            _targetUs            = anchorUs;
            _bWaited             = SW_FALSE;
            return anchorUs;
        }

    private:
        int64         _targetUs; ///< 마지막 단계를 보내려던 시각이다
        LoadBotPacing _pacing;
        uint8         _bWaited; ///< 마지막 단계 뒤에 기다림이 있었다 — 다음 단계는 시각표의 시각에 보내려던 것이다
    };
} // namespace sw
