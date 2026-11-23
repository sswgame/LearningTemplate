/**
 * @file SequenceNetSimGame.h
 * @brief 한 프로세스 재생(`NetSimHarness`)에서 컷씬 재생 복제(`GF_NetSequence`)를 보는 본보기 게임입니다 — 서버 판정 · 그 플레이어만 / 전원 · 늦게 들어온 클라이언트.
 * @details 월드마다 같은 이름의 오브젝트를 세운다(번호표 `NetObjectTable` 이 이름 해시로 짝짓는다).
 *          - 판 상태 `SessionState`(전원 기록), 디렉터 둘 `EveryoneDirector`(긴 재생 — 늦은 참가를 볼 틈) · `PlayerDirector`(짧은 재생),
 *            시퀀스 트리거 둘 `EveryoneTrigger`(Everyone, 플래그 `sample.everyoneSeen`) · `PlayerTrigger`(Activator, 플래그 `sample.playerSeen`), 폰 `Pawn1` ~ `Pawn4`, 세션 `NetSession`.
 *          - 서버 월드: 폰마다 원격 조종자 `Controller<n>`(플레이어 상태를 든다, 입력 연결 = n − 1 — 먼저 붙은 클라이언트가 연결 0 이다).
 *          - 클라이언트 월드 n: 로컬 플레이어 조종자 `LocalController` 가 `Pawn<n>` 을 쥔다(`ControlSystem::isLocallyControlled`).
 *          - 진입 판정은 서버 코드가 대신한다(`activateTrigger` — 트리거의 `activate`, 물리 겹침과 같은 길의 끝). 등록한 게임(`Scheduled`)은 클라이언트 둘이 붙은 뒤
 *            `Pawn1` 으로 두 트리거를, `PlayerDirector` 가 끝나면 `Pawn2` 로 `PlayerTrigger` 를 다시 연다 — 에디터 시나리오(`netsequence.scenario.xml`)가 감시 값으로 본다.
 *          - 읽을 값(`Editor.NetSimValue`): `EveryonePlaying` · `PlayerPlaying`(이 월드의 디렉터가 재생 중), `EveryoneFrame`, `PlayerStarts`(시작 수),
 *            서버만 `EveryoneSeen`(판 상태 플래그) · `PlayerSeen<n>`(플레이어 n 의 상태 플래그) · `SkipRejected` · `SkipAccepted`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    class PlayerStateComponent;
    class SequenceNetSessionComponent;
    class SequencePlayerComponent;

    /** @brief 서버가 진입을 스스로 여는지입니다. 테스트는 손으로 연다. */
    enum class SequenceNetSimSchedule : uint8
    {
        Scheduled = 0, ///< 클라이언트 둘이 붙은 뒤 정해진 차례로 연다(에디터 게임 목록)
        Manual         ///< 열지 않는다 — 부르는 쪽이 `activateTrigger` 로 연다(테스트)
    };

    /**
     * @class SequenceNetSimGame
     * @brief 컷씬 재생 복제의 본보기 게임입니다. 파일 머리말 참고.
     */
    class SequenceNetSimGame final : public INetSimGame
    {
    public:
        static constexpr const utf8* kName             = "Sample: cutscene"; ///< `NetSimGameRegistry` 에 등록하는 이름
        static constexpr const utf8* kEveryoneTrigger  = "EveryoneTrigger";
        static constexpr const utf8* kPlayerTrigger    = "PlayerTrigger";
        static constexpr const utf8* kEveryoneDirector = "EveryoneDirector";
        static constexpr const utf8* kPlayerDirector   = "PlayerDirector";
        static constexpr const utf8* kEveryoneFlag     = "sample.everyoneSeen";
        static constexpr const utf8* kPlayerFlag       = "sample.playerSeen";
        static constexpr int32       kMaxPlayerCount   = 4;   ///< 폰 · 원격 조종자 수(클라이언트 넷까지)
        static constexpr int32       kEveryoneFrames   = 300; ///< 전원 재생 길이(30 fps — 10 초, 늦은 참가를 볼 틈)
        static constexpr int32       kPlayerFrames     = 60;  ///< 그 플레이어만 재생 길이(2 초)
        static constexpr uint32      kScheduleDelay    = 30;  ///< 클라이언트 둘이 붙고 첫 진입까지의 서버 틱

        explicit SequenceNetSimGame( SequenceNetSimSchedule schedule );

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override;

        /** @brief `NetSimGameRegistry` 에 등록할 생성 함수입니다(`Scheduled`). */
        static unique_ptr<INetSimGame> create();
        /** @brief 서버 월드에서 @p triggerName 트리거에 폰 @p playerNumber(1 부터)가 들어온 것으로 칩니다. 튼 재생이면 true 입니다. */
        static bool activateTrigger( NetSimWorld& world, const utf8* triggerName, int32 playerNumber );
        /** @brief 서버 월드에서 @p triggerName 트리거가 튼 재생이 하나라도 돌고 있으면 true 입니다(원격 플레이어만의 재생은 씬의 디렉터가 아니라 시간만 재는 디렉터가 돈다). */
        static bool isTriggerPlaying( NetSimWorld& world, const utf8* triggerName );
        /** @brief 월드의 디렉터 @p directorName 의 시퀀스 플레이어입니다. 없으면 nullptr 입니다. */
        static SequencePlayerComponent* findDirector( NetSimWorld& world, const utf8* directorName );
        /** @brief 월드의 세션 컴포넌트입니다. 없으면 nullptr 입니다. */
        static SequenceNetSessionComponent* findNetSession( NetSimWorld& world );
        /** @brief 서버 월드에서 플레이어 @p playerNumber 의 상태(원격 조종자 오브젝트)입니다. 없으면 nullptr 입니다. */
        static PlayerStateComponent* findPlayerState( NetSimWorld& world, int32 playerNumber );
        /** @brief 폰 오브젝트 이름(`Pawn<n>`)입니다. */
        static string makePawnName( int32 playerNumber );

    private:
        SequenceNetSimSchedule _schedule;
    };
} // namespace sw
