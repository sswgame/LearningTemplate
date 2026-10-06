/**
 * @file ISceneFrameSystem.h
 * @brief 씬 한 프레임의 정해진 자리(`FrameSystems` 단계)에서 게임 스레드로 한 번 도는 시스템의 계약입니다. 매니저가 소유합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ISceneFrameSystem
     * @brief `SceneFrameStepList.xxx` 의 `FrameSystems` 단계(BeginPlay 뒤 · PrePhysics 틱 앞)에서 붙인 순서대로 불립니다.
     * @details 틱 밖 게임 스레드라 구조가 얼어 있지 않습니다 — 오브젝트를 만들어도 됩니다. 컴포넌트 틱 그룹으로는 "병렬 틱보다 먼저 · 게임 스레드" 를
     *          말할 수 없어서 있는 자리입니다(GameFramework 의 조종 시스템이 모든 폰의 이번 틱 의도를 여기서 채운다).
     *          **핫 리로드 되는 모듈(게임 · 키트)의 타입은 붙이지 않습니다** — 매니저가 소유한 객체의 vtable 이 모듈과 함께 사라집니다.
     *          엔진 · GameFramework 기반의 것만 붙이고, 그것도 자기 컴포넌트가 모두 빠지면 뗍니다(모듈을 내리면 컴포넌트가 먼저 지워진다).
     */
    class SW_API ISceneFrameSystem
    {
    public:
        virtual ~ISceneFrameSystem() = default;

        /** @brief 이번 프레임의 일을 합니다(게임 스레드, 틱 밖). */
        virtual void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) = 0;

    protected:
        ISceneFrameSystem()                                      = default;
        ISceneFrameSystem( const ISceneFrameSystem& )            = delete;
        ISceneFrameSystem& operator=( const ISceneFrameSystem& ) = delete;
    };
} // namespace sw
