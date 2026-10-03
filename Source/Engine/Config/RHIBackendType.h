/**
 * @file RHIBackendType.h
 * @brief 선택한 렌더링 백엔드의 종류입니다.
 *
 * @details 이 열거형이 **Config 에 있는 이유**: 이것은 "지금 어느 백엔드를 쓰는가" 라는 설정값이고,
 *          `EngineConfig::_window._defaultRHI` 와 전역 변수 `gv_rhiBackend` 가 그 값을 듭니다.
 *          설정 헤더가 이름 하나를 쓰려고 `Graphics/RHI/RHITypes.h` 전부를 끌어오지 않게 여기 둡니다.
 *          의존 방향은 Graphics → Config 입니다.
 *
 * @note 여기서 이름을 바꾸거나 값을 재배치하면 **이미 저장된 설정과 씬이 깨집니다.** 값은
 *       `EngineConfig.json` 에 문자열로 직렬화되지만, 리플렉션 등록기가 빠지면 역직렬화가 조용히
 *       기본값으로 떨어집니다. Shipping 빌드에서도 마찬가지입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RHIBackend
     * @brief 엔진이 지원하는 그래픽 API 입니다.
     */
    ENUM()
    enum class RHIBackend : uint32
    {
        DirectX11 = 0, ///< Direct3D 11
        DirectX12 = 1, ///< Direct3D 12 (Bindless)
        Vulkan    = 2, ///< Vulkan 1.3 (Bindless)
        OpenGL    = 3, ///< OpenGL 4.5+
    };
} // namespace sw
