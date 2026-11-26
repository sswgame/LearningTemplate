/**
 * @file RHIHandles.h
 * @brief RHI 자원 핸들(버퍼, 텍스처, 힙, PSO)과 bindless 디스크립터 인덱스입니다.
 * @details 핸들 값 형식만 담는다. 핸들을 들고 다니기만 하는 헤더(머티리얼, 캔버스)가 서술체(`RHITextureDesc.h` 등)를 끌지 않게 따로 둔다.
 *          장치 콜백 별칭은 `RHIDeviceCallbacks.h`, 버퍼 구간은 `RHIBufferSlice.h` 에 있다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/RHIDescriptorIndex.h"

#include <cstddef>
#include <type_traits>

namespace sw
{
    /**
     * @class RHIHandle
     * @brief 자원 종류(@p Tag)마다 다른 64비트 핸들 값 형식입니다. 크기 · 복사 비용은 `uint64` 와 같다.
     * @details 예전에는 핸들이 모두 `uint64` 별칭이라 텍스처 핸들을 버퍼 자리에, 개수 · 인덱스를 핸들 자리에 넘겨도 컴파일됐다.
     *          지금은 종류 사이 · 정수와 핸들 사이에 암묵 변환이 없다. 정수에서 만들 때는 명시(`RHIBufferHandle{ packed }`)로만,
     *          값을 꺼낼 때는 `getPacked()` 로만 한다(백엔드 핸들 표 · 메모리 추적 키 · 로그).
     *          무효 값은 0 하나다 — 기본 생성 · 리터럴 0 · `nullptr` 가 모두 무효이고(포인터의 널 상수와 같은 규칙), 변수에 든 정수는 들어오지 못한다.
     *          언리얼 `FRHIResource*` · Godot `RID` 가 종류별 형식으로 같은 일을 막는다.
     */
    template <typename Tag>
    class RHIHandle
    {
    public:
        constexpr RHIHandle() noexcept = default;
        /** @brief 무효 핸들입니다. 리터럴 0 과 `nullptr` 만 이 길로 들어온다(널 포인터 상수 규칙) — 변수에 든 정수는 아래 명시 생성자를 거친다. */
        constexpr RHIHandle( std::nullptr_t ) noexcept {}
        /** @brief packed 값에서 핸들을 만듭니다. 명시로만 부른다 — 핸들 표 · 테스트가 쓴다. */
        template <typename Int, std::enable_if_t<std::is_integral_v<Int> && std::is_same_v<Int, bool> == false, int> = 0>
        constexpr explicit RHIHandle( Int packed ) noexcept
            : _packed( static_cast<uint64>( packed ) )
        {
        }

        /** @brief packed 값(백엔드 핸들 표의 index | generation)입니다. */
        [[nodiscard]] constexpr uint64 getPacked() const noexcept { return _packed; }
        /** @brief 0 이 아니면 true 입니다(살아 있는지는 장치가 안다). */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _packed != 0; }
        constexpr explicit           operator bool() const noexcept { return _packed != 0; }
        /** @brief 해시 컨테이너 키로 쓸 때의 해시입니다(`DefaultHash` 가 부른다). */
        [[nodiscard]] constexpr uint64 computeHash() const noexcept { return _packed; }

        friend constexpr bool operator==( RHIHandle lhs, RHIHandle rhs ) noexcept { return lhs._packed == rhs._packed; }
        friend constexpr bool operator!=( RHIHandle lhs, RHIHandle rhs ) noexcept { return lhs._packed != rhs._packed; }
        friend constexpr bool operator<( RHIHandle lhs, RHIHandle rhs ) noexcept { return lhs._packed < rhs._packed; }

    private:
        uint64 _packed{ 0 };
    };

    struct RHIBufferHandleTag;
    struct RHITextureHandleTag;
    struct RHITransientHeapHandleTag;
    struct RHIPipelineStateHandleTag;

    /** @brief GPU 버퍼 리소스의 64비트 핸들입니다. */
    using RHIBufferHandle = RHIHandle<RHIBufferHandleTag>;

    /** @brief GPU 텍스처 리소스의 64비트 핸들입니다. 0 은 무효다 — 텍스처를 받는 RHI 호출(렌더 패스 컬러 타깃 · 전이 · 블릿)은 0 을 오류로 알리고 버린다. */
    using RHITextureHandle = RHIHandle<RHITextureHandleTag>;

    /**
     * @brief 스왑체인 백버퍼를 가리키는 전용 텍스처 핸들입니다 — 렌더 패스 컬러 타깃 · 전이 · 블릿이 받는다. 어느 핸들 표도 이 값을 내지 않는다(칸 번호 · 세대 모두 최댓값).
     * @details 예전에는 0 이 "없음" 과 "백버퍼" 를 겸해, 선언하지 않은 그래프 접근 · 만들지 못한 첨부의 0 이 화면 그리기로 읽혔다. 언리얼은 뷰포트 백버퍼를
     *          `FRHIViewport::GetBackBuffer` 의 텍스처 객체로, Godot 은 화면 프레임버퍼를 따로 받는다 — 여기서는 핸들 값 하나로 따로 둔다.
     *          어느 스왑체인인지는 `RHIRenderPassInfo::_pSwapChain` 이 정한다(nullptr 이면 메인 창).
     */
    inline constexpr RHITextureHandle kRHIBackBufferTexture{ ~uint64{ 0 } };

    /** @brief 텍스처를 겹쳐 놓는 힙의 64비트 핸들입니다(`IRHIResourceFactory::createTransientHeap`). 0 은 없음이다. */
    using RHITransientHeapHandle = RHIHandle<RHITransientHeapHandleTag>;

    /** @brief 파이프라인 상태 객체(PSO)의 64비트 핸들입니다. */
    using RHIPipelineStateHandle = RHIHandle<RHIPipelineStateHandleTag>;
} // namespace sw
