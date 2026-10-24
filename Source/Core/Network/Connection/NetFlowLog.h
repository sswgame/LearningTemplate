/**
 * @file NetFlowLog.h
 * @brief 네트워크 메시지(모든 채널)를 보낸 곳 · 받은 곳을 프로세스 링에 남깁니다. 트레이스 내보내기가 흐름 이벤트(`s` · `f`)로 써서 합친 트레이스에 프로세스 사이 화살표를 그립니다.
 * @details - **id 는 양쪽이 따로 계산한다**(`makeFlowID` — 연결 토큰 · 보낸 쪽 · 채널 · 양쪽이 같게 아는 번호). 신뢰 순서는 "보낸 N 번째 = 받은 N 번째" 이고,
 *            신뢰 순서 없음은 선의 신뢰 ID, 비신뢰 · 순서만은 (패킷 번호, 패킷 안 자리)다 — 셋 다 이미 선에 있거나 양쪽이 같은 순서로 세므로 프로토콜에 칸을 더하지 않는다.
 *          - **기록하는 쪽이 있을 때만 남긴다**: 타임라인 기록이 켜진 동안(`ProfilerTimeline::setRecording` 이 `addRecorder` · `removeRecorder`)만이다. 없으면 원자 하나 읽고 돌아온다.
 *          - 링은 Network 층에 있다 — Core 층 규칙상 네트워크가 진단(`ProfilerTimeline`)을 include 하지 못하므로, 진단 · 엔진이 이 링을 읽는다.
 *          - **스레드**: 어느 스레드에서 불러도 된다. 칸은 원자 덧셈으로 잡고 칸마다 스탬프(쓴 번호 + 1, 쓰는 중 0)으로 읽는 쪽이 덮이는 중인 칸을 버린다.
 * @note Shipping 에서는 빠집니다(`SW_NET_FLOW_LOG_ENABLED` 0 — 트레이스 내보내기가 없다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"

/** @brief 메시지 흐름 링이 컴파일되는 구성이면 1 입니다(트레이스 내보내기 `SW_PROFILER_TRACE_ENABLED` 와 같은 조건). */
#if defined( SW_SHIPPING )
    #define SW_NET_FLOW_LOG_ENABLED 0
#else
    #define SW_NET_FLOW_LOG_ENABLED 1
#endif

#if SW_NET_FLOW_LOG_ENABLED

namespace sw
{
    /** @brief 메시지 흐름 사건 하나 — 보낸 곳(`_bReceive` 거짓)과 받은 곳이 같은 `_id` 를 가집니다. */
    struct NetFlowEvent
    {
        uint64 _nanos{ 0 }; ///< 단조 시계 나노초
        uint64 _id{ 0 };
        uint8  _kind{ 0 };         ///< 메시지 종류(첫 바이트)
        bool   _bReceive{ false }; ///< 참이면 받은 곳(`f`), 거짓이면 보낸 곳(`s`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetFlowLog
     * @brief 프로세스 하나의 메시지 흐름 링입니다(전부 static).
     */
    class SW_API NetFlowLog
    {
    public:
        /** @brief 링 크기입니다. 넘치면 오래된 것부터 버립니다. */
        static constexpr uint32 kCapacity = 4096;

        /**
         * @brief 양쪽이 따로 계산해도 같은 흐름 id 입니다 — 연결 토큰(양쪽 솔트의 XOR) · 보낸 쪽이 서버인가 · 채널 · 그 채널에서 양쪽이 같게 아는 메시지 번호.
         * @details 번호는 채널마다 다르다(`NetConnection::recordFlow`): 신뢰 순서는 넣은 · 꺼낸 순번, 신뢰 순서 없음은 신뢰 ID, 비신뢰 · 순서만은 (패킷 번호 << 8 | 패킷 안 자리).
         *          값은 53 비트 안이다(트레이스 뷰어가 JSON 수를 배정밀도로 읽는다). 위 27 비트가 연결과 방향, 2 비트가 채널, 아래 24 비트가 번호(돌면 겹친다 — 트레이스 한 판은 몇 초다).
         */
        static uint64 makeFlowID( uint64 connectionToken, bool bFromServer, NetChannelType channel, uint64 wireIndex );

        /** @brief 기록하는 쪽을 하나 더합니다 · 뺍니다(타임라인 기록이 켜지고 꺼질 때). 하나라도 있으면 `record` 가 남깁니다. */
        static void addRecorder();
        static void removeRecorder();

        /** @brief 사건 하나를 남깁니다(`NetHost` 가 신뢰 순서 메시지를 큐에 넣을 때와 꺼낼 때). 기록하는 쪽이 없으면 원자 하나 읽고 돌아옵니다. */
        static void record( uint64 id, uint8 kind, bool bReceive );

        /** @brief [@p beginNanos, @p endNanos] 안의 사건을 시각 순으로 담습니다. 복사하는 동안 덮인 칸은 버립니다. */
        static void collect( uint64 beginNanos, uint64 endNanos, vector<NetFlowEvent>& outListFlow );
    };
} // namespace sw

#endif // SW_NET_FLOW_LOG_ENABLED
