#include "pch.h"

#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"

namespace sw::editor
{
    namespace
    {
        struct VFXPreviewModelInternal
        {
            /// @brief 미리보기 씬에 스폰하는 효과 오브젝트 이름입니다.
            static constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "VFXPreviewPanel" );

    VFXPreviewModel::VFXPreviewModel()
        : _listEmitterRow{}
        , _previewEffectPath{}
        , _status{}
    {
    }

    ToolAssetLoadResult VFXPreviewModel::loadAsset( string_view assetPath )
    {
        _listEmitterRow.clear();
        _previewEffectPath.clear();
        VFXEffectDef effect;
        if ( effect.loadFromAsset( AssetID{ assetPath } ) == false )
        {
            _status = "The effect could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        for ( const VFXEmitterDef& emitter : effect._listEmitter )
        {
            VFXPreviewEmitterRow& row = _listEmitterRow.emplace_back();
            row._name                 = emitter._name;
            row._texturePath          = emitter._texturePath;
            row._maxParticles         = emitter._maxParticles;
            row._bAdditive            = emitter._blendMode == VFXBlendMode::Additive;
            row._bLooping             = emitter._bLooping;
        }
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void VFXPreviewModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( VFXPreviewModelInternal::kPreviewSubjectName ) ) : nullptr;
        if ( pSubject != nullptr )
            (void)pSubject->addComponent<VFXComponent>(); // 효과 파일은 문서를 읽은 뒤 건다(syncEffect)
        _previewEffectPath.clear();                       // 새 씬의 효과에는 아직 효과 파일이 없다
    }

    VFXComponent* VFXPreviewModel::findSubject( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( VFXPreviewModelInternal::kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<VFXComponent>() : nullptr;
    }

    void VFXPreviewModel::syncEffect( Scene* pScene, string_view assetPath )
    {
        if ( string_view{ _previewEffectPath } == assetPath )
            return;
        VFXComponent* pVFX = findSubject( pScene );
        if ( pVFX == nullptr )
            return;
        // 미리보기 씬은 플레이하지 않는다(틱만) — 재생은 이 창이 시작한다.
        pVFX->setEffectPath( assetPath );
        pVFX->play();
        _previewEffectPath = assetPath;
        SW_LOG_INFO( "VFX Preview playing '%#' (%# emitters)", _previewEffectPath, pVFX->getEmitterCount() );
    }
} // namespace sw::editor
