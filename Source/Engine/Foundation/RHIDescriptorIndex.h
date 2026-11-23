/**
 * @file RHIDescriptorIndex.h
 * @brief bindless 디스크립터 인덱스 형식(`RHIDescriptorIndex`)과 무효 값입니다.
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

    /** @brief 유효하지 않은 디스크립터 인덱스입니다. */
    inline constexpr RHIDescriptorIndex kInvalidDescriptorIndex{};
} // namespace sw
