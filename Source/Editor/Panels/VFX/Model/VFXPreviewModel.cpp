#include "pch.h"

#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/ScenePreviewPolicy.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
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
        , _targetPrefabPath{}
        , _activeTargetPath{}
        , _loadedContentHash{ 0 }
        , _target{}
        , _orbitAngle{ 0.0f }
        , _soloEmitter{ kNoSolo }
        , _bTargetAttached{ SW_FALSE }
        , _bTargetPending{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void VFXPreviewModel::toggleSolo( int32 emitterIndex )
    {
        _soloEmitter = ( emitterIndex < 0 || emitterIndex == _soloEmitter ) ? kNoSolo : emitterIndex;
    }

    bool VFXPreviewModel::computeEmitterEnabled( int32 soloEmitter, uint32 emitterIndex )
    {
        return soloEmitter < 0 || static_cast<uint32>( soloEmitter ) == emitterIndex;
    }

    float3 VFXPreviewModel::computeOrbitPosition( float32 angle )
    {
        return float3{ MathUtil::cos( angle ) * kTargetOrbitRadius, 0.0f, MathUtil::sin( angle ) * kTargetOrbitRadius };
    }

    void VFXPreviewModel::spawnTargetFromField()
    {
        _activeTargetPath = _targetPrefabPath;
        _bTargetPending   = SW_TRUE; // 다음 `syncPlayback` 이 몸을 다시 놓는다(같은 경로를 다시 눌러도 새로 놓는다, 빈 경로면 치운다)
        _orbitAngle       = 0.0f;
    }

    bool VFXPreviewModel::hasTarget( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        return pManager != nullptr && pManager->resolveGameObject( _target ) != nullptr;
    }

    void VFXPreviewModel::syncPlayback( Scene* pScene, float32 frameDeltaSeconds )
    {
        VFXComponent* pSubject = findSubject( pScene );
        if ( pSubject == nullptr )
            return;
        // 격리는 보기 상태다 — 효과 파일(방출기 표)은 그대로이고, 다시 읽어도(자동 갱신) 이 자리가 다시 건다.
        for ( uint32 emitterIndex = 0; emitterIndex < pSubject->getEmitterCount(); ++emitterIndex )
        {
            const bool bEnabled = computeEmitterEnabled( _soloEmitter, emitterIndex );
            if ( pSubject->isEmitterEnabled( emitterIndex ) != bEnabled )
                (void)pSubject->setEmitterEnabled( emitterIndex, bEnabled );
        }
        syncTarget( *pScene, *pSubject );
        // 몸은 씬 시간(재생 막대의 멈춤 · 한 프레임 · 속도)으로 돈다 — 멈추면 트레일과 몸이 함께 선다.
        const SceneManager*       pSceneManager = editor::findSubsystem<SceneManager>();
        const ScenePreviewPolicy* pPolicy       = pSceneManager != nullptr ? pSceneManager->findPreviewPolicy( pScene ) : nullptr;
        _orbitAngle += frameDeltaSeconds * ( pPolicy != nullptr ? pPolicy->_timeScale : 1.0f ) * kTargetOrbitSpeed;
        GameObjectManager* pManager = pScene->getObjectManager();
        GameObject*        pTarget  = pManager != nullptr ? pManager->resolveGameObject( _target ) : nullptr;
        SceneComponent*    pRoot    = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        if ( pRoot != nullptr )
            pRoot->setLocalPosition( computeOrbitPosition( _orbitAngle ) );
    }

    void VFXPreviewModel::syncTarget( Scene& scene, VFXComponent& subject )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pEffect  = subject.getOwner();
        if ( pManager == nullptr || pEffect == nullptr )
            return;
        if ( _bTargetPending == SW_TRUE )
        {
            // 효과를 먼저 떼어 원점에 둔다 — 몸을 지우면 붙은 효과도 함께 사라진다.
            pEffect->detachFromParent( AttachRule::KeepRelative );
            SceneComponent* pEffectRoot = pEffect->getPrimarySceneComponent();
            if ( pEffectRoot != nullptr )
                pEffectRoot->setLocalPosition( float3{} );
            _bTargetAttached = SW_FALSE;
            GameObject* pOld = pManager->resolveGameObject( _target );
            if ( pOld != nullptr )
                pManager->destroyGameObject( pOld );
            _target         = GameObjectHandle{};
            _bTargetPending = SW_FALSE; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치고 Spawn 을 다시 누른다)
            if ( _activeTargetPath.empty() )
            {
                _status = "Target cleared";
                return;
            }
            AssetManager* pAssets  = pManager->findAssetManager();
            GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _activeTargetPath ) : nullptr;
            if ( pSpawned == nullptr )
            {
                _status = string( "Target prefab '" ) + _activeTargetPath + "' could not be spawned - see the log";
                return;
            }
            _target = pSpawned->getHandle();
            _status = "Target spawned";
        }
        GameObject* pTarget = pManager->resolveGameObject( _target );
        if ( pTarget == nullptr || _bTargetAttached == SW_TRUE )
            return;
        // 몸을 따라 움직이는 효과(트레일 · 리본)가 보이게 몸의 자식으로 가슴께에 붙인다.
        if ( pEffect->attachToParent( pTarget, AttachRule::KeepRelative ) == false )
        {
            _status = "The effect could not be attached to the target";
            return;
        }
        SceneComponent* pEffectRoot = pEffect->getPrimarySceneComponent();
        if ( pEffectRoot != nullptr )
            pEffectRoot->setLocalPosition( float3{ 0.0f, kTargetAttachHeight, 0.0f } );
        _bTargetAttached = SW_TRUE;
        SW_LOG_INFO_ID( "VFXPreview.TargetAttached", "VFX Preview attached the effect to target '%#'", _activeTargetPath );
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
            (void)pSubject->addComponent<VFXComponent>();                  // 효과 파일은 문서를 읽은 뒤 건다(syncEffect)
        _previewEffectPath.clear();                                        // 새 씬의 효과에는 아직 효과 파일이 없다
        _bTargetPending  = _activeTargetPath.empty() ? SW_FALSE : SW_TRUE; // 새 씬에는 타깃 몸이 없다 — 확정한 타깃이 있으면 다음 `syncPlayback` 이 다시 놓는다
        _target          = GameObjectHandle{};
        _bTargetAttached = SW_FALSE;
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
