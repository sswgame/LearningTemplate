/**
 * @file LiveOpsService.h
 * @brief 라이브 운영 로직입니다. 이벤트 정의 다시 읽기(30초 주기와 버스), 계정별 열린 이벤트 계산(원격 설정 값 해석), 경계 알림 판정, 운영의 변경(감사 기록과 한 트랜잭션)을 처리합니다. 전송은 모릅니다.
 * @details - 정의는 영속 테이블 `liveops_event` 에 있으며 키는 이벤트 id 이다. 운영 도구(GF_Admin, 게임 조립)가 `putEvent` 나 `removeEvent` 로 바꾸면 버스 `liveops.changed` 로 다른 서버가 바로 다시 읽는다.
 *            버스는 최대 한 번만 배달하므로 주기적인 읽기가 놓친 변경을 메운다.
 *          - 매개변수 값이 `@<설정 키>` 이면 원격 설정(`RemoteConfig`)의 문자열이나 정수로 해석한다. 이벤트를 다시 쓰지 않고 값을 운영 설정으로 바꿀 수 있다.
 *            기능 플래그 `feature.liveops` 가 꺼지면 아무 이벤트도 보이지 않는다. 플래그가 없으면 켜진 것으로 본다. 긴급 스위치이다.
 *          - 열린 이벤트 목록, 회차 끝, 원격 설정 묶음의 해시가 바뀌면 `takeStateChange` 가 true 를 반환한다. 시작, 끝, 회차 경계, 다시 읽기가 해당한다. 바인딩이 모든 클라이언트에 "다시 받아라" 를 알린다.
 *
 *          PlayFab LiveOps(타이틀 데이터와 예약)와 Unity Remote Config(조건, 출시 비율, 일정)에 해당한다. 일정은 영속 테이블과 버스로, 값은 원격 설정으로 관리해서 두 원본을 섞지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    class IServiceStore;
    class LiveOpsWriteWork;
    class RemoteConfig;
    class ServerBus;

    /** @brief 빌려 쓰는 객체들입니다. 로직보다 오래 살아야 합니다. */
    struct LiveOpsDependencies
    {
        IServiceStore*      _pStore{ nullptr };        ///< 필수
        ServerBus*          _pBus{ nullptr };          ///< 서버가 한 대이면 null 이다. 바꾼 뒤 알리기만 하며, 받기는 바인딩이 구독한다
        const RemoteConfig* _pRemoteConfig{ nullptr }; ///< null 이면 '@' 값은 그대로 두고 긴급 스위치도 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 변경 요청의 완료입니다. */
    struct LiveOpsCompletion
    {
        uint64        _requestTag{ 0 };
        LiveOpsResult _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브 운영 정의를 바꿨다는 버스 신호의 본문입니다. 필드가 없고 받는 서버가 다시 읽습니다. */
    REFLECT()
    struct SW_KIT_SERVER_API LiveOpsChanged
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 의미 버전(docs/plans/ServerBusFormat.md 3.4)
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 간 버스 주제와 플래그입니다. 다른 모듈이 값으로 읽으므로 내보내는 클래스 밖에 둡니다. */
    struct LiveOpsBus
    {
        static constexpr ServerBusTopic<LiveOpsChanged> kChangedTopic{ "liveops.changed" };  ///< 정의를 바꿨다는 알림이다. 받는 서버가 다시 읽는다
        static constexpr const utf8*                    kKillSwitchFlag = "feature.liveops"; ///< 꺼지면 아무 이벤트도 보이지 않는다
        static constexpr int64                          kReloadPeriodMs = 30000;
    };
} // namespace sw

namespace sw
{
    /**
     * @class LiveOpsService
     * @brief 라이브 운영 로직입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API LiveOpsService
    {
    public:
        LiveOpsService();
        ~LiveOpsService();

        LiveOpsService( const LiveOpsService& )            = delete;
        LiveOpsService& operator=( const LiveOpsService& ) = delete;

        void initialize( const LiveOpsDependencies& dependencies );
        /** @brief 맡긴 저장소 작업은 저장소의 `shutdown` 과 `pollCompletions` 가 회수합니다. 이 객체는 그때까지 살아 있어야 합니다. */
        void shutdown();
        /** @brief 주기적인 다시 읽기와 경계 판정을 합니다. 저장소 완료는 호스트(또는 서버 루프)가 비웁니다. */
        void update( int64 nowMs );
        /** @brief 다른 서버가 바꿨음을 버스로 받았을 때 부릅니다. 다음 tick 에 다시 읽습니다. */
        void notifyChanged() { _bReloadRequested = SW_TRUE; }
        /** @brief 열린 이벤트 목록이 지난번 이후 바뀌었으면 true 입니다. 읽으면 표시를 지웁니다. */
        [[nodiscard]] bool takeStateChange();

        /** @brief 이 계정에 지금 열린 이벤트를 반환합니다. @p bClientOnly 이면 클라이언트에 보이는 것만 반환합니다. */
        void computeActiveEvents( AccountID accountID, string_view region, uint32 buildVersion, int64 nowMs, bool bClientOnly, vector<LiveEventState>& outListEvent ) const;
        bool isEventActive( string_view eventID, AccountID accountID, string_view region, uint32 buildVersion, int64 nowMs ) const;
        /** @brief 매개변수 값을 반환합니다. 원격 설정까지 해석한 값이며 이벤트나 키가 없으면 false 를 반환합니다. */
        bool findParameter( string_view eventID, string_view key, string& outValue ) const;

        // 운영 함수들이며 게임 조립 단계에서 GF_Admin 명령이 부른다. 완료는 drainCompletions 로 받는다. @p actorID 는 감사 기록의 GM 계정이며 0이면 system 이다.
        void putEvent( const LiveEventDefinition& definition, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void removeEvent( string_view eventID, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void drainCompletions( vector<LiveOpsCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }

        const vector<LiveEventDefinition>& getEvents() const { return _listEvent; }
        int32                              getPendingCount() const { return _pendingCount; }

        /** @brief 저장소 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyReload( vector<LiveEventDefinition>&& listEvent, bool bReadOk, int64 nowMs );
        void applyWrite( uint64 requestTag, LiveOpsResult result );

    private:
        void                       startReload( int64 nowMs );
        void                       markIfChanged( int64 nowMs );
        uint64                     computeOpenHash( int64 nowMs ) const;
        string                     resolveValue( const string& value ) const;
        bool                       isKillSwitchOff() const;
        const LiveEventDefinition* findEvent( string_view eventID ) const;
        void                       submitWrite( unique_ptr<LiveOpsWriteWork> work );

        vector<LiveEventDefinition>    _listEvent;
        EventBuffer<LiveOpsCompletion> _completionBuffer;
        LiveOpsDependencies            _dependencies;
        int64                          _lastReloadMs;
        uint64                         _lastOpenHash;
        int32                          _pendingCount;
        uint8                          _bReloading;
        uint8                          _bReloadRequested;
        uint8                          _bStateChanged;
    };
} // namespace sw
