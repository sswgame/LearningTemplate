#include "pch.h"

#include "Engine/Environment/Surface/GroundSurfaceComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct GroundSurfaceComponentInternal
        {
            /** @brief 발 위에서 쏘기 시작하는 높이(m)입니다 — 발이 바닥에 조금 묻혀도 바닥을 맞힌다. */
            static constexpr float32 kProbeLift = 0.3f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GroundSurfaceComponent::sampleUnder( GameObject& object, float32 probeLength, SurfaceSample& outSample )
    {
        GameObjectManager*                       pManager = object.getMutableManager();
        const SceneComponent*                    pScene   = object.getPrimarySceneComponent();
        const shared_ptr<const SurfaceKindTable> pTable   = pManager != nullptr && pScene != nullptr ? SurfaceKindTable::findDefault() : nullptr;
        if ( pTable == nullptr )
            return false;
        const float3 feet = pScene->getWorldPosition();
        return SurfaceSample::sampleBelow( *pManager, pTable, feet + float3{ 0.0f, GroundSurfaceComponentInternal::kProbeLift, 0.0f }, feet - float3{ 0.0f, probeLength, 0.0f }, object.getObjectID(),
                                           false, outSample );
    }

    GroundSurfaceComponent::GroundSurfaceComponent()
        : _probeLength{ 0.6f }
        , _lastSample{}
        , _groundKind{}
        , _bSampledThisFrame{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void GroundSurfaceComponent::sampleGroundOnce( GameObjectManager& manager )
    {
        if ( _bSampledThisFrame == SW_TRUE )
            return;
        sampleGround( manager );
        _bSampledThisFrame = SW_TRUE;
    }

    void GroundSurfaceComponent::finishFrameSample( GameObjectManager& manager )
    {
        if ( _bSampledThisFrame == SW_FALSE )
            sampleGround( manager );
        _bSampledThisFrame = SW_FALSE;
    }

    void GroundSurfaceComponent::sampleGround( GameObjectManager& manager )
    {
        (void)manager;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        SurfaceSample sample;
        const bool    bSampled                    = sampleUnder( *pOwner, _probeLength, sample );
        _lastSample                               = bSampled ? sample : SurfaceSample{};
        _groundKind                               = bSampled ? sample._pKind->_name : hashed_string{};
        CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        if ( bSampled && pController != nullptr )
            pController->applyMoveScale( sample._pKind->_moveScale );
    }
} // namespace sw
