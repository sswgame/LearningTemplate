#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Client/RhythmViewComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/AudioPlayableAsset.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Client/RhythmHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/Rhythm/Component/RhythmGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        struct RhythmViewComponentInternal
        {
            static constexpr float32 kLaneThickness = 0.05f; ///< 레인 바닥의 두께(m)
            static constexpr float32 kNoteHeight    = 0.15f; ///< 노트의 두께(m)
            static constexpr float32 kNoteLength    = 0.35f; ///< 보통 노트의 길이(m)
            static constexpr float32 kNoteGap       = 0.9f;  ///< 노트 폭이 레인 폭에서 차지하는 비율
            static constexpr float32 kLineLength    = 0.15f; ///< 판정선의 길이(m)
        };
    } // namespace

    RhythmViewComponent::RhythmViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&RhythmViewComponent::updateView, &RhythmViewComponent::submitInput>() )
        , _music{}
        , _listLaneAction{}
        , _notePrefab{}
        , _laneWidth{ 1.0f }
        , _hiSpeed{ 8.0f }
        , _viewLength{ 16.0f }
        , _laneColor{ 0.15f, 0.16f, 0.22f, 1.0f }
        , _heldColor{ 0.35f, 0.6f, 1.0f, 1.0f }
        , _noteColor{ 1.0f, 0.8f, 0.25f, 1.0f }
        , _laneViews{}
        , _noteViews{}
        , _hudViewModel{ nullptr }
        , _listLaneHeld{}
        , _musicStart{ 0.0f }
        , _musicPhase{ MusicPhase::Idle }
        , _bMusicPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    RhythmViewComponent::~RhythmViewComponent() = default;

    void RhythmViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _listLaneHeld.assign( _listLaneAction.size(), SW_FALSE );
        _musicPhase = MusicPhase::Idle;
    }

    void RhythmViewComponent::onEndPlay()
    {
        if ( _musicPhase == MusicPhase::Playing )
            GameSound::stopMusic();
        _musicPhase                       = MusicPhase::Idle;
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _laneViews.destroyAll( *pManager );
            _noteViews.destroyAll( *pManager );
        }
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        GameModeViewComponent::onEndPlay();
    }

    void RhythmViewComponent::updateView( GameObjectManager& manager, RhythmGameModeComponent& director )
    {
        updateMusic( director );
        dispatchInput( manager, director );
        showNotes( manager, director );
        updateHUD( director );
    }

    void RhythmViewComponent::updateMusic( RhythmGameModeComponent& director )
    {
        const optional<float32> musicSeconds = GameSound::findMusicSeconds();
        switch ( _musicPhase )
        {
            case MusicPhase::Idle:
            {
                GameSound::stopMusic();
                _musicPhase = MusicPhase::Stopping;
                return;
            }
            case MusicPhase::Stopping:
            {
                // 앞 음악의 위치를 이 곡의 시각으로 읽으면 노트가 한꺼번에 지나간다 — 멈춘 것을 본 뒤에 튼다.
                if ( musicSeconds.has_value() )
                    return;
                const bool bPlaying = GameSound::playAdaptiveMusic( _music.id() );
                if ( bPlaying == false )
                    SW_LOG_ERROR_ID( "Rhythm.MusicNotPlaying", "Rhythm view: the music '%#' could not be played - the song does not advance", _music.c_str() );
                _musicPhase   = bPlaying ? MusicPhase::Playing : MusicPhase::Refused;
                _musicStart   = director.getMusicTime();
                _bMusicPaused = SW_FALSE;
                return;
            }
            case MusicPhase::Playing:
            {
                if ( ( _bMusicPaused == SW_TRUE ) == isTimeRunning() ) // 시간이 멈추거나 다시 흐르기 시작한 프레임이다
                {
                    _bMusicPaused = isTimeRunning() ? SW_FALSE : SW_TRUE;
                    GameSound::setMusicPaused( _bMusicPaused == SW_TRUE );
                }
                if ( musicSeconds.has_value() == false || director.getClock() != RhythmClock::Music )
                    return;                                                    // 오디오 스레드가 아직 시작하지 않았거나, 시뮬레이션 시계의 판이다
                (void)director.submitMusicTime( _musicStart + *musicSeconds ); // 끝난 판은 받지 않는다 — 할 일이 없다
                return;
            }
            case MusicPhase::Refused:
            {
                return;
            }
        }
    }

    void RhythmViewComponent::submitInput( const GameModeInput& input, RhythmGameModeComponent& director )
    {
        for ( size_t laneIndex = 0; laneIndex < _listLaneAction.size(); ++laneIndex )
        {
            const uint8 bHeld = input.isActionDown( _listLaneAction[laneIndex] ) ? SW_TRUE : SW_FALSE;
            if ( bHeld == _listLaneHeld[laneIndex] )
                continue;
            _listLaneHeld[laneIndex] = bHeld;
            // 채보에 없는 레인은 디렉터가 거절한다 — 액션이 레인보다 많은 씬에서 남는 키다.
            if ( bHeld == SW_TRUE )
                (void)director.pressLane( static_cast<int32>( laneIndex ) );
            else
                (void)director.releaseLane( static_cast<int32>( laneIndex ) );
        }
    }

    void RhythmViewComponent::showNotes( GameObjectManager& manager, const RhythmGameModeComponent& director )
    {
        using Internal                     = RhythmViewComponentInternal;
        const RhythmChart* const pChart    = director.findChart(); // 열린 판은 채보가 있다
        const int32              laneCount = pChart->getLaneCount();
        const float32            firstX    = -0.5f * _laneWidth * static_cast<float32>( laneCount - 1 );
        _laneViews.begin();
        for ( int32 lane = 0; lane < laneCount; ++lane )
        {
            const bool    bHeld = static_cast<size_t>( lane ) < _listLaneHeld.size() && _listLaneHeld[static_cast<size_t>( lane )] == SW_TRUE;
            const float32 x     = firstX + _laneWidth * static_cast<float32>( lane );
            (void)_laneViews.show( manager, _notePrefab, AbsolutePosition{
                                                             float3{ x, -Internal::kLaneThickness, _viewLength * 0.5f }
            },
                                   float3{ _laneWidth * Internal::kNoteGap, Internal::kLaneThickness, _viewLength }, 0.0f, bHeld ? _heldColor : _laneColor );
        }
        (void)_laneViews.show( manager, _notePrefab, AbsolutePosition{
                                                         float3{ 0.0f, 0.0f, 0.0f }
        },
                               float3{ _laneWidth * static_cast<float32>( laneCount ), Internal::kLaneThickness, Internal::kLineLength }, 0.0f, _heldColor );
        _laneViews.end( manager );

        // 노트는 시각 순이라 화면 위로 벗어난 첫 노트에서 멈춘다. 지나간 노트는 판정선 아래로 잠깐 보이고 사라진다.
        _noteViews.begin();
        const float32 now = director.getMusicTime();
        for ( const RhythmNote& note : pChart->getNotes() )
        {
            const float32 startZ = ( note._time - now ) * _hiSpeed;
            if ( startZ > _viewLength )
                break;
            const float32 endZ = note._bLong == SW_TRUE ? MathUtil::min( ( note._endTime - now ) * _hiSpeed, _viewLength ) : startZ + Internal::kNoteLength;
            if ( endZ < 0.0f )
                continue;
            const float32 fromZ = MathUtil::max( startZ, 0.0f );
            const float32 x     = firstX + _laneWidth * static_cast<float32>( note._lane );
            (void)_noteViews.show( manager, _notePrefab, AbsolutePosition{
                                                             float3{ x, Internal::kNoteHeight * 0.5f, ( fromZ + endZ ) * 0.5f }
            },
                                   float3{ _laneWidth * Internal::kNoteGap, Internal::kNoteHeight, MathUtil::max( endZ - fromZ, Internal::kLineLength ) }, 0.0f, _noteColor );
        }
        _noteViews.end( manager );
    }

    void RhythmViewComponent::updateHUD( const RhythmGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<RhythmHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const RhythmPlaySession& session = director.getSession();
        _hudViewModel->setScore( director.getScore() );
        _hudViewModel->setCombo( session.getCombo() );
        _hudViewModel->setAccuracy( static_cast<int32>( session.computeAccuracy() ) );
        _hudViewModel->setLifeRatio( session.getLife() / director.getMaxLife() );
        _hudViewModel->setClearedShown( session.getState() == RhythmPlayState::Cleared );
        _hudViewModel->setFailedShown( session.getState() == RhythmPlayState::Failed );
    }
} // namespace sw
