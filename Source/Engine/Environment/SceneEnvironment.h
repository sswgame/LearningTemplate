/**
 * @file Engine/Environment/SceneEnvironment.h
 * @brief 씬에 하나뿐인 환경 상태(하늘 · 안개 · 표면 변형 · 전역 머티리얼 값)의 칸입니다(씬 서비스) — 렌더러는 이 칸만 읽고 컴포넌트 · 기능 서비스를 모릅니다.
 * @details 예전에는 월드 코어의 `ISceneService` 가 렌더 기여 가상 함수(`contributeSky` · `contributeFog` · `contributeSurfaceDeform`)를 들고 매니저가
 *          모든 서비스를 돌며 먼저 true 를 낸 쪽을 썼다 — 둘이 맡으면 등록 순서가 조용히 이겼고, 코어가 기능 층(환경 8 층) 형식을 앞으로 선언해 들었다.
 *          이제 씬에 하나뿐인 것은 칸 하나에 둔다(언리얼 `FScene::AddExponentialHeightFog` · Godot `Environment` 자리).
 *          - 표면 변형: 값 칸. 바뀌는 곳이 그 서비스의 `update` 하나라(`SurfaceDeformSystem`) 그 끝에 값을 쓴다.
 *          - 하늘 · 안개: 물을 때 컴포넌트를 찾는다(`SkyComponent::findSky` · `FogComponent::findFog`). 안개는 보는 위치(물속)에 따라 값이 달라 값으로 둘 수 없다.
 *            하늘은 방향광 · 편집한 값에 기대고 에디터가 멈춘 동안 씬 서비스가 돌지 않아(`SceneTick::Pause`) 값 칸이면 멈춘 하늘이 남는다 — 그래서 안개처럼 물을 때 찾는다.
 *          - 전역 머티리얼 값(`MaterialGlobals`): 환경 시간 · 기준 시점은 `update` 가 쓰고(에디터 편집 중에는 돌지 않아 0 이다 — 멈춘 바람), 날씨는 물을 때 묶음에서 읽는다.
 *            묶음은 씬이 사는 동안 쥔다 — 씬이 비워지면 놓아 다음 씬이 파일 값에서 시작한다.
 *          하늘 · 안개를 맡는 쪽은 컴포넌트 하나뿐이라 갈아 끼우는 칸(델리게이트 · 묶기)을 두지 않는다 — 다른 원천이 생기면 그때 이 찾기를 고친다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Surface/SurfaceDeformBinding.h"
#include "Engine/Graphics/Material/MaterialGlobals.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;
    class MaterialParameterCollection;
    struct FogSettings;
    struct RainOcclusionSettings;
    struct SkyGradient;

    /** @class SceneEnvironment @brief 씬의 하늘 · 안개 · 표면 변형 · 전역 머티리얼 값 칸입니다(파일 머리말 참고). 게임 스레드에서만 씁니다. */
    class SW_API SceneEnvironment final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneEnvironment";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneEnvironment( GameObjectManager& manager );

        /** @brief 환경 시간과 기준 시점(게임 카메라)을 이번 프레임의 값으로 씁니다. 결정적 실행(`_bFreezeEnvironmentTime`)이면 시간은 멈춘다. */
        void update( float32 deltaTime ) override;
        /** @brief 표면 변형 값과 전역 머티리얼 값을 비웁니다. */
        void onSceneCleared() override;

        /** @brief 표면 변형 값을 씁니다(텍스처가 선 뒤 — `SurfaceDeformSystem::update`). */
        void setSurfaceDeform( const SurfaceDeformBinding& binding );
        /** @brief 표면 변형 값을 비웁니다(층 없음 — 셰이더가 정점을 옮기지 않는다). */
        void clearSurfaceDeform();

        /** @brief 이 씬의 하늘입니다 — 씬의 첫 활성 하늘 컴포넌트(`SkyComponent::findSky`). 하늘이 없으면 false 입니다. */
        [[nodiscard]] bool findSky( SkyGradient& outGradient ) const;
        /** @brief @p viewPosition 에서 보는 안개입니다 — 물속이면 물의 안개가 컴포넌트의 값을 덮는다(`FogComponent::findFog`). 안개가 없으면 false 입니다. */
        [[nodiscard]] bool findFog( const float3& viewPosition, FogSettings& outSettings ) const;
        /** @brief 이 씬의 비 가림 맵 값입니다(`RainOcclusionComponent::findRainOcclusion`). 없으면 false 입니다. */
        [[nodiscard]] bool findRainOcclusion( RainOcclusionSettings& outSettings ) const;
        /** @brief 표면 변형 층입니다. 값이 없으면 빈 값(핸들 없음)을 쓰고 false 입니다. */
        [[nodiscard]] bool findSurfaceDeform( SurfaceDeformBinding& outBinding ) const;
        /**
         * @brief 이번 프레임의 전역 머티리얼 값입니다(렌더 패킷이 싣는다). 처음 부를 때 날씨 묶음(`EngineDefaultAssets::_materialGlobals`)을 받아 쥡니다.
         * @details 묶음을 읽지 못하거나 전역 칸이 빠지면 오류를 한 번 남기고 그 칸은 0(맑음)이다 — 프레임마다 다시 읽지 않는다.
         */
        [[nodiscard]] MaterialGlobals collectMaterialGlobals();

    private:
        const GameObjectManager&                _manager;
        SurfaceDeformBinding                    _surfaceDeform;
        float4                                  _environment;      ///< `MaterialGlobals::_environment`
        shared_ptr<MaterialParameterCollection> _globalParameters; ///< 전역 값을 먹이는 묶음(쥔 동안 런타임 값이 산다)
        uint8                                   _bHasSurfaceDeform          : 1;
        uint8                                   _bGlobalParametersRequested : 1; ///< 묶음을 청했다(실패해도 다시 청하지 않는다)
    };
} // namespace sw
