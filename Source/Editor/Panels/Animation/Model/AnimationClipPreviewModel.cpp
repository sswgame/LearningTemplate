#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationClipPreviewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    AnimationClipPreviewModel::AnimationClipPreviewModel()
        : _bodyPrefabPath{}
        , _activePrefabPath{}
        , _spawnedPrefabPath{}
        , _playedClipPath{}
        , _status{}
        , _body{}
        , _clipSeconds{ 0.0f }
        , _bLoop{ SW_TRUE }
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

    const AnimationClip* AnimationClipPreviewModel::findOpenClip( const SkeletalAnimatorComponent& animator, string_view clipPath )
    {
        return animator.findClip( hashed_string( FileUtil::removeExtension( clipPath ) ) ); // '/' 가 든 이름은 파일 경로다(`SkeletalAnimatorComponent::makeClipPath`)
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
                pManager->destroyGameObject( pOld );
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
        // 클립은 공유 저장소에 키 이름으로 있어 폴더가 묶음이 아니다 — 고른 파일 하나를 경로로 재생한다.
        const string         stem = FileUtil::removeExtension( FileUtil::getFileNamePart( clipPath ) );
        const hashed_string  clipName{ FileUtil::removeExtension( clipPath ) };
        const AnimationClip* pClip = animator.preloadClip( clipName ) ? animator.findClip( clipName ) : nullptr;
        if ( pClip == nullptr )
        {
            _status = string( "The clip '" ) + stem + "' could not be read - see the log";
            return;
        }
        if ( animator.play( clipName, _bLoop, 0.0f ) == false )
        {
            _status = string( "The body could not play the clip '" ) + stem + "' - see the log";
            return;
        }
        _clipSeconds = pClip->getDuration();
        _status      = "Playing";
        SW_LOG_INFO_ID( "AnimationClipPreviewPanel.ClipPreviewPlaying", "Clip Preview playing '%#' on '%#'", _playedClipPath, _activePrefabPath );
    }

    bool AnimationClipPreviewModel::gatherClipInfo( Scene* pScene, string_view clipPath, AnimationClipPreviewInfo& outInfo ) const
    {
        const SkeletalAnimatorComponent* const pAnimator = findAnimator( pScene );
        const AnimationClip* const             pClip     = pAnimator != nullptr ? findOpenClip( *pAnimator, clipPath ) : nullptr;
        if ( pClip == nullptr )
            return false;
        outInfo._durationSeconds   = pClip->getDuration();
        outInfo._trackCount        = static_cast<uint32>( pClip->getTrackCount() );
        outInfo._curveCount        = static_cast<uint32>( pClip->getCurves().size() );
        outInfo._bLoopingByDefault = pClip->isLoopingByDefault() ? SW_TRUE : SW_FALSE;
        outInfo._bHasRootMotion    = pClip->getRootMotionTrack() >= 0 ? SW_TRUE : SW_FALSE;
        outInfo._bHasNotifyTrack   = pClip->findNotifyTrack() != nullptr ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool AnimationClipPreviewModel::readPlayTime( Scene* pScene, float32& outSeconds ) const
    {
        const SkeletalAnimatorComponent* const pAnimator = findAnimator( pScene );
        if ( pAnimator == nullptr || _clipSeconds <= 0.0f )
            return false;
        outSeconds = MathUtil::fmod( pAnimator->getStateTime(), _clipSeconds ); // 반복 재생은 상태 시간이 길이를 넘는다
        return true;
    }

    void AnimationClipPreviewModel::seek( Scene* pScene, float32 seconds ) const
    {
        SkeletalAnimatorComponent* const pAnimator = findAnimator( pScene );
        if ( pAnimator != nullptr )
            (void)pAnimator->seekCurrentTime( MathUtil::clamp( seconds, 0.0f, _clipSeconds ) ); // 범위 밖이면 애니메이터가 알린다
    }
} // namespace sw::editor
