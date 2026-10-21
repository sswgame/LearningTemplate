/**
 * @file ServerDirectoryTypes.h
 * @brief 서버 디렉터리의 타입입니다. 결과, 점검 창, 공지, 배정 요청과 응답, 서버 목록 항목을 정의합니다.
 * @details 시각은 모두 UTC 유닉스 밀리초(`WallClock`)이다. 끝이 0이면 해제할 때까지이다.
 *          와이어 타입(`ServerDirectoryProtocol::MessageTypes`)은 고정 배치형(B)이고, 저장소 레코드(점검 창, 공지)는 자기 기술형(A)이다.
 *          점검 창의 허용 목록은 클라이언트에 보내지 않으므로, 와이어에는 허용 목록이 없는 타입(`MaintenanceView`)만 싣는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /** @brief 서버 디렉터리 결과입니다. 배정 응답 본문과 변경 완료에 씁니다. */
    ENUM( Count = Count )
    enum class ServerDirectoryResult : uint8
    {
        Ok = 0,
        NoServer,    ///< 받을 서버가 없다. 모두 가득 찼거나 닫혔거나 하트비트가 오래됐다
        Maintenance, ///< 점검 중이다. 끝 시각과 안내 문구 키를 함께 준다
        Invalid,     ///< 모르는 종류이거나 규칙에 맞지 않는 값이다
        Unavailable, ///< 변경할 때 저장소를 쓸 수 없다. 다시 하면 된다
        NotFound,    ///< 변경할 때 없는 공지나 점검이다
        Count
    };

    /** @brief 상한 값들입니다. */
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
    /** @brief 클라이언트에 보이는 점검 창 하나입니다. 허용 목록이 없는 타입이라 상태 응답과 알림에 허용 목록이 실릴 길이 없습니다. */
    REFLECT()
    struct MaintenanceView
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        string _scope{}; ///< `all` 이거나 서버 종류 이름이다. 상한은 `ServerRecord::kMaxNameSize` 와 같다
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxMessageKeySize )
        string _messageKey{}; ///< 안내 문구의 로컬라이제이션 키이다. 상한은 `ServerDirectoryLimit::kMaxMessageKeySize` 와 같다
        PROPERTY()
        int64 _startMs{ 0 };
        PROPERTY()
        int64 _endMs{ 0 }; ///< 0이면 해제할 때까지이다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 점검 창 하나입니다. 범위가 `all` 이면 모든 서버 종류이고, 그 밖은 서버 종류 이름입니다.
     * @details 서버만 씁니다. 저장소 레코드는 자기 기술형(A)이며 와이어에는 싣지 않습니다. 클라이언트에는 `MaintenanceView` 로 보냅니다.
     */
    REFLECT()
    struct MaintenanceWindow
    {
        REFLECT_BODY();
        static constexpr const utf8* kScopeAll = "all";

        /// 점검 중에도 들어갈 수 있는 계정이다. GM 과 테스터가 해당하며 클라이언트에는 보내지 않는다.
        PROPERTY()
        vector<AccountID> _listAllowedAccount{};
        PROPERTY()
        string _scope{};
        PROPERTY()
        string _messageKey{}; ///< 안내 문구의 로컬라이제이션 키이다
        PROPERTY()
        int64 _startMs{ 0 };
        PROPERTY()
        int64 _endMs{ 0 }; ///< 0이면 해제할 때까지이다

        bool isActive( int64 nowMs ) const { return _startMs <= nowMs && ( _endMs == 0 || nowMs < _endMs ); }
        bool appliesTo( string_view kind ) const { return _scope == kScopeAll || _scope == kind; }
        bool allowsAccount( AccountID accountID ) const { return VectorUtil::contains( _listAllowedAccount, accountID ); }
        /** @brief 클라이언트에 보이는 부분입니다. 허용 목록은 빠집니다. */
        MaintenanceView makeView() const { return MaintenanceView{ _scope, _messageKey, _startMs, _endMs }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 공지 하나입니다. 상태 응답(B)과 저장소 레코드(A)가 같은 선언을 씁니다. */
    REFLECT()
    struct ServiceNotice
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxNoticeTextSize )
        string _text{}; ///< `_bLiteralText` 가 false 이면 로컬라이제이션 키이고, true 이면 GM 이 쓴 문구이다. 상한은 `ServerDirectoryLimit::kMaxNoticeTextSize` 와 같다
        PROPERTY()
        uint64 _noticeID{ 0 };
        PROPERTY()
        int64 _startMs{ 0 };
        PROPERTY()
        int64 _endMs{ 0 }; ///< 0이면 내릴 때까지이다
        PROPERTY()
        int32 _priority{ 0 }; ///< 값이 큰 것이 위에 온다
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bLiteralText{ SW_FALSE };

        bool isActive( int64 nowMs ) const { return _startMs <= nowMs && ( _endMs == 0 || nowMs < _endMs ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 보는 상태입니다. 지금 걸린 점검과 기간 안의 공지(우선순위 내림차순)를 담습니다. 상태 응답과 상태 알림의 본문입니다. */
    REFLECT()
    struct ServerDirectoryStatus
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxWindowCount )
        vector<MaintenanceView> _listMaintenance{}; ///< 상한은 `ServerDirectoryLimit::kMaxWindowCount` 와 같다
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxNoticeCount )
        vector<ServiceNotice> _listNotice{}; ///< 상한은 `ServerDirectoryLimit::kMaxNoticeCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 배정 요청입니다. 종류와 지역 상한은 `ServerRecord::kMaxNameSize` 와 같습니다. */
    REFLECT()
    struct ServerAssignmentRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _kind{};
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{};
        PROPERTY()
        uint32 _buildVersion{ 0 }; ///< 0이면 빌드 버전을 따지지 않는다
        PROPERTY( Min = 1, Max = 64 )
        int32 _seatCount{ 1 }; ///< 함께 들어갈 인원이다. 파티가 해당한다. 상한은 `ServerDirectoryLimit::kMaxSeatCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 배정 응답입니다. Ok 이면 접속할 서버를, Maintenance 이면 끝 시각과 안내 문구 키를 담습니다. */
    REFLECT()
    struct ServerAssignment
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerRecord::kMaxAddressSize )
        string _address{}; ///< 상한은 `ServerRecord::kMaxAddressSize` 와 같다
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxMessageKeySize )
        string _messageKey{};
        PROPERTY()
        uint64 _serverID{ 0 };
        PROPERTY()
        int64 _maintenanceEndMs{ 0 };
        PROPERTY()
        uint16 _port{ 0 };
        PROPERTY()
        ServerDirectoryResult _result{ ServerDirectoryResult::NoServer };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 목록 항목 하나입니다. 서버 고르기 화면에 씁니다. */
    REFLECT()
    struct ServerListEntry
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{};
        PROPERTY( MaxCount = ServerRecord::kMaxAddressSize )
        string _address{};
        PROPERTY()
        uint64 _serverID{ 0 };
        PROPERTY()
        uint16 _port{ 0 };
        PROPERTY( Min = 0, Max = 100 )
        uint8 _fillPercent{ 0 };
        PROPERTY()
        ServerState _state{ ServerState::Open };
    };
} // namespace sw
