/**
 * @file ServerDirectoryTypes.h
 * @brief 서버 디렉터리의 타입 — 결과 · 점검 창 · 공지 · 배정 요청과 답 · 서버 목록 줄입니다.
 * @details 시각은 모두 UTC 유닉스 밀리초(`WallClock`). 끝 0 = 내릴(풀) 때까지.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /** @brief 서버 디렉터리 결과입니다(배정 응답 몸 · 바꾸기 완료). */
    enum class ServerDirectoryResult : uint8
    {
        Ok = 0,
        NoServer,    ///< 받을 서버가 없다(모두 가득 · 닫힘 · 낡음)
        Maintenance, ///< 점검 중 — 끝 시각 · 안내 글 키
        Invalid,     ///< 모르는 종류 · 규칙 밖 값
        Unavailable, ///< 저장소(바꾸기) — 다시 하면 된다
        NotFound,    ///< 없는 공지 · 점검(바꾸기)
        Count
    };

    /** @brief 상한입니다. */
    struct ServerDirectoryLimit
    {
        static constexpr int32 kMaxAllowedAccountCount = 64;
        static constexpr int32 kMaxNoticeCount         = 32;
        static constexpr int32 kMaxNoticeTextSize      = 1024;
        static constexpr int32 kMaxMessageKeySize      = 64;
        static constexpr int32 kMaxWindowCount         = 16;
        static constexpr int32 kMaxServerListCount     = 256;
        static constexpr int32 kMaxSeatCount           = 64;
    };
} // namespace sw

namespace sw
{
    /** @brief 점검 창 하나 — 범위 `all` 은 모든 서버 종류, 그 밖은 종류 이름입니다. */
    struct MaintenanceWindow
    {
        static constexpr const utf8* kScopeAll = "all";

        vector<AccountID> _listAllowedAccount{}; ///< 점검 중에도 들어가는 계정(GM · 시험자) — 클라이언트에는 보내지 않는다
        string            _scope{};
        string            _messageKey{}; ///< 안내 글의 로컬라이제이션 키
        int64             _startMs{ 0 };
        int64             _endMs{ 0 }; ///< 0 = 풀 때까지

        bool isActive( int64 nowMs ) const { return _startMs <= nowMs && ( _endMs == 0 || nowMs < _endMs ); }
        bool appliesTo( string_view kind ) const { return _scope == kScopeAll || _scope == kind; }
        bool allowsAccount( AccountID accountID ) const
        {
            for ( const AccountID allowed : _listAllowedAccount )
            {
                if ( allowed == accountID )
                    return true;
            }
            return false;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 공지 하나입니다. */
    struct ServiceNotice
    {
        string _text{}; ///< 로컬라이제이션 키(`_bLiteralText` 가 거짓) 또는 GM 이 쓴 글
        uint64 _noticeID{ 0 };
        int64  _startMs{ 0 };
        int64  _endMs{ 0 };    ///< 0 = 내릴 때까지
        int32  _priority{ 0 }; ///< 큰 것이 위
        uint8  _bLiteralText{ SW_FALSE };

        bool isActive( int64 nowMs ) const { return _startMs <= nowMs && ( _endMs == 0 || nowMs < _endMs ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 보는 상태 — 지금 걸린 점검(허용 목록 없이)과 기간 안 공지(우선순위 내림차순)입니다. */
    struct ServerDirectoryStatus
    {
        vector<MaintenanceWindow> _listMaintenance{};
        vector<ServiceNotice>     _listNotice{};
    };
} // namespace sw

namespace sw
{
    /** @brief 배정 요청입니다. */
    struct ServerAssignmentRequest
    {
        string _kind{};
        string _region{};
        uint32 _buildVersion{ 0 }; ///< 0 = 판 무관
        int32  _seatCount{ 1 };    ///< 같이 들어갈 자리(파티)
    };
} // namespace sw

namespace sw
{
    /** @brief 배정 답입니다. Ok 면 붙을 서버, Maintenance 면 끝 시각 · 안내 글 키입니다. */
    struct ServerAssignment
    {
        string                _address{};
        string                _messageKey{};
        uint64                _serverID{ 0 };
        int64                 _maintenanceEndMs{ 0 };
        uint16                _port{ 0 };
        ServerDirectoryResult _result{ ServerDirectoryResult::NoServer };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 목록 한 줄(서버 고르기 화면)입니다. */
    struct ServerListEntry
    {
        string      _region{};
        string      _address{};
        uint64      _serverID{ 0 };
        uint16      _port{ 0 };
        uint8       _fillPercent{ 0 };
        ServerState _state{ ServerState::Open };
    };
} // namespace sw
