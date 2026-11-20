/**
 * @file SceneSubLevelTypes.h
 * @brief 런타임 하위 레벨(`SceneSubLevelSubsystem`)의 값 형식입니다. 씬의 조립 기록(`Scene::AssemblySnapshot`)이 값으로 들어 따로 둔다 — 서비스 헤더는 `Scene.h` 를 포함한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 하위 레벨 하나의 단계입니다. */
    enum class SubLevelState : uint8
    {
        Queued,   ///< 더하기를 받았다 — 다음 갱신이 앵커를 세우고 짓기 시작한다
        Building, ///< 엔티티 단위로 짓는 중이다(지은 오브젝트는 아직 드러나지 않았다)
        Loaded,   ///< 다 지어 드러났다
        Failed,   ///< 짓지 못했다(오류를 남겼다) — 지은 것을 내렸고 다시 시도하지 않는다
        Removing, ///< 빼기를 받았다 — 다음 갱신이 내리고 목록에서 지운다
    };

    /** @brief 하위 레벨을 놓을 자리입니다(앵커의 월드 위치 · 오일러 회전 — `SceneComponent::setLocalRotation` 과 같은 규칙, 라디안). */
    struct SubLevelPlacement
    {
        float3 _position{};
        float3 _rotation{};
    };

    /** @brief 플레이 종료 복원이 들고 있는 하위 레벨 하나입니다(다 지은 것만 — 오브젝트는 같은 id 로 돌아온다). */
    struct CapturedSubLevel
    {
        string            _name;
        string            _scenePath;
        SubLevelPlacement _placement{};
        uint64            _anchorObjectID{ 0 };
        vector<uint64>    _listObjectID;
    };

    /** @brief 플레이 종료 복원이 들고 있는 하위 레벨 목록입니다(`SceneSubLevelSubsystem::captureState`). */
    struct SceneSubLevelCapture
    {
        vector<CapturedSubLevel> _listSubLevel;
    };
} // namespace sw
