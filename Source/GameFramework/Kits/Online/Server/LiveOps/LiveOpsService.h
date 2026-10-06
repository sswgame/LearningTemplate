/**
 * @file LiveOpsService.h
 * @brief 라이브 운영 로직 — 이벤트 정의 다시 읽기(30 초 · 버스), 계정마다 열린 이벤트(원격 설정 값 풀기), 경계 알림 판정, 운영의 바꾸기(감사 줄과 한 트랜잭션). 전송을 모른다.
 * @details - 정의는 영속 `liveops_event`(키 = 이벤트 id). 운영 도구(GF_Admin — 게임 조립)가 `putEvent` · `removeEvent` 로 바꾸면 버스 `liveops.changed` 로 다른 서버가 바로 다시
 *            읽는다(버스는 최대 한 번이라 주기 읽기가 놓친 것을 메운다).
 *          - 매개변수 값이 `@<설정 키>` 면 원격 설정(`RemoteConfig`)의 글 · 정수로 푼다 — 이벤트를 다시 쓰지 않고 값을 운영 설정으로 바꾼다.
 *            기능 플래그 `feature.liveops`(없으면 켬)가 꺼지면 아무 이벤트도 보이지 않는다(긴급 스위치).
 *          - 열린 이벤트 묶음 · 회차 끝 · 원격 설정 묶음의 해시가 바뀌면(시작 · 끝 · 회차 경계 · 다시 읽기) `takeStateChange` 가 참 — 바인딩이 모두에게 "다시 받아라" 를 알린다.
 *          PlayFab LiveOps(타이틀 데이터 · 예약) · Unity Remote Config(조건 · 출시 비율 · 일정)와 같은 자리 — 일정은 영속 + 버스, 값은 원격 설정으로 두 정본을 섞지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/LiveOps/LiveOpsTypes.h"

namespace sw
{
    class IServerBus;
    class IServiceStore;
    class LiveOpsWriteWork;
    class RemoteConfig;

    /** @brief 빌려 쓰는 것들입니다(로직보다 오래 산다). */
    struct LiveOpsDependencies
    {
        IServiceStore*      _pStore{ nullptr };        ///< 필수
        IServerBus*         _pBus{ nullptr };          ///< 서버 한 대면 null — 바꾼 뒤 알리기만(받기는 바인딩이 구독)
        const RemoteConfig* _pRemoteConfig{ nullptr }; ///< null 이면 '@' 값은 그대로 · 긴급 스위치 없음
    };
} // namespace sw

namespace sw
{
    /** @brief 바꾸기 요청의 완료입니다. */
    struct LiveOpsCompletion
    {
        uint64        _requestTag{ 0 };
        LiveOpsResult _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 간 버스 주제 · 플래그입니다(내보내는 클래스 밖 — 다른 모듈이 값으로 읽는다). */
    struct LiveOpsBus
    {
        static constexpr const utf8* kChangedTopic   = "liveops.changed"; ///< 정의를 바꿨다 — 몸 없음, 받는 서버는 다시 읽는다
        static constexpr const utf8* kKillSwitchFlag = "feature.liveops"; ///< 꺼지면 아무 이벤트도 보이지 않는다
        static constexpr int64       kReloadPeriodMs = 30000;
    };
} // namespace sw

namespace sw
{
    /**
     * @class LiveOpsService
     * @brief 라이브 운영 로직입니다(서비스 스레드 하나).
     */
    class SW_GF_API LiveOpsService
    {
    public:
        LiveOpsService();
        ~LiveOpsService();

        LiveOpsService( const LiveOpsService& )            = delete;
        LiveOpsService& operator=( const LiveOpsService& ) = delete;

        void initialize( const LiveOpsDependencies& dependencies );
        /** @brief 맡긴 저장소 일은 저장소의 `shutdown` · `pollCompletions` 가 거둔다 — 이 객체는 그때까지 살아 있어야 한다. */
        void shutdown();
        /** @brief 주기 다시 읽기 · 경계 판정. 저장소 완료는 호스트(또는 서버 루프)가 비운다. */
        void tick( int64 nowMs );
        /** @brief 다른 서버가 바꿨다(버스) — 다음 tick 에 다시 읽는다. */
        void notifyChanged() { _bReloadRequested = SW_TRUE; }
        /** @brief 열린 이벤트 묶음이 지난번 이후 바뀌었는가입니다(읽으면 지운다). */
        [[nodiscard]] bool takeStateChange();

        /** @brief 이 계정에 지금 열린 이벤트입니다. @p bClientOnly 면 클라이언트에 보이는 것만. */
        void computeActiveEvents( AccountId accountId, string_view region, uint32 buildVersion, int64 nowMs, bool bClientOnly, vector<LiveEventState>& outListEvent ) const;
        bool isEventActive( string_view eventId, AccountId accountId, string_view region, uint32 buildVersion, int64 nowMs ) const;
        /** @brief 매개변수 값(원격 설정까지 푼 값)입니다. 이벤트 · 키가 없으면 false. */
        bool findParameter( string_view eventId, string_view key, string& outValue ) const;

        // 운영(게임 조립 — GF_Admin 명령이 부른다). 완료는 drainCompletions. @p actorId 는 감사 줄의 GM 계정(0 = system).
        void putEvent( const LiveEventDefinition& definition, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag );
        void removeEvent( string_view eventId, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag );
        void drainCompletions( vector<LiveOpsCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }

        const vector<LiveEventDefinition>& getEvents() const { return _listEvent; }
        int32                              getPendingCount() const { return _pendingCount; }

        /** @brief 저장소 일의 `complete` 가 부른다(키트 안). */
        void applyReload( vector<LiveEventDefinition>&& listEvent, bool bReadOk, int64 nowMs );
        void applyWrite( uint64 requestTag, LiveOpsResult result );

    private:
        void                       startReload( int64 nowMs );
        void                       markIfChanged( int64 nowMs );
        uint64                     computeOpenHash( int64 nowMs ) const;
        string                     resolveValue( const string& value ) const;
        bool                       isKillSwitchOff() const;
        const LiveEventDefinition* findEvent( string_view eventId ) const;
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
