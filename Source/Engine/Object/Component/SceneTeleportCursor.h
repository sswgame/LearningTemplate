/**
 * @file Engine/Object/Component/SceneTeleportCursor.h
 * @brief 씬 컴포넌트가 그사이 순간이동했는지 한 번 읽는 자리입니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    enum class TeleportType : uint8;

    class SceneComponent;

    /**
     * @class SceneTeleportCursor
     * @brief 지난 프레임의 자리(쓸기 시작점 · 속도 차분 · 따로 든 몸 자리)를 든 쪽이 그사이 순간이동했는지 한 번 읽는 자리입니다.
     * @details `SceneComponent::onTeleported` 는 씬 컴포넌트만 받는다 — 트랜스폼이 아닌 컴포넌트(칼 궤적 알림 · 좌석 · 이동기)는 이것으로 같은 사실을 안다(언리얼 `ETeleportType` 이 지난 변환을
     *          버리는 자리). 거리로 짐작하지 않는다. 월드 원점 이동은 순간이동이 아니다(`onWorldOriginShifted` 가 자리를 옮긴다).
     */
    class SW_API SceneTeleportCursor
    {
    public:
        SceneTeleportCursor();

        /** @brief 지난 호출 뒤 @p component 가 순간이동했으면 그 종류입니다(한 번이라도 운동을 지웠으면 `ResetPhysics`). 아니면 없음입니다. */
        [[nodiscard]] optional<TeleportType> consume( const SceneComponent& component );

    private:
        uint16 _teleportSerial;
        uint16 _motionResetSerial;
    };
} // namespace sw
