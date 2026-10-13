/**
 * @file PhysicsSystem.h
 * @brief 물리 엔진 서비스입니다. 설정 테이블을 읽고, 백엔드(3D 는 Jolt, 2D 는 Box2D)를 초기화하고, 씬을 만들어 줍니다(`engine::getPhysicsSystem`).
 * @details 백엔드를 고르는 곳은 여기 하나다(`createScene3D`, `createScene2D`). 다른 엔진 코드는 `IPhysicsScene3D` 와 `IPhysicsScene2D` 만 본다.
 *          엔진에서는 서브시스템 `Physics` 이고(설정 표는 리소스, Jolt 잡은 태스크라 Resource · Task 뒤에 선다), 씬(`GameObjectManager` 의
 *          `ScenePhysics`)은 그보다 먼저 사라진다(Scene 단계가 Physics 에 의존한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/ISubsystem.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"

namespace sw
{
    /** @class PhysicsSystem @brief 물리 서비스입니다. 파일 머리말 참고. */
    class SW_API PhysicsSystem : public ISubsystem
    {
    public:
        /** @brief 설정 테이블의 기본값입니다. */
        static constexpr const utf8* kSettingsPath = "engine/physics/physicssettings.xml";

        PhysicsSystem();
        ~PhysicsSystem() override;

        PhysicsSystem( const PhysicsSystem& )            = delete;
        PhysicsSystem& operator=( const PhysicsSystem& ) = delete;

        /**
         * @brief 설정 테이블을 읽고 백엔드를 초기화합니다. 테이블이 없으면 기본값(레이어와 재질 `Default`)으로 시작하고 경고를 남깁니다.
         *        테이블이 틀리면(모르는 이름 등) 오류를 남기고 false 를 반환하며, 백엔드를 초기화하지 못해도 false 를 반환합니다.
         */
        [[nodiscard]] bool initialize( string_view settingsPath = kSettingsPath );
        /** @brief 백엔드를 내립니다. 이 서비스로 만든 씬은 먼저 모두 사라져 있어야 합니다. */
        void shutdown();

        // ISubsystem — 기동 단계 `Physics`. 기본 설정 표로 선다.
        EngineInitResult initializeSubsystem() override { return initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed; }
        void             shutdownSubsystem() override { shutdown(); }
        const utf8*      getSubsystemName() const override { return "Physics"; }
        const utf8*      getSubsystemDependencies() const override { return "Resource Task"; }
        EngineInitTarget getSubsystemTarget() const override { return EngineInitTarget::All; }
        MemoryTag        getSubsystemMemoryTag() const override { return MemoryTag::Physics; }

        /** @brief 올라와 있으면 true 입니다. */
        bool isInitialized() const { return _bInitialized; }

        /** @brief 지금 설정으로 3D 씬을 만듭니다. 올라와 있지 않으면 nullptr 입니다. */
        unique_ptr<IPhysicsScene3D> createScene3D() const;
        /** @brief 주어진 설정으로 3D 씬을 만듭니다(테스트와 미리보기 월드에 씁니다). */
        unique_ptr<IPhysicsScene3D> createScene3D( const PhysicsSettings& settings ) const;
        /** @brief 지금 설정으로 2D 씬을 만듭니다. */
        unique_ptr<IPhysicsScene2D> createScene2D() const;
        /** @brief 주어진 설정으로 2D 씬을 만듭니다. */
        unique_ptr<IPhysicsScene2D> createScene2D( const PhysicsSettings& settings ) const;

        /** @brief 읽은 설정 표입니다. */
        const PhysicsSettings& getSettings() const { return _settings; }
        /** @brief 설정을 바꿉니다. 이미 있는 씬에는 적용되지 않으며(새로 만드는 씬부터 적용), 검사에 실패하면 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool setSettings( const PhysicsSettings& settings );

        /**
         * @brief 설정된 3D 중력(m/s²)을 반환합니다. 엔진 서비스가 묶여 있으면 이 서비스의 설정 테이블(`physicssettings.xml`)을, 없으면(도구나 서비스 없는 테스트) 설정 테이블 기본값을 씁니다.
         * @details 물리 바깥에서 중력을 쓰는 계산(탄도, 파괴 하중, 코스터, 파도 분산, 스프링 체인)은 값을 따로 적지 않고 이 함수 하나를 읽는다
         *          (언리얼의 `UWorld::GetGravityZ` 가 프로젝트 물리 설정 한 곳을 읽는 것과 같다).
         */
        static float3 getConfiguredGravity();
        /** @brief `getConfiguredGravity()` 의 크기(m/s²)입니다. 아래(-Y) 방향만 쓰는 계산이 읽습니다. */
        static float32 getConfiguredGravityMagnitude();

    private:
        PhysicsSettings _settings;
        bool            _bInitialized;
    };
} // namespace sw
