#include "pch.h"

#include "Engine/Environment/Surface/SurfaceDeformMeshComponent.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct SurfaceDeformMeshComponentInternal
        {
            /** @brief 지면 머티리얼의 칸 이름입니다 — `surface_deform.hlsl` · `surfacedeform.material` 과 같다. */
            static const hashed_string& getMapName()
            {
                static const hashed_string s_name( "_deformMap" );
                return s_name;
            }
            static const hashed_string& getParamsName()
            {
                static const hashed_string s_name( "_deformParams" );
                return s_name;
            }
            static const hashed_string& getTexelName()
            {
                static const hashed_string s_name( "_deformTexel" );
                return s_name;
            }
        };
    } // namespace

    SurfaceDeformMeshComponent::SurfaceDeformMeshComponent()
        : _boundRevision{ 0 }
    {
    }

    void SurfaceDeformMeshComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<SurfaceDeformMeshComponent>( this );
    }

    void SurfaceDeformMeshComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SurfaceDeformMeshComponent>( this );
        Component::onUnregister( manager );
    }

    void SurfaceDeformMeshComponent::bindMaterial( GameObjectManager& manager )
    {
        using Internal                    = SurfaceDeformMeshComponentInternal;
        const SurfaceDeformSystem& deform = manager.getSubsystem<SurfaceDeformSystem>();
        if ( deform.getTexturePath().empty() || deform.getBindingRevision() == _boundRevision )
            return;
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        Material*      pBase  = pMesh != nullptr ? pMesh->getMaterial() : nullptr;
        // 머티리얼을 아직 읽는 중이면 다음 틱에 다시 본다(메시가 있는데 머티리얼이 없다).
        if ( pMesh != nullptr && pBase == nullptr )
            return;
        if ( pBase == nullptr || pBase->findProperty( Internal::getMapName() ) == nullptr || pBase->findProperty( Internal::getParamsName() ) == nullptr ||
             pBase->findProperty( Internal::getTexelName() ) == nullptr )
        {
            const hashed_string ownerName = pOwner != nullptr ? pOwner->getName() : hashed_string{};
            SW_LOG_ERROR_ONCE_FOR_ID( "Surface.DeformMeshMaterialMissing", string_view( ownerName.c_str() ),
                                      "'%#' has a SurfaceDeformMeshComponent but no mesh whose material has _deformMap, _deformParams and _deformTexel (use a surface_deform.hlsl material)",
                                      ownerName.c_str() );
            _boundRevision = deform.getBindingRevision(); // 같은 바인딩으로 다시 묻지 않는다 — 다음 바인딩이 바뀔 때 다시 본다
            return;
        }
        shared_ptr<MaterialInstance> instance = pMesh->getMaterialInstance();
        if ( instance == nullptr )
        {
            instance = MaterialInstance::create( pBase );
            if ( instance == nullptr )
                return;
            pMesh->setMaterialInstance( instance );
        }
        instance->setTextureParameter( Internal::getMapName(), deform.getTexturePath() );
        instance->setVectorParameter( Internal::getParamsName(), deform.makeShaderParams() );
        instance->setScalarParameter( Internal::getTexelName(), deform.getGrid().getTexelSize() );
        _boundRevision = deform.getBindingRevision();
    }
} // namespace sw
