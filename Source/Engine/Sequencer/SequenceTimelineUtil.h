/**
 * @file SequenceTimelineUtil.h
 * @brief SequenceAsset 프레임을 씬 오브젝트(활성 상태, 키 트랙 값, 게임 카메라, 구간 동작)에 적용합니다. 대상은 바인딩 id 로 찾습니다(`SequenceBindingView`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    struct PropertyInfo;
    struct SequenceKeyTrack;

    class AnimationClip;
    class Component;
    class GameObject;
    class GameObjectManager;
    class SequenceAsset;
    class SequencePlayer;

    /** @brief 시퀀스가 카메라 쪽에 내는 신호의 종류입니다. 듣는 쪽(게임 프레임워크의 카메라)이 블렌드 · 흔들림 · 대기 뷰로 바꿉니다. */
    enum class SequenceCameraSignalKind : uint8
    {
        CutBegin = 0,   ///< 덮는 카메라 컷이 바뀌었다 — 대상 카메라의 디렉터가 `_preset` 을 켠다
        CinematicBegin, ///< 컷이 없던 때 첫 컷이 시작했다 — 게임 카메라 화면에서 `_blendSeconds` 동안 섞어 들어간다
        CinematicEnd,   ///< 덮던 컷이 모두 끝났다(재생을 멈춘 때 포함) — 마지막 컷 화면에서 게임 카메라로 `_blendSeconds` 동안 섞어 나온다
        CutPrewarm,     ///< 컷이 `_framesUntil` 프레임 뒤에 시작한다 — 그 카메라를 "곧 볼 시점(대기 뷰)" 으로 등록할 때(언리얼 preroll)
        Shake,          ///< 카메라 흔들림 항목이 시작했다 — 대상 카메라를 구간 길이만큼 흔든다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시퀀스 카메라 신호 하나입니다. 항목은 신호를 낸 순간의 사본이라 에셋이 바뀌어도 유효합니다.
     * @details 프레임 단위 값만 타임라인이 채우고, 초 단위 값(`_secondsUntil`, `_durationSeconds`)과 대상(`_target`)은 재생 빠르기와 바인딩을 아는 `SequencePlayerComponent` 가 채운다.
     */
    struct SequenceCameraSignal
    {
        SequenceTrackItem        _item{};                  ///< 그 컷 · 흔들림 항목(끝 신호는 덮던 마지막 컷)
        GameObjectHandle         _target{};                ///< 항목의 바인딩이 묶인 오브젝트(컷 카메라 · 흔들 카메라). 묶이지 않았으면 무효다
        int32                    _framesUntil{ 0 };        ///< 대기 뷰: 컷 시작까지 남은 프레임
        float32                  _secondsUntil{ 0.0f };    ///< 대기 뷰: 컷 시작까지 남은 시간(초)
        float32                  _durationSeconds{ 0.0f }; ///< 흔들림: 구간 길이(초)
        float32                  _blendSeconds{ 0.0f };    ///< 시네마틱 시작 · 끝: 섞는 시간(초). 0 이면 끊어 바꾼다
        SequenceCameraSignalKind _kind{ SequenceCameraSignalKind::CutBegin };
    };

    /** @brief 지난 이벤트 항목 하나입니다(`SequencePlayerComponent::registerSequenceEvent`). 항목은 알림 동안만 유효한 사본이고, 대상은 바인딩이 묶인 오브젝트입니다(없으면 무효). */
    struct SequenceFiredEvent
    {
        SequenceTrackItem _item{};
        GameObjectHandle  _target{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SequenceBindingSlot
     * @brief 씬의 바인딩 하나 — 시퀀스의 바인딩 id 에 묶인 오브젝트입니다(유니티 PlayableDirector 의 바인딩 표 한 줄).
     * @details 오브젝트는 핸들 PROPERTY 라 씬 파일에는 엔티티의 파일 id 로 적히고 로드 때 이 실행의 오브젝트로 바뀐다(`ObjectStateBatch`) — 이름을 바꿔도 끊기지 않는다.
     */
    REFLECT()
    struct SW_API SequenceBindingSlot
    {
        REFLECT_BODY();

        PROPERTY( Min = 1, Tooltip = "Binding id in the sequence" )
        uint32 _binding{ kSequenceNoBinding };
        PROPERTY( Tooltip = "Scene object the binding drives" )
        GameObjectHandle _object{};
    };

    /** @brief 시퀀스가 스폰한 오브젝트 하나입니다. 항목 번호로 어느 스폰 항목의 것인지 안다. 스폰에 실패했으면 `_object` 가 무효이고 그 구간이 끝날 때까지 다시 시도하지 않는다. */
    struct SequenceSpawnedObject
    {
        uint32           _itemIndex{ 0 };
        GameObjectHandle _object{};
    };

    /** @brief 구간 처리기가 있는 항목(애니메이션 · 소리 · 자막) 하나의 재생 상태입니다. 들어간 뒤 나갈 때까지 산다. */
    struct SW_API SequenceSpanState
    {
        shared_ptr<const AnimationClip> _clip;      ///< 애니메이션: 들어갈 때 잡은 클립(머무는 동안 다시 찾지 않는다)
        uint64                          _playingID; ///< 소리: 낸 소리의 재생 id(엔진이 없으면 0)
        uint32                          _itemIndex; ///< 시퀀스의 항목 번호
        uint8                           _bInside  : 1;
        [[maybe_unused]] uint8          _reserved : 7;

        explicit SequenceSpanState( uint32 itemIndex );
    };

    /**
     * @brief 재생 전 대상의 값 하나입니다(`SequenceFinishPolicy::RestoreState`). 출처는 항목(클립의 활성 상태) 또는 키 트랙(채널 값)이고, 대상은 되돌릴 때 바인딩으로 다시 찾는다.
     */
    struct SW_API SequenceRestoreEntry
    {
        float32                _arrValue[kSequenceMaxChannelCount]; ///< 키 트랙: 채널 순서의 값(`SequenceTimelineUtil::readTrackValues`)
        uint32                 _sourceIndex;                        ///< 항목 또는 키 트랙 번호
        uint8                  _bFromTrack : 1;                     ///< 참이면 키 트랙, 거짓이면 항목
        uint8                  _bActive    : 1;                     ///< 항목: 재생 전 활성 상태
        [[maybe_unused]] uint8 _reserved   : 6;

        SequenceRestoreEntry();
    };

    /** @brief 시퀀스가 재생 · 미리보기 동안 쥐는 것들입니다(스폰한 오브젝트, 구간 상태, 되돌릴 값). 플레이어와 에디터 미리보기가 하나씩 든다. 저장하지 않는다. */
    struct SW_API SequencePlaybackState
    {
        vector<SequenceSpawnedObject> _listSpawned;
        vector<SequenceSpanState>     _listSpan;
        vector<SequenceRestoreEntry>  _listRestore;
        uint8                         _bRestoreCaptured : 1; ///< `_listRestore` 가 이번 재생의 시작 상태다
        [[maybe_unused]] uint8        _reserved         : 7;

        SequencePlaybackState();
    };

    /**
     * @brief 바인딩 id 를 오브젝트로 푸는 창입니다. 스폰 바인딩은 시퀀스가 스폰한 오브젝트, 나머지는 씬의 바인딩 표(`SequenceBindingSlot`)입니다. 이름으로 찾는 길은 없다.
     * @details 가리키는 것은 부르는 쪽(플레이어 · 에디터 미리보기)이 쥔다. 창은 한 호출 동안만 쓴다.
     */
    struct SW_API SequenceBindingView
    {
        const SequenceAsset*                 _pAsset{ nullptr };
        const vector<SequenceBindingSlot>*   _pListSlot{ nullptr };
        const vector<SequenceSpawnedObject>* _pListSpawned{ nullptr };

        /** @brief 바인딩 @p bindingID 의 오브젝트 핸들입니다. 스폰 바인딩이면 스폰한 오브젝트(아직 없으면 무효), 아니면 표의 칸이고 칸이 없으면 무효입니다. */
        [[nodiscard]] GameObjectHandle findHandle( uint32 bindingID ) const;
        /** @brief 표에 바인딩 @p bindingID 의 칸이 있는지입니다(스폰 바인딩은 표에 두지 않는다). */
        [[nodiscard]] bool hasSlot( uint32 bindingID ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 구간 처리기에 넘기는 한 번의 문맥입니다. 대상은 바인딩이 묶인 오브젝트이고 바인딩이 없는 항목이면 nullptr 입니다.
     */
    struct SequenceSpanContext
    {
        GameObjectManager*       _pManager{ nullptr };
        const SequenceAsset*     _pAsset{ nullptr }; ///< 항목의 시퀀스 — 바인딩의 표시 이름(자막 화자)을 여기서 읽는다
        const SequenceTrackItem* _pItem{ nullptr };
        GameObject*              _pTarget{ nullptr };
        SequenceSpanState*       _pState{ nullptr };
        float32                  _framesPerSecond{ 30.0f };
        int32                    _frame{ 0 };
    };

    /** @brief 구간 종류의 동작 — 들어갈 때, 머무는 프레임마다, 나갈 때(멈춤 · 되감기 포함)입니다. 없는 단계는 nullptr 입니다. 종류 표의 줄이 가리킨다(`SequenceItemKindInfo::_pSpanHandler`). */
    struct SequenceSpanHandler
    {
        void ( *_pEnter )( const SequenceSpanContext& context );
        void ( *_pUpdate )( const SequenceSpanContext& context );
        void ( *_pLeave )( const SequenceSpanContext& context );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시퀀서 타임라인을 GameObject 에 반영하는 공용 도우미입니다.
     */
    struct SW_API SequenceTimelineUtil
    {
        /**
         * @brief "이전 프레임이 없다"는 표시입니다. 이 값을 넘기면 이벤트를 쏘지 않습니다.
         * @details `-1` 이나 `previousFrame < 0` 판정을 쓰지 않는다. **음수 프레임이 있는 시퀀스에서는 멀쩡한 이전 프레임이
         *          이 뜻으로 읽히고**, `_frameMin` 이 0 인 시퀀스에서는 "첫 프레임을 막 지났다"(-1)와 겹쳐 첫 프레임 이벤트를 쏠 수 없다.
         */
        static constexpr int32 kNoPreviousFrame = ( -2147483647 - 1 );

        /**
         * @brief 그 프레임의 클립 활성 상태, 카메라 컷, 키 트랙 값을 적용하고, 지나간 이벤트를 모읍니다.
         * @param previousFrame 직전에 적용한 프레임입니다. 이벤트는 그 사이를 **지나간 것**만 셉니다.
         *                      `kNoPreviousFrame` 이면 이벤트를 보지 않습니다.
         * @param pOutListCrossedEvent nullptr 가 아니면 이번에 지나간 이벤트 항목들로 **채웁니다**(먼저 비웁니다).
         *                             가리키는 `SequenceTrackItem` 은 @p asset 안의 원소이므로 에셋보다 오래 살지 않습니다.
         * @details 묶이지 않은 바인딩의 항목 · 트랙은 건너뛴다(재생은 시작 때 누락을 오류로 멈춘다 — `SequencePlayerComponent::play`). 구간 동작은 `applySpans` 가 따로 돈다.
         */
        static void applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, int32 frame,
                                int32                             previousFrame        = kNoPreviousFrame,
                                vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
        /**
         * @brief 플레이어의 이번 갱신을 적용합니다(`applyFrame`). 루프를 되감았으면 되감기 전 끝 구간(직전 프레임, `_frameMax`]의
         *        이벤트를 먼저 모읍니다. 그 구간과 되감은 뒤 구간을 합친 것이 이번 갱신에 지나간 이벤트입니다.
         */
        static void applyPlayback( GameObjectManager* pManager, const SequenceAsset& asset, const SequencePlaybackStep& step, const SequenceBindingView& bindings,
                                   vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
        /** @brief 바인딩 @p bindingID 가 묶인 살아 있는 오브젝트입니다. 관리자가 없거나 묶이지 않았거나 지워졌으면 nullptr 입니다. 재생 · 미리보기 · 에디터의 키 찍기가 같은 규칙을 쓴다. */
        static GameObject* findTarget( GameObjectManager* pManager, const SequenceBindingView& bindings, uint32 bindingID );
        /**
         * @brief 재생에 필요한 바인딩 가운데 씬에 묶이지 않은 것을 @p outListBindingID 에 채웁니다(스폰 바인딩과 대상이 없어도 되는 항목은 빼고).
         * @details 재생이 시작 전에 부른다 — 묶이지 않은 채 틀면 클립 · 키가 조용히 아무것도 몰지 않는다(이름 찾기로 돌아가는 길은 없다).
         */
        static void collectUnboundBindings( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, vector<uint32>& outListBindingID );
        /**
         * @brief 프로퍼티 트랙이 가리키는 컴포넌트와 숫자 프로퍼티를 찾습니다(`_propertyPath` = "컴포넌트 타입.프로퍼티"). 못 찾거나 숫자가 아니면 false 입니다.
         * @details 숫자 여부는 내장 타입 표의 갈래(`ReflectBuiltinClass::Number` — 정수 · 실수 · 성분이 하나인 압축 타입)로 가른다. 값은 `ReflectedValueUtil::readNumber` · `writeNumber` 로만 읽고 쓴다.
         *          재생이 값을 쓰는 길과 에디터가 지금 값을 읽어 키로 찍는 길이 이 한 규칙을 쓴다(언리얼 `FTrackInstancePropertyBindings` 처럼 묶기는 런타임, 키 찍기는 편집기).
         */
        [[nodiscard]] static bool findTrackProperty( GameObject* pTarget, const SequenceKeyTrack& track, Component*& pOutComponent, const PropertyInfo*& pOutProperty );
        /**
         * @brief 키 트랙 대상의 지금 값을 채널 순서로 읽습니다(트랜스폼은 로컬 이동, 회전(라디안), 크기이고, 리그 슬롯은 가중치, 프로퍼티는 그 값, 렌즈는 화각 · 초점 · 조리개 · 시프트입니다).
         * @details 에디터의 키 찍기(지금 상태를 키로)와 재생 전 상태 기억(`captureRestoreState`)이 같은 읽기를 쓴다.
         * @return 대상, 컴포넌트, 숫자 프로퍼티를 찾지 못하거나 @p valueCapacity 가 채널 수보다 작으면 false 입니다.
         */
        [[nodiscard]] static bool readTrackValues( GameObject* pTarget, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity );
        /** @brief `readTrackValues` 가 읽은 값을 대상에 그대로 씁니다(되돌리기). 채널 수는 트랙 종류입니다. 대상 · 컴포넌트가 없으면 아무것도 하지 않습니다. */
        static void writeTrackValues( GameObject* pTarget, const SequenceKeyTrack& track, const float32* pArrValue );
        /**
         * @brief 리그 슬롯 트랙의 @p frame 가중치입니다(0..1 로 자른다). 키가 없으면 1(슬롯을 정하지 않은 것과 같다)입니다.
         * @details 언리얼 Control Rig 트랙의 가중치 커브, 유니티 Animation Rigging 의 Rig Weight 키에 해당한다.
         */
        static float32 sampleRigSlotWeight( const SequenceKeyTrack& track, float32 frame );
        /**
         * @brief 스폰 항목(`SequenceItemKind::Spawn`)을 @p frame 에 맞춥니다 — 구간 시작 `getPrerollFrames` 프레임 앞부터 프리팹을 **꺼 둔 채** 스폰하고, 구간 동안 켜고, 구간(끝)이 지나거나 되감아
         *        창을 벗어나면 지웁니다. 스폰한 오브젝트는 @p inoutListSpawned 가 기억하고 그 항목의 바인딩이 된다(`SequenceBindingView`). 오브젝트 이름은 바인딩 이름이다.
         * @details 프리롤 동안 꺼 둔 채 두는 까닭: 프리팹의 메시 · 머티리얼 · 애니메이션 에셋이 읽히고 PSO 가 만들어져(`FrameRenderer` 가 씬의 머티리얼마다 만든다) 컷의 첫 프레임이
         *          튀지 않는다. 스폰은 컴포넌트 틱 중이면 상태가 틱 뒤에 채워지므로(`PrefabCache::spawn`) 끄기도 틱 뒤로 미룬다. 프리팹 캐시가 없는 매니저는 오류를 남기고 아무것도 하지 않는다.
         */
        static void applySpawns( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, vector<SequenceSpawnedObject>& inoutListSpawned );
        /** @brief 시퀀스가 스폰한 오브젝트를 모두 지우고 목록을 비웁니다. 재생을 멈출 때 부릅니다. */
        static void releaseSpawns( GameObjectManager* pManager, vector<SequenceSpawnedObject>& inoutListSpawned );
        /** @brief 시퀀스에 카메라 컷이 있으면 컷을 풉니다(게임 카메라가 원래 규칙으로 돌아간다). 재생을 멈출 때 부릅니다. */
        static void releaseCameraCut( GameObjectManager* pManager, const SequenceAsset& asset );
        /**
         * @brief 구간 처리기가 있는 항목(애니메이션 · 소리 · 자막)을 @p frame 에 맞춥니다 — 구간에 들어가면 들어가기, 머무는 동안 머물기, 벗어나면 나가기를 부른다(`SequenceSpanHandler`).
         * @details 구간 상태는 @p inoutListSpan 이 쥔다(항목 번호로 찾는다). 대상이 필요한 항목인데 바인딩이 묶이지 않았으면 들어가지 않는다.
         *          @p pOutListEntered 가 있으면 이번에 들어간 항목을 더한다(재생 기록 `SequenceHistoryKind::Span`).
         */
        static void applySpans( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, int32 frame, float32 framesPerSecond,
                                vector<SequenceSpanState>& inoutListSpan, vector<const SequenceTrackItem*>* pOutListEntered = nullptr );
        /** @brief 들어가 있는 구간을 모두 나가고 목록을 비웁니다(멈춤 · 씬 나감 — 소리를 멈추고 애니메이션 덮어쓰기를 끈다). */
        static void releaseSpans( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, float32 framesPerSecond, vector<SequenceSpanState>& inoutListSpan );
        /** @brief 재생 전 상태(클립 대상의 활성 · 키 트랙 대상의 값)를 @p outListRestore 에 채웁니다(`SequenceFinishPolicy::RestoreState`). 묶이지 않은 대상은 건너뛴다. */
        static void captureRestoreState( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, vector<SequenceRestoreEntry>& outListRestore );
        /** @brief `captureRestoreState` 가 기억한 상태로 대상을 되돌립니다(기억한 순서의 반대로 — 같은 대상의 두 기록은 처음 값이 남는다). */
        static void applyRestoreState( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, const vector<SequenceRestoreEntry>& listRestore );
        /**
         * @brief (@p previousFrame, @p frame] 동안의 카메라 신호를 @p outListSignal 에 **덧붙입니다**.
         * @details 순서는 컷 바뀜, 시네마틱 시작 · 끝, 대기 뷰, 흔들림이다. 듣는 쪽은 컷의 프리셋을 먼저 켜고 그 위에 게임 화면에서 섞는다.
         *          @p previousFrame 이 `kNoPreviousFrame` 이면 이전에 덮던 컷이 없다고 보고, 흔들림은 @p frame 에서 시작하는 것만 낸다.
         */
        static void collectCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, vector<SequenceCameraSignal>& outListSignal );
        /** @brief 플레이어의 이번 갱신 동안의 카메라 신호입니다. 루프를 되감았으면 되감기 전 끝 구간을 먼저 보고, 끝과 처음을 덮는 컷은 이어진 것으로 봅니다. */
        static void collectPlaybackCameraSignals( const SequenceAsset& asset, const SequencePlaybackStep& step, vector<SequenceCameraSignal>& outListSignal );

    private:
        /** @brief (@p previousFrame, @p frame] 에서 시작하는 이벤트를 @p pOutListCrossedEvent 에 **덧붙입니다**(비우지 않는다). */
        static void appendCrossedEvents( const SequenceAsset& asset, int32 previousFrame, int32 frame,
                                         vector<const SequenceTrackItem*>* pOutListCrossedEvent );
        /** @brief 이전에 덮던 컷(@p pPreviousCut)을 알고 (@p previousFrame, @p frame] 의 카메라 신호를 덧붙입니다. */
        static void appendCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, const SequenceTrackItem* pPreviousCut,
                                         vector<SequenceCameraSignal>& outListSignal );
    };
} // namespace sw
