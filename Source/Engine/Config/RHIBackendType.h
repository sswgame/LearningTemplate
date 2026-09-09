/**
 * @file RHIBackendType.h
 * @brief 선택된 렌더링 백엔드 종류.
 *
 * @details 이 열거형이 **Config 에 있는 이유**: 이것은 "지금 어느 백엔드를 쓰는가" 라는 설정값이고,
 *          `EngineConfig::_window._defaultRHI` 와 전역 변수 `gv_rhiBackend` 가 그 값을 든다.
 *          예전에는 `Graphics/RHI/RHITypes.h`(732줄, 44개 타입) 안에 있었고, 그래서 설정 헤더가
 *          이름 하나를 쓰려고 RHI 타입 전부를 끌어왔다 — 그 한 줄이 `Config` 를 Engine 코어 강결합
 *          묶음에 묶어 두는 고리였다. Graphics 는 이미 Config 를 참조하므로(14곳) 방향이 맞는다.
 *
 * @note 여기서 이름을 바꾸거나 값을 재배치하면 **이미 저장된 설정과 씬이 깨진다.** 값은
 *       `EngineConfig.json` 에 문자열로 직렬화되지만, 리플렉션 등록기가 빠지면 역직렬화가 조용히
 *       기본값으로 떨어진다(Shipping 에서 실제로 그런 일이 있었다 — docs/06_Backlog.md 참고).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RHIBackend
     * @brief 엔진이 지원하는 크로스 플랫폼 Graphics API
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
