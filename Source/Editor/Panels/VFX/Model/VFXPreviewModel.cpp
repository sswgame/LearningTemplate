#include "pch.h"

#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
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
    VFXPreviewModel::VFXPreviewModel()
        : _listEmitterRow{}
        , _previewEffectPath{}
        , _status{}
        , _loadedDiskPath{}
        , _loadedContentHash{ 0 }
    {
    }

    ToolAssetLoadResult VFXPreviewModel::loadAsset( string_view assetPath )
    {
        _listEmitterRow.clear();
        _previewEffectPath.clear();
        // 자동 갱신이 볼 내용 해시 — 리소스 id 면 디스크 경로로 푼다(시나리오의 임시 문서는 이미 디스크 경로다).
        const optional<AbsolutePath> diskPath = AbsolutePath::isStrictAbsolute( assetPath ) ? AbsolutePath::tryMake( assetPath )
                                                                                            : optional<AbsolutePath>{ editor::getSubsystem<AssetPaths>().toDiskPath( assetPath ) };
        _loadedDiskPath                       = diskPath.has_value() ? diskPath->str() : string{};
        _loadedContentHash                    = 0;
        if ( _loadedDiskPath.empty() == false )
            (void)updateContentHash( _loadedDiskPath, _loadedContentHash );
        // 미리 보기의 표는 씬의 효과와 같은 한 벌을 읽는다(공유 캐시 — 핫 리로드가 떼어 낸 새 객체도 다시 열면 받는다).
        const shared_ptr<const VFXEffectDef> pEffect = SharedAssetCache<VFXEffectDef>::acquire( assetPath );
        if ( pEffect == nullptr )
        {
            _status = "The effect could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        for ( const VFXEmitterDef& emitter : pEffect->_listEmitter )
        {
            VFXPreviewEmitterRow& row = _listEmitterRow.emplace_back();
            row._name                 = emitter._name;
            row._texturePath          = emitter._texturePath.path();
            row._maxParticles         = emitter._maxParticles;
            row._bAdditive            = emitter._blendMode == VFXBlendMode::Additive;
            row._bLooping             = emitter._bLooping;
        }
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    bool VFXPreviewModel::pollFileChanged()
    {
        return _loadedDiskPath.empty() == false && updateContentHash( _loadedDiskPath, _loadedContentHash );
    }

    bool VFXPreviewModel::updateContentHash( string_view diskPath, uint64& inoutHash )
    {
        const uint64 hash = ContentHash::computeFile( diskPath );
        if ( hash == 0 || hash == inoutHash )
            return false;
        inoutHash = hash;
        return true;
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
        SW_LOG_INFO_ID( "VFXPreview.Playing", "VFX Preview playing '%#' (%# emitters)", _previewEffectPath, pVFX->getEmitterCount() );
    }

    void VFXPreviewModel::applySavedAsset( string_view assetPath )
    {
        editor::getSubsystem<AssetHotReload>().applySavedAsset( assetPath );
    }
} // namespace sw::editor
