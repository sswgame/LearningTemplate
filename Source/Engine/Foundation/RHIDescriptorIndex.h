/**
 * @file RHIDescriptorIndex.h
 * @brief bindless 디스크립터 인덱스 형식(`RHIDescriptorIndex` 와 번호 공간마다의 형식)과 무효 값입니다.
 * @details 게임이 보는 헤더(`TextureCache.h` · `MaterialInstance.h`)가 텍스처의 안정 SRV 번호를 들고 다닌다. 타깃 게이트(Config/Rules/Layers/TargetLayers.toml)는 게임 쪽에서
 *          `Engine/Graphics/RHI` 를 막으므로, 값 하나뿐인 이 형식을 RHI 밖 바닥 층으로 내렸다(언리얼 `FRHIDescriptorHandle` 의 인덱스를 머티리얼이 들고 다니는 자리).
 *          RHI 쪽은 `RHIHandles.h` 가 이 헤더를 include 해 그대로 쓴다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw
{
    /**
     * @class RHIDescriptorIndex
     * @brief bindless 리소스 인덱스입니다. 디스크립터 힙이나 세트 안의 바인딩 인덱스를 가리킵니다. 기본 생성은 무효 값입니다.
     * @details 정수 별칭이던 때는 슬롯 번호 · 개수 · 서수를 번호 자리에 넘겨도(인자 순서를 바꿔 적어도) 컴파일됐고, 셰이더가 남의 디스크립터를 읽었다. 정수에서는 명시로만
     *          만든다 — 만드는 쪽은 번호 발급기(`RHIBindlessRegistry`)다. 밖으로는 암시 변환이다: 셰이더가 받는 값(루트 상수 · 구조 버퍼 칸)과 백엔드 표의 칸 번호가 이 uint 그대로다.
     *          언리얼 `FRHIDescriptorHandle`(명시 생성 · `GetIndex`) 자리다.
     */
    class RHIDescriptorIndex
    {
    public:
        constexpr RHIDescriptorIndex() noexcept = default;
        /** @brief 정수 값에서 만듭니다. 명시로만 부른다 — 번호 발급기 · 해제 큐 · 테스트가 쓴다. */
        template <typename Int, std::enable_if_t<std::is_integral_v<Int> && std::is_same_v<Int, bool> == false, int> = 0>
        constexpr explicit RHIDescriptorIndex( Int value ) noexcept
            : _value{ static_cast<uint32>( value ) }
        {
        }

        /** @brief 셰이더가 읽는 값입니다. 형식을 추론하는 자리(로그 · optional)는 `getValue` 를 쓴다. */
        constexpr                      operator uint32() const noexcept { return _value; }
        [[nodiscard]] constexpr uint32 getValue() const noexcept { return _value; }
        /** @brief 해시 컨테이너 키로 쓸 때의 해시입니다(`DefaultHash` 가 부른다). */
        [[nodiscard]] constexpr uint64 computeHash() const noexcept { return _value; }

    private:
        uint32 _value{ constant::invalid_index::kUint32 };
    };

    /**
     * @class RHITypedDescriptorIndex
     * @brief 번호 공간(@p Tag)을 아는 번호입니다. 다른 공간의 번호와 서로 바꿔 쓸 수 없습니다.
     * @details DX11 · GL · Vulkan · WebGPU 는 텍스처 · 버퍼 · UAV 번호를 각자 0 부터 내므로 공간을 바꿔 넘긴 번호도 살아 있는 남의 자원이다 — 한 형식이면 조용히 다른 자원을 읽었다.
     *          공간을 모르는 자리(등록표 · 링 미러 · 로그)는 바탕 형식으로 받는다. 바탕에서 이 형식으로는 명시로만 만든다 — 그 공간의 등록표를 아는 백엔드가 한다.
     */
    template <typename Tag>
    class RHITypedDescriptorIndex : public RHIDescriptorIndex
    {
    public:
        constexpr RHITypedDescriptorIndex() noexcept = default;
        /** @brief 정수 값에서 만듭니다. 상속 생성자는 아래 명시 생성자와 모양이 같아 가려지므로 다시 적는다. */
        template <typename Int, std::enable_if_t<std::is_integral_v<Int> && std::is_same_v<Int, bool> == false, bool> = true>
        constexpr explicit RHITypedDescriptorIndex( Int value ) noexcept
            : RHIDescriptorIndex{ value }
        {
        }
        /** @brief 공간을 모르는 번호에서 만듭니다. 다른 공간의 번호는 들어오지 못한다(바탕 형식 그대로만). */
        template <typename Index, std::enable_if_t<std::is_same_v<Index, RHIDescriptorIndex>, int> = 0>
        constexpr explicit RHITypedDescriptorIndex( Index index ) noexcept
            : RHIDescriptorIndex{ index.getValue() }
        {
        }
    };

    struct RHITextureSRVIndexTag;
    struct RHIBufferSRVIndexTag;
    struct RHIUnorderedAccessIndexTag;

    /** @brief 텍스처 SRV 의 번호입니다(`IRHIResourceFactory::registerBindlessTexture`). */
    using RHITextureSRVIndex = RHITypedDescriptorIndex<RHITextureSRVIndexTag>;
    /** @brief 버퍼 SRV · 상수 버퍼의 번호입니다(`IRHIResourceFactory::registerBindlessBufferSRV`). */
    using RHIBufferSRVIndex = RHITypedDescriptorIndex<RHIBufferSRVIndexTag>;
    /** @brief 버퍼 · 텍스처 UAV 의 번호입니다(`IRHIResourceFactory::registerBindlessBufferUAV` · `registerBindlessTextureUAV` — 한 공간이다). */
    using RHIUnorderedAccessIndex = RHITypedDescriptorIndex<RHIUnorderedAccessIndexTag>;

    /** @brief 무효 번호 표지의 형식입니다. 어느 공간의 번호로도 바뀝니다. */
    struct RHIInvalidDescriptorIndex
    {
        template <typename Index, std::enable_if_t<std::is_base_of_v<RHIDescriptorIndex, Index>, int> = 0>
        constexpr operator Index() const noexcept
        {
            return Index{};
        }
        constexpr operator uint32() const noexcept { return RHIDescriptorIndex{}.getValue(); }
    };

    /** @brief 유효하지 않은 디스크립터 인덱스입니다. */
    inline constexpr RHIInvalidDescriptorIndex kInvalidDescriptorIndex{};
} // namespace sw
