#include "pch.h"

#include "Engine/Object/Component/3D/MeshSectionComponent.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "MeshSectionComponent" );

    void MeshSectionComponent::resolveRenderAssets()
    {
        resolveMaterialAsset();
        followSourceMesh();
    }

    MeshComponent* MeshSectionComponent::findSourceMeshComponent() const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return nullptr;
        MeshComponent* pSource = nullptr;
        pOwner->forEachComponentOfType<MeshComponent>( [&pSource]( MeshComponent* pMesh )
        {
            const bool bSection = castTo<MeshSectionComponent>( pMesh ) != nullptr;
            if ( pSource == nullptr && bSection == false )
                pSource = pMesh;
        } );
        return pSource;
    }

    void MeshSectionComponent::followSourceMesh()
    {
        const MeshComponent* pSource = findSourceMeshComponent();
        if ( pSource == nullptr )
        {
            if ( getOwner() != nullptr )
                SW_LOG_ERROR( "Mesh section '%#' has no main mesh component on its object - it draws nothing", getSectionName().c_str() );
            assignMeshWithoutFollowers( nullptr );
            return;
        }
        assignMeshWithoutFollowers( pSource->getMesh() );
    }
} // namespace sw
