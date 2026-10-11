/**
 * @file ACLAnimCodec.h
 * @brief ACL(Animation Compression Library 2.1) 백엔드입니다. ACL 과 RTM 헤더는 이 폴더의 .cpp 에서만 include 합니다.
 */
#pragma once
#include "Engine/Animation/Codec/AnimCodec.h"

namespace sw
{
    /**
     * @class ACLAnimCodec
     * @brief 트랙을 ACL `qvvf` 변환 트랙으로 압축합니다. 블롭은 `acl::compressed_tracks` 바이트 그대로입니다.
     * @details ACL 이 하는 일은 다음과 같다. 회전은 smallest-three 로 양자화하고, 범위를 줄인 뒤 가변 비트율을 적용하고, 상수와 기본값 트랙을 제거하고, 계층을 따라 가상 정점 오차가
     *          `precision` 을 넘지 않는 선에서 비트를 줄인다(오차는 `shell_distance` 만큼 떨어진 정점으로 측정한다). 런타임은
     *          `decompression_context` 로 시각을 찾아 트랙마다 쓰개(writer)에 값을 넘긴다.
     */
    class SW_API ACLAnimCodec final : public IAnimCodec
    {
    public:
        /** @brief 프로세스에 하나인 인스턴스입니다(상태가 없습니다). */
        static const ACLAnimCodec& getInstance();

        AnimCodecID        getID() const override { return AnimCodecID::ACL; }
        const utf8*        getName() const override { return "acl"; }
        [[nodiscard]] bool compress( const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes ) const override;
        [[nodiscard]] bool sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask = nullptr ) const override;
    };
} // namespace sw
