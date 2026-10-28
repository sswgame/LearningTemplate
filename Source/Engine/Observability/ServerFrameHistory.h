/**
 * @file ServerFrameHistory.h
 * @brief 서버의 최근 N 틱(틱 시간 · 접속 수 · 밀린 틱 수)을 고정 크기 링 버퍼에 남깁니다. 느린 틱 · 비정상 종료 때 파일로 덤프해 사후 분석에 씁니다(서버 운영 O1).
 * @details 틱마다 한 칸을 덮어쓰는 비용뿐이라(할당 없음) 늘 켜 둔다. 게임 스레드 하나가 쓰고 읽는다 — 잠금이 없다. 메시지 종류별 바이트는 칸마다 싣지 않고
 *          덤프하는 순간의 누적 셈(`GameNetSession::makeStatsReport`)을 덤프 본문에 붙인다(칸마다 싣는 비용 · 프로파일러 탭의 "지난 보고" 상태를 건드리는 일을 피한다).
 *          크래시 때는 새 파일을 쓰지 않는다 — 죽어 가는 프로세스에서 할당 · 파일 쓰기를 하지 않는다. 대신 크래시 핸들러가 문맥 파일을 쓸 때 이 링 버퍼를 직접 읽어
 *          최근 칸들을 그 파일 끝에 싣는다(`writeCrashTail` — `setCrashExtraTextWriter` 에 건다). 틱마다 문맥 값을 갱신하는 비용도 없고 마지막 틱까지 남는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 틱 하나의 기록입니다. */
    struct ServerFrameSample
    {
        uint64 _tick{ 0 };             ///< 끝낸 틱 수(이 틱을 끝낸 뒤의 번호)
        int64  _bodyMicroseconds{ 0 }; ///< 틱 본문 시간(마이크로초)
        uint32 _connectionCount{ 0 };  ///< 서버 서비스가 받고 있던 연결 수
        uint32 _droppedTickTotal{ 0 }; ///< 이 틱까지 밀려 버린 틱의 누적 수
    };
} // namespace sw

namespace sw
{
    /** @class ServerFrameHistory @brief 최근 틱 기록의 링 버퍼입니다. 용량 0 이면 아무것도 남기지 않습니다. */
    class SW_API ServerFrameHistory
    {
    public:
        ServerFrameHistory();

        /** @brief 용량을 정하고 비웁니다. 0 이면 끕니다. */
        void initialize( uint32 capacity );
        /** @brief 한 틱을 남깁니다. 가장 오래된 것을 덮어씁니다. 꺼져 있으면 아무 일도 하지 않습니다. */
        void record( const ServerFrameSample& sample );

        uint32 getCapacity() const { return static_cast<uint32>( _listSample.size() ); }
        /** @brief 지금 들어 있는 칸 수입니다(용량 이하). */
        uint32 getCount() const { return _count; }
        /** @brief 가장 오래된 것부터 센 @p index 번째 칸을 돌려줍니다. 범위 밖이면 false 입니다. */
        [[nodiscard]] bool tryGetSample( uint32 index, ServerFrameSample& outSample ) const;
        /** @brief 가장 새 칸입니다. 비어 있으면 false 입니다. */
        [[nodiscard]] bool tryGetLatest( ServerFrameSample& outSample ) const;

        /**
         * @brief 덤프 본문을 @p outText 뒤에 붙입니다. 머리(까닭 · 세션 · 칸 수 · 평균 · 최대 · 최대 틱)와 칸마다 한 줄(`tick=N body_us=N connections=N dropped=N`)입니다.
         * @param reason 덤프한 까닭(`slow_tick` · `shutdown_signal` …). 줄바꿈이 없어야 한다.
         * @param extraText 끝에 그대로 붙일 글(메시지 종류별 셈 줄 등). 비어도 된다.
         */
        /**
         * @brief 가장 새 칸부터 최대 @p maxTicks 칸을 오래된 순서로 @p pOutText 에 씁니다(`tick=N body_us=N connections=N dropped=N` 한 줄씩, 앞에 머리 한 줄).
         * @details **할당 · 잠금이 없습니다** — 크래시 경로가 부릅니다. 게임 스레드가 쓰는 도중에 읽으면 한 칸이 어긋나 보일 수 있으나 진단용이라 받아들입니다.
         *          용량이 모자라면 새 칸을 우선해 앞쪽을 줄입니다. 쓴 길이를 돌려줍니다(널 종료).
         */
        uint32 writeCrashTail( utf8* pOutText, uint32 capacity, uint32 maxTicks ) const;

        void writeReport( string& outText, string_view reason, string_view sessionID, string_view extraText ) const;

    private:
        vector<ServerFrameSample> _listSample;
        uint32                    _nextIndex;
        uint32                    _count;
    };
} // namespace sw
