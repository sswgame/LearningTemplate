#include "pch.h"

#include "Editor/Panels/Mesh/Model/MeshPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct MeshPreviewModelInternal
        {
            /// @brief 미리보기 씬에 스폰하는 메시 오브젝트 이름입니다.
            static constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
            /// @brief 경계 구 반지름에 곱해 화면 가장자리에 붙지 않게 하는 여유 비율입니다.
            static constexpr float32 kFrameMargin = 1.15f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "MeshPreviewPanel" );

    MeshPreviewModel::MeshPreviewModel()
        : _previewMeshPath{}
        , _status{}
        , _lodChoice{ kAutoLOD }
        , _bFramed{ false }
    {
    }

    void MeshPreviewModel::resetForDocument()
    {
        // 메시는 미리보기 메시 컴포넌트가 읽는다(비동기) — 읽기 실패는 컴포넌트가 로그로 남기고 표가 알린다.
        _previewMeshPath.clear();
        _bFramed = false;
        _status  = "Loaded";
    }

    void MeshPreviewModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( MeshPreviewModelInternal::kPreviewSubjectName ) ) : nullptr;
        if ( pSubject != nullptr )
            (void)pSubject->addComponent<MeshComponent>(); // 메시 경로는 문서를 읽은 뒤 건다(syncSubject)
        _previewMeshPath.clear();                          // 새 씬의 메시에는 아직 경로가 없다
        _bFramed = false;
    }

    MeshComponent* MeshPreviewModel::findSubject( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( MeshPreviewModelInternal::kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<MeshComponent>() : nullptr;
    }

    bool MeshPreviewModel::syncSubject( Scene* pScene, string_view assetPath, float3& outCenter, float32& outRadius )
    {
        MeshComponent* pMesh = findSubject( pScene );
        if ( pMesh == nullptr )
            return false;
        if ( string_view{ _previewMeshPath } != assetPath )
        {
            pMesh->setMeshID( assetPath );
            _previewMeshPath = assetPath;
            _bFramed         = false;
            SW_LOG_INFO( "Mesh Preview showing '%#'", _previewMeshPath );
        }
        if ( _bFramed )
            return false;
        const Mesh* pLoaded = pMesh->getRawMesh();
        if ( pLoaded == nullptr )
        {
            if ( pMesh->isMeshLoading() == false )
                _status = "The mesh could not be read - see the log";
            return false; // 읽는 중 — 읽힌 뒤 한 번 카메라를 맞춘다
        }
        const float3& boundsMin = pLoaded->getLocalBoundsMin();
        const float3& boundsMax = pLoaded->getLocalBoundsMax();
        outCenter               = float3{ ( boundsMin._x + boundsMax._x ) * 0.5f, ( boundsMin._y + boundsMax._y ) * 0.5f, ( boundsMin._z + boundsMax._z ) * 0.5f };
        outRadius               = MathUtil::max( pLoaded->getBoundingRadius(), 0.01f ) * MeshPreviewModelInternal::kFrameMargin;
        _bFramed                = true;
        _status                 = "Loaded";
        return true;
    }

    void MeshPreviewModel::applyLODChoice( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        pManager->getSubsystem<MeshLODSystem>().setForcedLOD( _lodChoice );
    }
} // namespace sw::editor
