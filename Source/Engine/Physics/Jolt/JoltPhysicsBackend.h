/**
 * @file JoltPhysicsBackend.h
 * @brief 3D 물리 백엔드(Jolt)의 진입점입니다. 전역 초기화(할당자, 타입 등록, 잡 시스템)와 씬 생성을 담당합니다.
 * @details 이 헤더는 Jolt 헤더를 include 하지 않는다. `PhysicsSystem` 만 이것을 보고, 나머지 엔진은 `IPhysicsScene3D` 만 본다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    struct PhysicsSettings;

    /** @brief Jolt 백엔드의 전역 상태입니다. 프로세스에 하나이고 `PhysicsSystem` 이 올리고 내립니다. */
    struct SW_API JoltPhysicsBackend
    {
        /**
         * @brief Jolt 를 초기화합니다. 할당을 엔진 할당자로 돌리고(메모리 태그 Physics), 로그와 단언을 엔진 로그로 돌리고, 셰이프 타입을 등록하고,
         *        잡 시스템(엔진 태스크)을 만듭니다. 헤더와 DLL 의 빌드 설정이 다르면(`VerifyJoltVersionID`) 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool initialize();
        /** @brief 잡 시스템과 타입 등록을 해제합니다. 모든 씬이 먼저 사라져 있어야 합니다. */
        static void shutdown();
        /** @brief 올라와 있으면 true 입니다. */
        static bool isInitialized();
        /** @brief 설정(중력, 레이어 테이블, 재질)으로 씬 하나를 만듭니다. 초기화되어 있지 않으면 nullptr 를 반환합니다. */
        static unique_ptr<IPhysicsScene3D> createScene( const PhysicsSettings& settings );
    };
} // namespace sw
