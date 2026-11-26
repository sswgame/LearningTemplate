/**
 * @file Engine/Visual/2D/Shadow/Scene2DOccluderProvider.cpp
 * @brief 2D 그림자 가림막의 렌더 기여입니다(씬 서브시스템) — 가림막 세그먼트를 씬 라이트 목록 맨 뒤에 붙입니다. 렌더러는 `ShadowCaster2DComponent` 를 모릅니다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Visual/2D/Shadow/ShadowCaster2DComponent.h"

namespace sw
{
    /** @class Scene2DOccluderProvider @brief 씬의 `ShadowCaster2DComponent` 를 라이트 목록에 내는 씬 서브시스템입니다. */
    class Scene2DOccluderProvider final : public ISceneSubsystem
    {
    public:
        /** @brief 씬 서브시스템 등록 줄입니다(`SW_SCENE_SUBSYSTEM` 가 정의한다). */
        static const SceneSubsystemRegistration& getSceneSubsystemRegistration();
        static constexpr const utf8*             kSubsystemName = "Scene2DOccluderProvider";
        const utf8*                              getSubsystemName() const override { return kSubsystemName; }

        explicit Scene2DOccluderProvider( GameObjectManager& manager );

        void update( float32 deltaTime ) override { (void)deltaTime; }
        void onSceneCleared() override {}
        void contributeLights( SceneLightPhase phase, vector<GPULight>& inoutList ) const override;

    private:
        const GameObjectManager& _manager;
    };

    SW_SCENE_SUBSYSTEM( Scene2DOccluderProvider, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    Scene2DOccluderProvider::Scene2DOccluderProvider( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    void Scene2DOccluderProvider::contributeLights( const SceneLightPhase phase, vector<GPULight>& inoutList ) const
    {
        if ( phase != SceneLightPhase::ShadowOccluders )
            return;
        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting_2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : _manager.getComponentRegistry().getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGPUShadowSegments( inoutList );
        }
    }
} // namespace sw
