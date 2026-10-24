/**
 * @file IPhysicsSceneFwd.h
 * @brief 물리 씬 `IPhysicsScene3D` · `IPhysicsScene2D` 의 이름만 알리는 전방 선언 헤더입니다.
 * @details 씬을 포인터 · 참조(`IPhysicsScene3D&`, `unique_ptr<IPhysicsScene3D>&`)로만 받는 헤더가 무거운 `IPhysicsScene.h`(역할 인터페이스 일곱 개와
 *          `PhysicsDesc.h`)를 끌어오지 않게 한다. 씬의 함수를 부르거나 `unique_ptr` 멤버를 소멸시키는 파일은 `IPhysicsScene.h` 를 직접 include 한다.
 *          별칭의 정의는 여기 한 곳이다(`IPhysicsScene.h` 가 이 헤더를 include 한다).
 */
#pragma once

namespace sw
{
    struct PhysicsDimension2D;
    struct PhysicsDimension3D;

    template <typename TDimension>
    class IPhysicsScene;

    using IPhysicsScene3D = IPhysicsScene<PhysicsDimension3D>;
    using IPhysicsScene2D = IPhysicsScene<PhysicsDimension2D>;
} // namespace sw
