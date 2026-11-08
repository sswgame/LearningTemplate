#include "pch.h"

#include "Editor/Panels/Mesh/Model/MeshPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/MeshLODSystem.h"

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
    MeshPreviewModel::MeshPreviewModel()
        : _previewMeshPath{}
        , _status{}
        , _lodChoice{ kAutoLOD }
        , _bFramed{ SW_FALSE }
    {
    }

    void MeshPreviewModel::resetForDocument()
    {
        // 메시는 미리보기 메시 컴포넌트가 읽는다(비동기) — 읽기 실패는 컴포넌트가 로그로 남기고 표가 알린다.
        _previewMeshPath.clear();
        _bFramed = SW_FALSE;
        _status  = "Loaded";
    }

    void MeshPreviewModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( MeshPreviewModelInternal::kPreviewSubjectName ) ) : nullptr;
        if ( pSubject != nullptr )
            (void)pSubject->addComponent<MeshComponent>(); // 메시 경로는 문서를 읽은 뒤 건다(syncSubject)
        _previewMeshPath.clear();                          // 새 씬의 메시에는 아직 경로가 없다
        _bFramed = SW_FALSE;
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
            _bFramed         = SW_FALSE;
            SW_LOG_INFO_ID( "MeshPreviewPanel.MeshPreviewShowing", "Mesh Preview showing '%#'", _previewMeshPath );
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
        _bFramed                = SW_TRUE;
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

    void MeshPreviewModel::gatherLOD( Scene* pScene, MeshPreviewLODState& outState )
    {
        outState._listLOD.clear();
        outState._lodCount                  = 0;
        outState._drawnLOD                  = 0;
        outState._vertexCount               = 0;
        outState._triangleCount             = 0;
        outState._bMeshRead                 = SW_FALSE;
        const MeshComponent* const pSubject = findSubject( pScene );
        outState._bHasSubject               = pSubject != nullptr ? SW_TRUE : SW_FALSE;
        outState._bLoading                  = pSubject != nullptr && pSubject->isMeshLoading() ? SW_TRUE : SW_FALSE;
        const Mesh* const pMesh             = pSubject != nullptr ? pSubject->getRawMesh() : nullptr;
        if ( pMesh == nullptr )
            return;
        outState._bMeshRead     = SW_TRUE;
        outState._lodCount      = pMesh->getLODCount();
        outState._drawnLOD      = static_cast<uint32>( pSubject->getLODIndex() );
        outState._vertexCount   = pMesh->getVertexCount();
        outState._triangleCount = static_cast<uint32>( pMesh->getIndexCount() / 3u );
        for ( const MeshLOD& level : pMesh->getLODs() )
        {
            MeshPreviewLODRow& row = outState._listLOD.emplace_back();
            row._vertexCount       = static_cast<uint32>( level._vertexCount );
            row._triangleCount     = static_cast<uint32>( level._indexCount / 3u );
            row._screenSize        = static_cast<float32>( level._screenSize );
        }
    }
} // namespace sw::editor
