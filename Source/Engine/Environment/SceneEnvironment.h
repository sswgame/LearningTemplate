/**
 * @file Engine/Environment/SceneEnvironment.h
 * @brief 씬에 하나뿐인 환경 상태(하늘 · 안개 · 표면 변형)의 칸입니다(씬 서비스) — 렌더러는 이 칸만 읽고 컴포넌트 · 기능 서비스를 모릅니다.
 * @details 예전에는 월드 코어의 `ISceneService` 가 렌더 기여 가상 함수(`contributeSky` · `contributeFog` · `contributeSurfaceDeform`)를 들고 매니저가
 *          모든 서비스를 돌며 먼저 true 를 낸 쪽을 썼다 — 둘이 맡으면 등록 순서가 조용히 이겼고, 코어가 기능 층(환경 8 층) 형식을 앞으로 선언해 들었다.
 *          이제 씬에 하나뿐인 것은 칸 하나에 둔다(언리얼 `FScene::AddExponentialHeightFog` · Godot `Environment` 자리).
 *          - 표면 변형: 값 칸. 바뀌는 곳이 그 서비스의 `update` 하나라(`SurfaceDeformSystem`) 그 끝에 값을 쓴다.
 *          - 하늘 · 안개: 묻는 칸(델리게이트) 하나. 안개는 보는 위치(물속)에 따라 값이 달라 값으로 둘 수 없다. 하늘은 방향광 · 편집한 값에 기대고 에디터가 멈춘 동안
 *            씬 서비스가 돌지 않아(`SceneTick::Pause`) 값 칸이면 멈춘 하늘이 남는다 — 그래서 안개와 같은 묻는 칸이다.
 *          칸마다 맡는 쪽은 하나다: 이미 묶인 칸에 둘째를 묶으면 오류로 거절한다(먼저 `unbind…` 로 비운다). 기본은 이 서비스가 컴포넌트 찾기(`SkyComponent::findSky` ·
 *          `FogComponent::findFog`)를 묶어 둔다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Surface/SurfaceDeformBinding.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;
    struct FogSettings;
    struct SkyGradient;

    /** @brief 이 씬의 하늘을 씁니다. 하늘이 없으면 false 입니다. */
    SW_DECLARE_DELEGATE( bool, SkySourceDelegate, SkyGradient& );
    /** @brief 보는 위치에서의 안개를 씁니다. 안개가 없으면 false 입니다. */
    SW_DECLARE_DELEGATE( bool, FogSourceDelegate, const float3&, FogSettings& );

    /** @class SceneEnvironment @brief 씬의 하늘 · 안개 · 표면 변형 칸입니다(파일 머리말 참고). 게임 스레드에서만 씁니다. */
    class SW_API SceneEnvironment final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneEnvironment";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief 하늘 · 안개 칸에 컴포넌트 찾기를 묶습니다. */
        explicit SceneEnvironment( GameObjectManager& manager );

        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 표면 변형 값을 비웁니다. 하늘 · 안개 칸은 묶인 채 남는다(다음 씬도 같은 찾기를 쓴다). */
        void onSceneCleared() override;

        /** @brief 하늘 칸을 @p source 로 묶습니다. 이미 묶였으면 오류를 남기고 false 입니다 — 맡는 쪽은 하나다. */
        [[nodiscard]] bool bindSkySource( const SkySourceDelegate& source );
        /** @brief 하늘 칸을 비웁니다(하늘 없음). */
        void unbindSkySource() { _skySource = SkySourceDelegate{}; }
        /** @brief 안개 칸을 @p source 로 묶습니다. 이미 묶였으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool bindFogSource( const FogSourceDelegate& source );
        /** @brief 안개 칸을 비웁니다(안개 없음). */
        void unbindFogSource() { _fogSource = FogSourceDelegate{}; }
        /** @brief 표면 변형 값을 씁니다(텍스처가 선 뒤 — `SurfaceDeformSystem::update`). */
        void setSurfaceDeform( const SurfaceDeformBinding& binding );
        /** @brief 표면 변형 값을 비웁니다(층 없음 — 셰이더가 정점을 옮기지 않는다). */
        void clearSurfaceDeform();

        /** @brief 이 씬의 하늘입니다. 칸이 비었거나 하늘이 없으면 false 입니다. */
        [[nodiscard]] bool findSky( SkyGradient& outGradient ) const;
        /** @brief @p viewPosition 에서 보는 안개입니다. 칸이 비었거나 안개가 없으면 false 입니다. */
        [[nodiscard]] bool findFog( const float3& viewPosition, FogSettings& outSettings ) const;
        /** @brief 표면 변형 층입니다. 값이 없으면 빈 값(핸들 없음)을 쓰고 false 입니다. */
        [[nodiscard]] bool findSurfaceDeform( SurfaceDeformBinding& outBinding ) const;

    private:
        /** @brief 기본 하늘 찾기 — 씬의 첫 활성 하늘 컴포넌트입니다. */
        bool findComponentSky( SkyGradient& outGradient ) const;
        /** @brief 기본 안개 찾기 — 물속이면 물의 안개가 컴포넌트의 값을 덮는다(`FogComponent::findFog`). */
        bool findComponentFog( const float3& viewPosition, FogSettings& outSettings ) const;

    private:
        const GameObjectManager& _manager;
        SkySourceDelegate        _skySource;
        FogSourceDelegate        _fogSource;
        SurfaceDeformBinding     _surfaceDeform;
        uint8                    _bHasSurfaceDeform : 1;
    };
} // namespace sw
