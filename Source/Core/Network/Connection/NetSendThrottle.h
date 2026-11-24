/**
 * @file NetSendThrottle.h
 * @brief 연결마다의 송신 속도를 RTT · 패킷 손실로 조절합니다(AIMD, 감사 N-11).
 * @details 고정 토큰 버킷은 회선이 막혀도 상한 속도로 계속 보내 신뢰 채널의 재전송을 키운다(잃은 만큼 다시 보내고, 줄이 길어져 재전송 시한이 헛되이 지난다).
 *          조절 간격(`_adjustIntervalSeconds`)마다 연결 통계를 보고, 막힘이면 속도에 `_decreaseFactor` 를 곱하고(곱셈 감소) 아니면 `_increaseBytesPerSecond` 를 더한다(덧셈 증가).
 *          속도는 `[_minBytesPerSecond, 상한]` 안이다 — 하한이 있어 손실이 계속돼도 신뢰 메시지는 하한 속도로 나아간다.
 *          막힘은 둘 중 하나다: 손실률(`NetConnectionStats::_packetLoss`)이 `_lossThreshold` 를 넘거나, 평활 RTT 가 최근 창(`kMinRttWindowSeconds`)에서 본 가장 작은 RTT 보다
 *          `_maxQueueDelaySeconds` 넘게 길다(회선 줄에 쌓인 지연 — LEDBAT 의 목표 지연과 같은 생각).
 *          가장 작은 RTT 를 연결 처음부터 들고 있으면 경로가 길어진 연결(이동망 전환 · 경유 변경)이 그 차이를 줄 지연으로 보고 끝까지 하한에 묶인다 — 창이 지난 값은 버린다
 *          (RFC 9002 5.2 는 새로 잡는 것을 허용하고, BBR 은 10 초 창이다). 우리가 쌓은 줄이면 하한으로 줄인 동안 빠지므로 창 안에 진짜 최솟값이 다시 보인다.
 *          상용 비교: ENet 의 패킷 스로틀(RTT 가 평균 + 흔들림을 넘으면 감속, 밑이면 가속) · 언리얼 NetDriver 의 연결 대역폭 · 유니티 Transport 의 혼잡 조절과 같은 자리다.
 *          ENet 은 스로틀로 비신뢰 패킷만 버리지만 여기서는 바이트 속도를 줄인다 — 비신뢰는 줄에서 나이 상한으로 버려지고(`NetConnection::kDefaultMaxUnreliableAge`), 신뢰는 기다린다.
 *
 *          **스레드**: 잠금이 없다. `NetEndpoint` 슬롯의 값 멤버로 그 잠금 안에서만 쓴다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct NetConnectionStats;

    /**
     * @brief 송신 속도 조절의 계수입니다. 값은 데이터(`Config/Engine/NetSendThrottle.json`)가 정하고 `NetSendThrottle::validate` 가 검사합니다.
     * @details 상한은 `NetEndpointConfig::_maxBytesPerSecond` 다(같은 파일의 `_maxBytesPerSecond` 가 채운다).
     */
    struct NetSendThrottleConfig
    {
        int32   _minBytesPerSecond{ 0 };       ///< 속도 하한(초당 바이트). 패킷 하나(`kNetMaxPacketSize`) 이상, 상한 이하
        int32   _increaseBytesPerSecond{ 0 };  ///< 막힘이 없던 조절마다 더하는 속도(덧셈 증가, > 0)
        float64 _decreaseFactor{ 0.0 };        ///< 막힌 조절마다 곱하는 값(곱셈 감소, (0, 1))
        float64 _lossThreshold{ 0.0 };         ///< 손실률이 이 값을 넘으면 막힘이다((0, 1))
        float64 _maxQueueDelaySeconds{ 0.0 };  ///< RTT 가 가장 작은 RTT 보다 이만큼 넘게 길면 막힘이다(> 0)
        float64 _adjustIntervalSeconds{ 0.0 }; ///< 조절 간격(> 0). 연결 통계(손실률)는 0.25 초마다 갱신되므로 그보다 짧으면 같은 표본으로 여러 번 줄인다
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSendThrottle
     * @brief 연결 하나의 지금 송신 속도입니다. 연결되면 상한에서 시작한다(ENet 의 스로틀 시작값과 같다).
     */
    class SW_API NetSendThrottle
    {
    public:
        /** @brief 가장 작은 RTT 를 기억하는 창(초)입니다. 칸 둘(지금 · 지난 창)이라 실제로는 한 창에서 두 창 사이를 기억한다. */
        static constexpr float64 kMinRttWindowSeconds = 10.0;

        /** @brief 계수가 쓸 수 있는 값이면 성공입니다. 틀린 칸은 오류의 세부(`Error::detail`)에 이름이 실립니다. */
        [[nodiscard]] static expected<void> validate( const NetSendThrottleConfig& config, int32 maxBytesPerSecond );

        /**
         * @brief @p nowSeconds 에 조절 간격이 지났으면 @p stats 로 속도를 한 번 조절합니다.
         * @param maxBytesPerSecond 상한(`NetEndpointConfig::_maxBytesPerSecond`).
         */
        void update( float64 nowSeconds, const NetSendThrottleConfig& config, int32 maxBytesPerSecond, const NetConnectionStats& stats );

        /** @brief 지금 송신 속도(초당 바이트)입니다. `update` 전이면 @p maxBytesPerSecond 입니다. */
        float64 getBytesPerSecond( int32 maxBytesPerSecond ) const { return _bytesPerSecond > 0.0 ? _bytesPerSecond : static_cast<float64>( maxBytesPerSecond ); }

    private:
        float64 _bytesPerSecond{ 0.0 };  ///< 0 이면 아직 조절하지 않았다(상한)
        float64 _lastAdjustTime{ -1.0 }; ///< 음수면 아직 조절하지 않았다
        float64 _minRtt{ 0.0 };          ///< 지금 창에서 본 가장 작은 평활 RTT(초). 0 이면 아직 표본이 없다
        float64 _previousMinRtt{ 0.0 };  ///< 지난 창의 값. 0 이면 없다
        float64 _windowStartTime{ 0.0 }; ///< 지금 창이 열린 때(초)
    };
} // namespace sw
