/**
 * @file RawAnimCodec.h
 * @brief 압축하지 않는 기준 코덱입니다 — 균일 표본을 float 그대로 싣고 두 표본 사이를 보간합니다.
 */
#pragma once
#include "Engine/Animation/Codec/AnimCodec.h"

namespace sw
{
    /**
     * @class RawAnimCodec
     * @brief 블롭 = 머리(표본 수 · 표본율 · 트랙 수) + 표본마다 트랙마다 이동 3 · 회전 4 · 스케일 3 의 float32(리틀 엔디언)입니다.
     * @details 오차가 0 인 기준이라 다른 코덱의 오차를 잴 때와 디버그에 씁니다. 샘플링은 `AnimRawClip::sample` 과 같은 식입니다.
     */
    class SW_API RawAnimCodec final : public IAnimCodec
    {
    public:
        /** @brief 프로세스에 하나인 인스턴스입니다(상태가 없습니다). */
        static const RawAnimCodec& getInstance();

        AnimCodecID        getID() const override { return AnimCodecID::Raw; }
        const utf8*        getName() const override { return "raw"; }
        [[nodiscard]] bool compress( const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes ) const override;
        [[nodiscard]] bool sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask = nullptr ) const override;
    };
} // namespace sw
