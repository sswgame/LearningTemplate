/**
 * @file TextureStreamingPolicy.h
 * @brief 텍스처 밉을 어디까지 올리고 내릴지 정하는 순수 계산(`TextureStreamingPolicy`)입니다 — 예산 · 화면이 원하는 밉 · 유휴 프레임만 입력이라 GPU 없이 테스트한다.
 * @details 설계: DesignBacklog "렌더 · RHI 설계(2026-11-17)" R6-2. 언리얼 `FRenderAssetStreamingManager` 의 wanted mip · 풀 예산, 유니티 Mipmap Streaming 의 메모리 예산 자리다.
 *          올리기 · 내리기를 실제로 내는 쪽(R6-6 — bindless 번호를 지키는 rebind 뒤)은 이 결정을 그대로 따른다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TextureStreamingConfig;

    /** @brief 텍스처 하나의 스트리밍 상태입니다. 밉 번호 0 이 가장 크다. */
    struct TextureStreamingCandidate
    {
        uint32 _mipCount{ 0 };
        uint32 _residentFirstMip{ 0 }; ///< 지금 GPU 에 있는 가장 큰 밉
        uint32 _wantedFirstMip{ 0 };   ///< 화면 크기에서 구한, 필요한 가장 큰 밉
        uint32 _idleFrames{ 0 };       ///< 화면 크기를 알리지 않은 프레임 수
        uint64 _topMipBytes{ 0 };      ///< 밉 0 하나의 바이트(아래 밉은 넷에 하나씩 줄어든다)

        /** @brief 가장 큰 밉이 @p firstMip 일 때 GPU 에 있는 바이트입니다(@p firstMip 부터 끝 밉까지). */
        [[nodiscard]] uint64 computeResidentBytes( uint32 firstMip ) const;
    };

    /** @brief 바꿀 텍스처 하나입니다 — 목록 번호와 새로 둘 가장 큰 밉. 바뀌지 않는 텍스처는 결정에 들지 않는다. */
    struct TextureStreamingDecision
    {
        uint32 _candidateIndex{ 0 };
        uint32 _targetFirstMip{ 0 };
    };

    /**
     * @class TextureStreamingPolicy
     * @brief 예산 안에서 무엇을 올리고 내릴지 정합니다.
     * @details 규칙:
     *          - 유휴 프레임이 `TextureStreamingConfig::_idleFramesBeforeRelease` 이상이면 필요한 밉은 가장 작은 밉이다(보이지 않는다).
     *          - 예산을 넘으면(엄격히 큼) 화면에서 작은 것(필요한 밉 번호가 큰 것) → 오래 안 보인 것 순으로 내린다. 하나는 필요한 밉까지, 이미 필요한 만큼이면 한 밉씩 — 예산 아래가 되면 멈춘다.
     *          - 예산 아래면 화면에서 큰 것부터 필요한 밉까지 올린다. 올려서 예산을 넘는 것은 올리지 않는다 — 내린 직후 같은 텍스처를 다시 올려 경계에서 흔들리지 않는다.
     *          바이트는 밉마다 넷에 하나씩 준다고 본다(블록 압축의 끝 밉 올림은 무시한다 — 순서 판단에는 충분하다).
     */
    class SW_API TextureStreamingPolicy
    {
    public:
        /** @brief @p listCandidate 를 보고 바꿀 것을 @p outListDecision 에 채웁니다(지우고 채운다). @p residentBytes 는 지금 풀에 든 바이트 합입니다. */
        static void decide( vector_reference<const TextureStreamingCandidate> listCandidate, uint64 residentBytes, uint64 budgetBytes, const TextureStreamingConfig& config,
                            vector<TextureStreamingDecision>& outListDecision );
    };
} // namespace sw
