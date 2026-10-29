#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationClipPreviewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    SW_LOG_CALLER( "AnimationClipPreviewPanel" );

    AnimationClipPreviewModel::AnimationClipPreviewModel()
        : _bodyPrefabPath{}
        , _activePrefabPath{}
        , _spawnedPrefabPath{}
        , _playedClipPath{}
        , _status{}
        , _body{}
        , _clipSeconds{ 0.0f }
        , _playRate{ 1.0f }
        , _bLoop{ true }
        , _bPaused{ false }
    {
    }

    void AnimationClipPreviewModel::resetForDocument()
    {
        // 클립은 몸의 애니메이터가 읽는다 — 읽기 실패는 playClip 이 상태 줄로 알린다. 다른 클립을 열었으니 몸에 다시 걸게 한다.
        _playedClipPath.clear();
        _clipSeconds = 0.0f;
        _status      = "Loaded";
    }

    void AnimationClipPreviewModel::resetForNewScene()
    {
        _spawnedPrefabPath.clear(); // 새 씬에는 몸이 없다
        _playedClipPath.clear();
        _body = GameObjectHandle{};
    }

    void AnimationClipPreviewModel::spawnFromField()
    {
        _activePrefabPath = _bodyPrefabPath;
        _spawnedPrefabPath.clear(); // 다음 그리기가 몸을 다시 놓는다
    }

    SkeletalAnimatorComponent* AnimationClipPreviewModel::findAnimator( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pBody    = pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
        return pBody != nullptr ? pBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
    }

    void AnimationClipPreviewModel::applyPlayRate( SkeletalAnimatorComponent& animator ) const
    {
        animator.setPlayRate( _bPaused ? 0.0f : _playRate );
    }

    const AnimationClip* AnimationClipPreviewModel::findOpenClip( const SkeletalAnimatorComponent& animator, string_view clipPath )
    {
        string_view directory;
        string_view fileName;
        FileUtil::splitPath( clipPath, directory, fileName );
        string_view stem;
        FileUtil::removeExtension( fileName, stem );
        return animator.findClip( hashed_string( stem ) );
    }

    void AnimationClipPreviewModel::syncBody( Scene* pScene, string_view clipPath )
    {
        if ( pScene == nullptr || _activePrefabPath.empty() )
            return;
        GameObjectManager* pManager = pScene->getObjectManager();
        if ( pManager == nullptr )
            return;
        if ( _spawnedPrefabPath != _activePrefabPath )
        {
            GameObject* pOld = pManager->resolveGameObject( _body );
            if ( pOld != nullptr )
                pManager->destroyObject( pOld );
            _body        = GameObjectHandle{};
            _clipSeconds = 0.0f;
            _playedClipPath.clear();
            _spawnedPrefabPath     = _activePrefabPath; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치고 Spawn 을 다시 누른다)
            AssetManager* pAssets  = pManager->findAssetManager();
            GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _activePrefabPath ) : nullptr;
            if ( pSpawned == nullptr )
            {
                _status = string( "Prefab '" ) + _activePrefabPath + "' could not be spawned - see the log";
                return;
            }
            _body = pSpawned->getHandle();
        }

        SkeletalAnimatorComponent* pAnimator = findAnimator( pScene );
        if ( pAnimator == nullptr )
        {
            if ( pManager->resolveGameObject( _body ) != nullptr )
                _status = string( "Prefab '" ) + _activePrefabPath + "' has no skeletal animator on its root - the clip cannot play";
            return;
        }
        if ( string_view{ _playedClipPath } != clipPath )
            playClip( *pAnimator, clipPath );
    }

    void AnimationClipPreviewModel::playClip( SkeletalAnimatorComponent& animator, string_view clipPath )
    {
        _playedClipPath = clipPath; // 실패해도 같은 클립을 매 프레임 다시 읽지 않는다(Restart 나 다른 클립)
        _clipSeconds    = 0.0f;
        string_view directory;
        string_view fileName;
        FileUtil::splitPath( clipPath, directory, fileName );
        string_view stem;
        FileUtil::removeExtension( fileName, stem );
        while ( directory.empty() == false && directory.back() == '/' )
        {
            directory.remove_suffix( 1 );
        }

        const hashed_string clipName{ stem };
        animator.setClipFolder( directory );
        const AnimationClip* pClip = animator.preloadClip( clipName ) ? animator.findClip( clipName ) : nullptr;
        if ( pClip == nullptr )
        {
            _status = string( "The clip '" ) + string{ stem } + "' could not be read from its folder - see the log";
            return;
        }
        if ( animator.play( clipName, _bLoop, 0.0f ) == false )
        {
            _status = string( "The body could not play the clip '" ) + string{ stem } + "' - see the log";
            return;
        }
        _clipSeconds = pClip->getDuration();
        _status      = "Playing";
        SW_LOG_INFO( "Clip Preview playing '%#' on '%#'", _playedClipPath, _activePrefabPath );
    }
} // namespace sw::editor
