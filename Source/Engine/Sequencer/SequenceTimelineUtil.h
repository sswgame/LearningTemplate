/**
 * @file SequenceTimelineUtil.h
 * @brief SequenceAsset 프레임을 씬 오브젝트(활성 상태, 키 트랙 값, 게임 카메라)에 적용합니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    struct SequenceKeyTrack;

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
     * @details 프레임 단위 값만 타임라인이 채우고, 초 단위 값(`_secondsUntil`, `_durationSeconds`)은 재생 빠르기를 아는 `SequencePlayerComponent` 가 채운다.
     */
    struct SequenceCameraSignal
    {
        SequenceTrackItem        _item{};                  ///< 그 컷 · 흔들림 항목(끝 신호는 덮던 마지막 컷)
        int32                    _framesUntil{ 0 };        ///< 대기 뷰: 컷 시작까지 남은 프레임
        float32                  _secondsUntil{ 0.0f };    ///< 대기 뷰: 컷 시작까지 남은 시간(초)
        float32                  _durationSeconds{ 0.0f }; ///< 흔들림: 구간 길이(초)
        float32                  _blendSeconds{ 0.0f };    ///< 시네마틱 시작 · 끝: 섞는 시간(초). 0 이면 끊어 바꾼다
        SequenceCameraSignalKind _kind{ SequenceCameraSignalKind::CutBegin };
    };
} // namespace sw

namespace sw
{
    /** @brief 시퀀스가 스폰한 오브젝트 하나입니다. 항목 번호로 어느 스폰 항목의 것인지 안다. 스폰에 실패했으면 `_object` 가 무효이고 그 구간이 끝날 때까지 다시 시도하지 않는다. */
    struct SequenceSpawnedObject
    {
        uint32           _itemIndex{ 0 };
        GameObjectHandle _object{};
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
         * @details 이벤트에 반응하려면 이 출력을 씁니다. `SequencePlayerComponent` 는 이것을 `registerSequenceEvent` 델리게이트로 내보냅니다.
         *          함께 남기는 `SW_LOG_INFO` 는 Dev 편의 기능이고 배포본에서는 사라집니다.
         */
        static void applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame,
                                int32                             previousFrame        = kNoPreviousFrame,
                                vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
        /**
         * @brief 플레이어의 이번 갱신을 적용합니다(`applyFrame`). 루프를 되감았으면 되감기 전 끝 구간(직전 프레임, `_frameMax`]의
         *        이벤트를 먼저 모읍니다. 그 구간과 되감은 뒤 구간을 합친 것이 이번 갱신에 지나간 이벤트입니다.
         */
        static void applyPlayback( GameObjectManager* pManager, const SequencePlayer& player,
                                   vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
        /**
         * @brief 키 트랙 대상의 현재 값을 채널 순서로 읽습니다(트랜스폼은 로컬 이동, 회전(라디안), 크기이고, 프로퍼티는 그 값입니다).
         * @return 대상, 컴포넌트, 숫자 프로퍼티를 찾지 못하면 false 를 반환합니다. 에디터의 키 찍기(현재 상태를 키로)가 씁니다.
         */
        [[nodiscard]] static bool readTrackValues( GameObjectManager* pManager, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity );
        /**
         * @brief 리그 슬롯 트랙의 @p frame 가중치입니다(0..1 로 자른다). 키가 없으면 1(슬롯을 정하지 않은 것과 같다)입니다.
         * @details 언리얼 Control Rig 트랙의 가중치 커브, 유니티 Animation Rigging 의 Rig Weight 키에 해당한다.
         */
        static float32 sampleRigSlotWeight( const SequenceKeyTrack& track, float32 frame );
        /**
         * @brief 스폰 항목(`SequenceItemKind::Spawn`)을 @p frame 에 맞춥니다 — 구간 시작 `getPrerollFrames` 프레임 앞부터 프리팹을 **꺼 둔 채** 스폰하고, 구간 동안 켜고, 구간(끝)이 지나거나 되감아
         *        창을 벗어나면 지웁니다. 스폰한 오브젝트는 @p inoutListSpawned 가 기억한다(플레이어가 쥔다).
         * @details 프리롤 동안 꺼 둔 채 두는 까닭: 프리팹의 메시 · 머티리얼 · 애니메이션 에셋이 읽히고 PSO 가 만들어져(`FrameRenderer` 가 씬의 머티리얼마다 만든다) 컷의 첫 프레임이
         *          튀지 않는다. 스폰은 컴포넌트 틱 중이면 상태가 틱 뒤에 채워지므로(`PrefabCache::spawn`) 끄기도 틱 뒤로 미룬다. 프리팹 캐시가 없는 매니저는 오류를 남기고 아무것도 하지 않는다.
         */
        static void applySpawns( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, vector<SequenceSpawnedObject>& inoutListSpawned );
        /** @brief 시퀀스가 스폰한 오브젝트를 모두 지우고 목록을 비웁니다. 재생을 멈출 때 부릅니다. */
        static void releaseSpawns( GameObjectManager* pManager, vector<SequenceSpawnedObject>& inoutListSpawned );
        /** @brief 시퀀스에 카메라 컷이 있으면 컷을 풉니다(게임 카메라가 원래 규칙으로 돌아간다). 재생을 멈출 때 부릅니다. */
        static void releaseCameraCut( GameObjectManager* pManager, const SequenceAsset& asset );
        /**
         * @brief (@p previousFrame, @p frame] 동안의 카메라 신호를 @p outListSignal 에 **덧붙입니다**.
         * @details 순서는 컷 바뀜, 시네마틱 시작 · 끝, 대기 뷰, 흔들림이다. 듣는 쪽은 컷의 프리셋을 먼저 켜고 그 위에 게임 화면에서 섞는다.
         *          @p previousFrame 이 `kNoPreviousFrame` 이면 이전에 덮던 컷이 없다고 보고, 흔들림은 @p frame 에서 시작하는 것만 낸다.
         */
        static void collectCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, vector<SequenceCameraSignal>& outListSignal );
        /** @brief 플레이어의 이번 갱신 동안의 카메라 신호입니다. 루프를 되감았으면 되감기 전 끝 구간을 먼저 보고, 끝과 처음을 덮는 컷은 이어진 것으로 봅니다. */
        static void collectPlaybackCameraSignals( const SequencePlayer& player, vector<SequenceCameraSignal>& outListSignal );

    private:
        /** @brief (@p previousFrame, @p frame] 에서 시작하는 이벤트를 @p pOutListCrossedEvent 에 **덧붙입니다**(비우지 않는다). */
        static void appendCrossedEvents( const SequenceAsset& asset, int32 previousFrame, int32 frame,
                                         vector<const SequenceTrackItem*>* pOutListCrossedEvent );
        /** @brief 이전에 덮던 컷(@p pPreviousCut)을 알고 (@p previousFrame, @p frame] 의 카메라 신호를 덧붙입니다. */
        static void appendCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, const SequenceTrackItem* pPreviousCut,
                                         vector<SequenceCameraSignal>& outListSignal );
    };
} // namespace sw
