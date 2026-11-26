#include "pch.h"

#include "Editor/Panels/VFX/Model/VFXPreviewViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/AssetWriter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Panels/Catalog/Model/ReflectedXMLDocument.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
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
        struct VFXPreviewViewModelInternal
        {
            /// @brief 미리보기 씬에 스폰하는 효과 오브젝트 이름입니다.
            static constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
            /// @brief 효과 문서의 방출기 목록 칸과 방출기의 열쇠 칸입니다(`VFXEffectDef::_listEmitter` · `VFXEmitterDef::_name`).
            static constexpr string_view kEmitterList = "_listEmitter";
            static constexpr string_view kEmitterKey  = "_name";

            static void fillRows( const VFXEffectDef& effect, vector<VFXPreviewEmitterRow>& outListRow )
            {
                outListRow.clear();
                for ( const VFXEmitterDef& emitter : effect._listEmitter )
                {
                    VFXPreviewEmitterRow& row = outListRow.emplace_back();
                    row._name                 = emitter._name;
                    row._texturePath          = emitter._texturePath.path();
                    row._maxParticles         = emitter._maxParticles;
                    row._bAdditive            = emitter._blendMode == VFXBlendMode::Additive;
                    row._bLooping             = emitter._bLooping;
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    VFXPreviewViewModel::VFXPreviewViewModel()
        : _pDocument{ make_unique<ReflectedXMLDocument>() }
        , _selectedEmitter{}
        , _listEmitterRow{}
        , _previewEffectPath{}
        , _previewText{}
        , _status{}
        , _loadedDiskPath{}
        , _targetPrefabPath{}
        , _activeTargetPath{}
        , _loadedContentHash{ 0 }
        , _seenContentSerial{ 0 }
        , _target{}
        , _orbitAngle{ 0.0f }
        , _soloEmitter{ kNoSolo }
        , _bTargetAttached{ SW_FALSE }
        , _bTargetPending{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    VFXPreviewViewModel::~VFXPreviewViewModel() = default;

    void VFXPreviewViewModel::bindDocumentEditRecorder( const DocumentEditRecorder& recorder )
    {
        _pDocument->bindDocumentEditRecorder( recorder );
    }

    string VFXPreviewViewModel::captureText() const
    {
        return _pDocument->getText();
    }

    void VFXPreviewViewModel::applyText( string_view text )
    {
        _pDocument->applyText( text );
        refreshEmitterRows();
    }

    bool VFXPreviewViewModel::saveDocument( AssetWriter& writer )
    {
        if ( _loadedDiskPath.empty() || writer.writeText( _loadedDiskPath, _pDocument->getText() ) == false )
        {
            _status = "The effect could not be saved - see the log";
            return false;
        }
        _pDocument->markSaved();
        (void)updateContentHash( _loadedDiskPath, _loadedContentHash ); // 내가 쓴 글이다 — 자동 갱신이 문서를 다시 읽어 되돌리기를 비우지 않게
        requestResync();                                                // 저장 뒤 공유 캐시가 다시 읽은 효과를 미리보기에 다시 건다
        _status = "Saved";
        return true;
    }

    bool VFXPreviewViewModel::isEditable() const
    {
        return _pDocument->isEditable();
    }

    const string& VFXPreviewViewModel::getBaseID() const
    {
        return _pDocument->getBaseID();
    }

    vector<hashed_string> VFXPreviewViewModel::collectHiddenProperties() const
    {
        const optional<uint32> selected = getSelectedEmitter();
        vector<hashed_string>  listHidden;
        if ( selected.has_value() && _pDocument->isInheritedItem( VFXPreviewViewModelInternal::kEmitterList, *selected ) )
            listHidden.push_back( hashed_string( VFXPreviewViewModelInternal::kEmitterKey ) );
        return listHidden;
    }

    optional<uint32> VFXPreviewViewModel::getSelectedEmitter() const
    {
        return _selectedEmitter.has_value() && *_selectedEmitter < _listEmitterRow.size() ? _selectedEmitter : nullopt;
    }

    void* VFXPreviewViewModel::editSelectedEmitter()
    {
        const optional<uint32> selected = getSelectedEmitter();
        VFXEffectDef*          pEffect  = static_cast<VFXEffectDef*>( _pDocument->getInstance() );
        return pEffect != nullptr && selected.has_value() && *selected < pEffect->_listEmitter.size() ? &pEffect->_listEmitter[*selected] : nullptr;
    }

    const TypeInfo* VFXPreviewViewModel::getEmitterType()
    {
        return VFXEmitterDef::StaticType();
    }

    void VFXPreviewViewModel::commitEmitterProperty( const PropertyInfo& property )
    {
        const void* pEmitter = editSelectedEmitter();
        if ( pEmitter == nullptr )
            return;
        (void)_pDocument->commitItemProperty( VFXPreviewViewModelInternal::kEmitterList, *getSelectedEmitter(), pEmitter, *getEmitterType(), property ); // 같은 글이면 바뀐 것이 없다
        refreshEmitterRows();
    }

    void VFXPreviewViewModel::duplicateSelectedEmitter()
    {
        const optional<uint32> selected = getSelectedEmitter();
        if ( selected.has_value() == false || _pDocument->duplicateItem( VFXPreviewViewModelInternal::kEmitterList, *selected, VFXPreviewViewModelInternal::kEmitterKey ) == false )
            return;
        _selectedEmitter = *selected + 1;
        refreshEmitterRows();
    }

    void VFXPreviewViewModel::removeSelectedEmitter()
    {
        const optional<uint32> selected = getSelectedEmitter();
        if ( selected.has_value() == false || _listEmitterRow.size() < 2 )
            return;
        if ( _pDocument->removeItem( VFXPreviewViewModelInternal::kEmitterList, *selected ) == false )
            return;
        _selectedEmitter = nullopt;
        refreshEmitterRows();
    }

    void VFXPreviewViewModel::refreshEmitterRows()
    {
        const VFXEffectDef* pEffect = static_cast<const VFXEffectDef*>( _pDocument->getInstance() );
        if ( pEffect != nullptr )
            VFXPreviewViewModelInternal::fillRows( *pEffect, _listEmitterRow );
    }

    void VFXPreviewViewModel::toggleSolo( int32 emitterIndex )
    {
        _soloEmitter = ( emitterIndex < 0 || emitterIndex == _soloEmitter ) ? kNoSolo : emitterIndex;
    }

    bool VFXPreviewViewModel::computeEmitterEnabled( int32 soloEmitter, uint32 emitterIndex )
    {
        return soloEmitter < 0 || static_cast<uint32>( soloEmitter ) == emitterIndex;
    }

    float3 VFXPreviewViewModel::computeOrbitPosition( float32 angle )
    {
        return float3{ MathUtil::cos( angle ) * kTargetOrbitRadius, 0.0f, MathUtil::sin( angle ) * kTargetOrbitRadius };
    }

    void VFXPreviewViewModel::spawnTargetFromField()
    {
        _activeTargetPath = _targetPrefabPath;
        _bTargetPending   = SW_TRUE; // 다음 `syncPlayback` 이 몸을 다시 놓는다(같은 경로를 다시 눌러도 새로 놓는다, 빈 경로면 치운다)
        _orbitAngle       = 0.0f;
    }

    bool VFXPreviewViewModel::hasTarget( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        return pManager != nullptr && pManager->resolveGameObject( _target ) != nullptr;
    }

    void VFXPreviewViewModel::syncPlayback( Scene* pScene, float32 frameDeltaSeconds )
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

    void VFXPreviewViewModel::syncTarget( Scene& scene, VFXComponent& subject )
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
            GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _activeTargetPath, SpawnLifetime::Persistent ) : nullptr;
            if ( pSpawned == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> statusBuilder;
                statusBuilder.appendFormat( "Target prefab '%#' could not be spawned - see the log", _activeTargetPath );
                _status = string( statusBuilder.view() );
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

    ToolAssetLoadResult VFXPreviewViewModel::loadAsset( string_view assetPath )
    {
        _listEmitterRow.clear();
        _previewEffectPath.clear();
        // 자동 갱신이 볼 내용 해시 — 리소스 id 면 디스크 경로로 푼다(시나리오의 임시 문서는 이미 디스크 경로다).
        const optional<DiskPath> diskPath = AbsolutePath::isStrictAbsolute( assetPath ) ? optional<DiskPath>{ FileUtil::toDiskPath( assetPath ) }
                                                                                        : editor::getSubsystem<AssetPaths>().toDiskPath( assetPath );
        _loadedDiskPath                   = diskPath.has_value() ? *diskPath : DiskPath{};
        _loadedContentHash                = 0;
        _seenContentSerial                = editor::getSubsystem<AssetHotReload>().getContentChangeSerial();
        if ( _loadedDiskPath.empty() == false )
            (void)updateContentHash( _loadedDiskPath, _loadedContentHash );
        // 미리 보기의 표는 씬의 효과와 같은 한 벌을 읽는다(공유 캐시 — 핫 리로드가 떼어 낸 새 객체도 다시 열면 받는다).
        const shared_ptr<const VFXEffectDef> pEffect = SharedAssetCache<VFXEffectDef>::acquire( assetPath ).snapshot();
        if ( pEffect == nullptr )
        {
            _status = "The effect could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        VFXPreviewViewModelInternal::fillRows( *pEffect, _listEmitterRow );
        // 편집 사본 — 읽지 못하면 공유 캐시가 이미 거절했다.
        string text;
        _pDocument->clear();
        if ( _loadedDiskPath.empty() == false && FileUtil::readTextFile( _loadedDiskPath, text ) )
        {
            const expected<void> opened = _pDocument->load( *VFXEffectDef::StaticType(), text, assetPath );
            if ( opened.has_value() == false )
                SW_LOG_WARNING_ID( "VFXPreview.EditCopyNotRead", "VFX Preview cannot edit '%#': %# (%#)", assetPath, opened.error().message(), opened.error().detail() );
        }
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    bool VFXPreviewViewModel::pollFileChanged()
    {
        const uint64 serial = editor::getSubsystem<AssetHotReload>().getContentChangeSerial();
        if ( _loadedDiskPath.empty() || serial == _seenContentSerial )
            return false; // 감시가 아무 변경도 알리지 않았다
        _seenContentSerial = serial;
        return updateContentHash( _loadedDiskPath, _loadedContentHash );
    }

    bool VFXPreviewViewModel::updateContentHash( const DiskPath& diskPath, uint64& inoutHash )
    {
        const uint64 hash = ContentHash::computeFile( diskPath );
        if ( hash == 0 || hash == inoutHash )
            return false;
        inoutHash = hash;
        return true;
    }

    void VFXPreviewViewModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( VFXPreviewViewModelInternal::kPreviewSubjectName ) ) : nullptr;
        if ( pSubject != nullptr )
            (void)pSubject->addComponent<VFXComponent>();                  // 효과 파일은 문서를 읽은 뒤 건다(syncEffect)
        _previewEffectPath.clear();                                        // 새 씬의 효과에는 아직 효과 파일이 없다
        _bTargetPending  = _activeTargetPath.empty() ? SW_FALSE : SW_TRUE; // 새 씬에는 타깃 몸이 없다 — 확정한 타깃이 있으면 다음 `syncPlayback` 이 다시 놓는다
        _target          = GameObjectHandle{};
        _bTargetAttached = SW_FALSE;
    }

    VFXComponent* VFXPreviewViewModel::findSubject( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( VFXPreviewViewModelInternal::kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<VFXComponent>() : nullptr;
    }

    bool VFXPreviewViewModel::findSubjectStats( Scene* pScene, VFXPreviewSubjectStats& outStats )
    {
        const VFXComponent* pSubject = findSubject( pScene );
        if ( pSubject == nullptr )
            return false;
        outStats._spawnedCount        = pSubject->getSpawnedParticleCount();
        outStats._cpuAliveCount       = pSubject->getCPUAliveParticleCount();
        outStats._enabledEmitterCount = 0;
        for ( uint32 emitterIndex = 0; emitterIndex < pSubject->getEmitterCount(); ++emitterIndex )
        {
            if ( pSubject->isEmitterEnabled( emitterIndex ) )
                ++outStats._enabledEmitterCount;
        }
        return true;
    }

    void VFXPreviewViewModel::restartSubject( Scene* pScene )
    {
        if ( VFXComponent* pSubject = findSubject( pScene ) )
            pSubject->play();
    }

    void VFXPreviewViewModel::stopSubject( Scene* pScene )
    {
        if ( VFXComponent* pSubject = findSubject( pScene ) )
            pSubject->stop();
    }

    void VFXPreviewViewModel::syncEffect( Scene* pScene, string_view assetPath, bool bInteracting )
    {
        const bool        bEdited  = _pDocument->isEditable() && _pDocument->isDirty();
        const string_view editText = bEdited ? string_view{ _pDocument->getText() } : string_view{};
        const bool        bSame    = string_view{ _previewEffectPath } == assetPath;
        if ( bSame && ( bInteracting || string_view{ _previewText } == editText ) )
            return;
        VFXComponent* pVFX = findSubject( pScene );
        if ( pVFX == nullptr )
            return;
        _previewEffectPath = assetPath;
        _previewText       = string( editText ); // 틀린 글도 적어 둔다 — 고칠 때까지 프레임마다 다시 읽지 않는다
        // 미리보기 씬은 플레이하지 않는다(틱만) — 재생은 이 창이 시작한다.
        if ( bEdited )
        {
            const shared_ptr<const VFXEffectDef> pEdited = VFXEffectDef::makeFromXMLText( editText, assetPath );
            if ( pEdited == nullptr )
            {
                _status = "The edited effect has an invalid value - the preview keeps the last valid one (see the log)";
                return;
            }
            pVFX->setEffect( pEdited );
            pVFX->play();
            SW_LOG_INFO_ID( "VFXPreview.PlayingEdit", "VFX Preview playing the unsaved edit of '%#' (%# emitters)", _previewEffectPath, pVFX->getEmitterCount() );
            return;
        }
        pVFX->setEffectPath( assetPath );
        pVFX->play();
        SW_LOG_INFO_ID( "VFXPreview.Playing", "VFX Preview playing '%#' (%# emitters)", _previewEffectPath, pVFX->getEmitterCount() );
    }

    void VFXPreviewViewModel::applySavedAsset( string_view assetPath )
    {
        editor::getSubsystem<AssetHotReload>().applySavedAsset( assetPath );
    }
} // namespace sw::editor
