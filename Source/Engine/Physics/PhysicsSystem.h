/**
 * @file PhysicsSystem.h
 * @brief 물리 엔진 서비스 — 설정 표를 읽고, 백엔드(3D Jolt · 2D Box2D)를 올리고, 씬을 만들어 줍니다(`engine::getPhysicsSystem`).
 * @details 백엔드를 고르는 자리가 여기 하나입니다(`createScene3D` · `createScene2D`). 다른 엔진 코드는 `IPhysicsScene3D` · `IPhysicsScene2D` 만 봅니다.
 *          기동 단계 `Physics`(`EngineInitStepList.xxx`)가 `initialize` 를, 종료가 `shutdown` 을 부릅니다 — 씬(`GameObjectManager` 의 `ScenePhysics`)은
 *          그보다 먼저 사라집니다(Scene 단계가 Physics 에 의존한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"

namespace sw
{
    /** @class PhysicsSystem @brief 물리 서비스입니다. 파일 머리말 참고. */
    class SW_API PhysicsSystem
    {
    public:
        /** @brief 설정 표의 기본 자리입니다. */
        static constexpr const utf8* kSettingsPath = "engine/physics/physicssettings.xml";

        PhysicsSystem();
        ~PhysicsSystem();

        PhysicsSystem( const PhysicsSystem& )            = delete;
        PhysicsSystem& operator=( const PhysicsSystem& ) = delete;

        /**
         * @brief 설정 표를 읽고 백엔드를 올립니다. 표가 없으면 기본값(레이어 · 재질 `Default`)으로 오르고 경고를, 표가 틀리면(모르는 이름 등) 오류를 남기고
         *        false 입니다. 백엔드를 올리지 못해도 false 입니다.
         */
        [[nodiscard]] bool initialize( string_view settingsPath = kSettingsPath );
        /** @brief 백엔드를 내립니다. 이 서비스로 만든 씬은 먼저 모두 사라져 있어야 합니다. */
        void shutdown();
        /** @brief 올라와 있으면 true 입니다. */
        bool isInitialized() const { return _bInitialized; }

        /** @brief 지금 설정으로 3D 씬을 만듭니다. 올라와 있지 않으면 nullptr 입니다. */
        unique_ptr<IPhysicsScene3D> createScene3D() const;
        /** @brief 주어진 설정으로 3D 씬을 만듭니다(시험 · 미리보기 월드). */
        unique_ptr<IPhysicsScene3D> createScene3D( const PhysicsSettings& settings ) const;
        /** @brief 지금 설정으로 2D 씬을 만듭니다. */
        unique_ptr<IPhysicsScene2D> createScene2D() const;
        /** @brief 주어진 설정으로 2D 씬을 만듭니다. */
        unique_ptr<IPhysicsScene2D> createScene2D( const PhysicsSettings& settings ) const;

        /** @brief 읽은 설정 표입니다. */
        const PhysicsSettings& getSettings() const { return _settings; }
        /** @brief 설정을 바꿉니다. 이미 있는 씬에는 들지 않습니다(새로 만드는 씬부터). 검사에 지면 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool setSettings( const PhysicsSettings& settings );

        /**
         * @brief 설정된 3D 중력(m/s²)입니다 — 엔진 서비스가 묶여 있으면 이 서비스의 설정 표(`physicssettings.xml`), 없으면(도구 · 서비스 없는 시험) 설정 표 기본값.
         * @details 물리 바깥에서 중력을 쓰는 계산(탄도 · 파괴 하중 · 코스터 · 파도 분산 · 스프링 사슬)은 숫자를 따로 적지 않고 이것 하나를 읽는다
         *          (언리얼 `UWorld::GetGravityZ` ← 프로젝트 물리 설정과 같은 한 출처).
         */
        static float3 getConfiguredGravity();
        /** @brief `getConfiguredGravity()` 의 크기(m/s²)입니다. 아래(-Y) 방향만 쓰는 계산이 읽습니다. */
        static float32 getConfiguredGravityMagnitude();

    private:
        PhysicsSettings _settings;
        bool            _bInitialized;
    };
} // namespace sw
