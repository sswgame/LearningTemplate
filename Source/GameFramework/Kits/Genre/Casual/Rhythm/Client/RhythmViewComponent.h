/**
 * @file RhythmViewComponent.h
 * @brief 리듬 키트의 클라이언트 쪽 — 음악을 틀어 그 구간 시계를 디렉터(`RhythmGameModeComponent`)에 넘기고, 레인 액션을 누름 · 뗌 명령으로 내며, 노트를 프리팹으로 보이고 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Rhythm`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitMusicTime` · `pressLane` · `releaseLane`)과 읽기 API 만 쓰고 판(규칙)을 직접 고치지 않는다.
 *          **시계**: 곡의 시각은 오디오 구간 시계(`GameSound::findMusicSeconds`)다 — 벽시계 · 프레임 시간을 쓰지 않아 소리와 판정이 어긋나지 않는다. 앞서 나던 음악의 위치를 이 곡의 시각으로 읽지 않게,
 *          앞 음악이 멈춘 것을 본 뒤에 튼다. 게임이 멈추면 음악을 멈춘다 — 구간 시계가 소리와 함께 서고 이어진다. 디렉터가 시뮬레이션 시계면 음악만 틀고 시각은 넘기지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 모습은 틱 뒤 게임 스레드에서 낸다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class RhythmGameModeComponent;
    class RhythmHUDViewModel;
    struct AudioMusicAsset;

    /**
     * @class RhythmViewComponent
     * @brief 레인마다 액션 하나 — 누르기 시작한 프레임에 누름, 뗀 프레임에 뗌을 디렉터에 넘깁니다.
     */
    REFLECT( Category = "Rhythm", DisplayName = "Rhythm View", Tooltip = "Client side of a rhythm song: plays the music and hands its segment clock over, presses lanes from input, shows the notes and fills the HUD" )
    class SW_KIT_CLIENT_API RhythmViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        RhythmViewComponent();
        virtual ~RhythmViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레인 액션이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 음악을 멈추고, 세운 모습을 지우고, HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;
        /** @brief 시계 · 입력 · 모습을 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 음악의 단계입니다. */
        enum class MusicPhase : uint8
        {
            Idle = 0, ///< 아직 틀지 않았다
            Stopping, ///< 앞 음악이 멈추기를 기다린다
            Playing,  ///< 이 곡을 틀었다
            Refused   ///< 틀지 못했다 — 다시 해 보지 않는다(오류는 남겼다)
        };

        /** @brief 틱 뒤 게임 스레드: 음악 시계를 넘기고 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager );
        /** @brief 음악을 틀고 그 시각을 디렉터에 넘깁니다. 멈춘 프레임의 시간은 건너뜁니다. */
        void updateMusic( RhythmGameModeComponent& director );
        /** @brief 이번 프레임의 레인 누름 · 뗌을 디렉터에 냅니다. */
        void submitInput( RhythmGameModeComponent& director );
        /** @brief 레인 · 판정선 · 다가오는 노트를 보입니다. */
        void showNotes( GameObjectManager& manager, const RhythmGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const RhythmGameModeComponent& director );

    private:
        PROPERTY( Category = "Song", DisplayName = "Music", Required, Tooltip = "Adaptive music (*.music.xml) whose start segment is the song" )
        AssetRef<AudioMusicAsset> _music;
        PROPERTY( Category = "Input", DisplayName = "Lane Actions", Tooltip = "InputMap action of each lane, left to right (one per chart lane)" )
        vector<hashed_string> _listLaneAction;
        PROPERTY( Category = "View", DisplayName = "Note Prefab", Required, Tooltip = "Unit piece used for lanes, the judge line and notes" )
        AssetRef<PrefabAsset> _notePrefab;
        PROPERTY( Category = "View", DisplayName = "Lane Width", Min = 0.1, Units = m )
        float32 _laneWidth;
        PROPERTY( Category = "View", DisplayName = "Hi Speed", Tooltip = "Distance a note travels in one second", Min = 0.1, Units = "m/s" )
        float32 _hiSpeed;
        PROPERTY( Category = "View", DisplayName = "View Length", Tooltip = "Notes within this distance of the judge line are shown", Min = 1.0, Units = m )
        float32 _viewLength;
        PROPERTY( Category = "View", DisplayName = "Lane Color" )
        float4 _laneColor;
        PROPERTY( Category = "View", DisplayName = "Held Color" )
        float4 _heldColor;
        PROPERTY( Category = "View", DisplayName = "Note Color" )
        float4 _noteColor;

        SW_TRANSIENT TComponentHandle<RhythmGameModeComponent> _director;
        SW_TRANSIENT PrefabViewBatch                           _laneViews;
        SW_TRANSIENT PrefabViewBatch                           _noteViews;
        SW_TRANSIENT unique_ptr<RhythmHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT vector<uint8> _listLaneHeld;                  ///< 레인마다 지난 프레임에 눌려 있었는가
        SW_TRANSIENT float32       _musicStart;                    ///< 음악을 튼 때의 곡 시각(초) — 되살린 판은 그 자리부터 흐른다
        SW_TRANSIENT MusicPhase    _musicPhase;                    ///< 음악의 단계
        SW_TRANSIENT uint8         _bInputLive    : 1;             ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 읽지 않는다)
        SW_TRANSIENT uint8         _bActionsValid : 1;             ///< 레인 액션이 모두 있다
        SW_TRANSIENT uint8         _bMusicPaused  : 1;             ///< 게임이 멈춰 음악을 멈춰 두었다
        [[maybe_unused]] uint8     _reserved      : 5;
    };
} // namespace sw
