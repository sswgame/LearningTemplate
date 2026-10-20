/**
 * @file ServerDirectoryControl.h
 * @brief 서버 디렉터리의 점검과 공지를 바꾸는 인터페이스입니다. 서버 디렉터리 키트가 구현해 서버 조립 문맥(`OnlineServerKitContext::_pDirectoryControl`)에 내놓고, GM 키트가 읽어 부릅니다.
 * @details - 키트끼리 include 하지 않도록 값은 기반 타입으로만 주고받습니다. 서버 디렉터리 키트가 자기 형식(`MaintenanceWindow`, `ServiceNotice`)으로 옮기고 같은 규칙으로 검사합니다.
 *          - 변경은 비동기입니다. 결과는 부른 쪽이 준 요청 태그와 함께 `drainControlCompletions` 로 받습니다. 감사 기록은 구현이 변경과 같은 트랜잭션에 씁니다.
 *          - 서비스 스레드 하나에서만 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /** @brief 점검 창 하나의 변경입니다. 범위는 `all` 이거나 서버 종류 이름입니다. */
    struct ServerDirectoryMaintenanceChange
    {
        vector<AccountID> _listAllowedAccount{}; ///< 점검 중에도 들어갈 수 있는 계정(GM, 테스터)이다
        string            _scope{};
        string            _messageKey{}; ///< 안내 문구의 로컬라이제이션 키이다
        int64             _startMs{ 0 };
        int64             _endMs{ 0 }; ///< 0이면 해제할 때까지이다
    };
} // namespace sw

namespace sw
{
    /** @brief 공지 하나의 변경입니다. 같은 id 를 다시 올리면 덮어씁니다. */
    struct ServerDirectoryNoticeChange
    {
        string _text{}; ///< `_bLiteralText` 가 false 이면 로컬라이제이션 키이고, true 이면 GM 이 쓴 문구이다
        uint64 _noticeID{ 0 };
        int64  _startMs{ 0 };
        int64  _endMs{ 0 }; ///< 0이면 내릴 때까지이다
        int32  _priority{ 0 };
        uint8  _bLiteralText{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 변경 결과입니다. */
    enum class ServerDirectoryControlResult : uint8
    {
        Ok = 0,
        Invalid,    ///< 규칙에 맞지 않는 값이다
        NotFound,   ///< 없는 공지나 점검이다
        Unavailable ///< 저장소를 쓸 수 없다. 다시 하면 된다
    };

    /** @brief 변경 하나의 완료입니다. */
    struct ServerDirectoryControlCompletion
    {
        uint64                       _requestTag{ 0 };
        ServerDirectoryControlResult _result{ ServerDirectoryControlResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServerDirectoryControl
     * @brief 점검과 공지를 바꾸는 창입니다. @p actorID 는 감사 기록의 GM 계정이며 0이면 system 입니다.
     */
    class IServerDirectoryControl
    {
    public:
        IServerDirectoryControl()          = default;
        virtual ~IServerDirectoryControl() = default;

        IServerDirectoryControl( const IServerDirectoryControl& )            = delete;
        IServerDirectoryControl& operator=( const IServerDirectoryControl& ) = delete;

        virtual void setMaintenance( const ServerDirectoryMaintenanceChange& change, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag ) = 0;
        virtual void clearMaintenance( string_view scope, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag )                            = 0;
        virtual void postNotice( const ServerDirectoryNoticeChange& change, AccountID actorID, int64 nowMs, uint64 requestTag )                            = 0;
        virtual void removeNotice( uint64 noticeID, AccountID actorID, int64 nowMs, uint64 requestTag )                                                    = 0;
        /** @brief 끝난 변경을 @p outListCompletion 뒤에 붙입니다. */
        virtual void drainControlCompletions( vector<ServerDirectoryControlCompletion>& outListCompletion ) = 0;
    };
} // namespace sw
