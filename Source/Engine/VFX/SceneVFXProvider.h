/**
 * @file Engine/VFX/SceneVFXProvider.h
 * @brief VFX 의 렌더 기여입니다(씬 서비스) — 파티클 라이트와 방출기마다의 입자 그리기를 렌더러에 냅니다. 렌더러는 `VFXComponent` 를 모릅니다.
 * @details 언리얼 나이아가라가 시스템 프록시로 렌더러에 내는 자리와 같습니다(기억). 입자 형식 자체(`VFXParticle.h`)는 렌더러의 VFX 절반(`Renderer/VFX`)과 함께 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/VFX/VFXParticle.h"

namespace sw
{
    class GameObjectManager;
    class MeshInstanceBatch;

    /** @brief 입자 그리기 하나의 공통 값입니다. */
    struct SceneParticleBatchDesc
    {
        float4x4      _world{};               ///< 방출기 행렬(행벡터 규약)
        hashed_string _texturePath{};         ///< 입자 텍스처(리소스 id)
        float32       _sizeScale{ 1.0f };     ///< 방출기 행렬의 최대 축 배율
        uint8         _bAdditive{ SW_FALSE }; ///< 더하기 블렌드면 SW_TRUE
        uint8         _bRibbon{ SW_FALSE };   ///< 띠면 SW_TRUE
    };

    /** @class ISceneParticleSink @brief 입자 그리기를 받는 쪽입니다(렌더러의 `GPUSceneBuilder`). 낸 순서가 정렬 전 후보 순서입니다. */
    class ISceneParticleSink
    {
    public:
        virtual ~ISceneParticleSink() = default;

        /** @brief CPU 로 시뮬레이션한 입자 배열 하나를 싣습니다. 배열은 공유합니다(방출기가 단계마다 새 배열을 낸다). */
        virtual void addCPUParticles( const shared_ptr<const vector<GPUParticle>>& pListParticle, const SceneParticleBatchDesc& desc ) = 0;
        /** @brief GPU 로 시뮬레이션하는 방출기의 요청 하나를 싣습니다. */
        virtual void addGPUParticles( const VFXGPUEmitterRequest& request, const SceneParticleBatchDesc& desc ) = 0;
        /** @brief GPU 메시 방출기 하나를 싣습니다 — 인스턴스 칸은 받는 쪽이 찾습니다. */
        virtual void addGPUMeshEmitter( const VFXGPUEmitterRequest& request, const MeshInstanceBatch& batch, const float4x4& world, float32 meshRadius ) = 0;
    };

    /** @class SceneVFXProvider @brief 씬의 `VFXComponent` 를 렌더러에 내는 씬 서비스입니다. */
    class SceneVFXProvider final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneVFXProvider";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneVFXProvider( GameObjectManager& manager );

        void update( float32 deltaTime ) override { (void)deltaTime; }
        void onSceneCleared() override {}
        void contributeLights( SceneLightPhase phase, vector<GPULight>& inoutList ) const override;
        void contributeParticles( ISceneParticleSink& sink ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw
