/**
 * @file ServerDirectoryProtocol.h
 * @brief 서버 디렉터리의 메서드, 알림, 키트 오류 번호와 본문 형식, 저장소 레코드 코덱을 정의합니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          배정 응답은 결과(`ServerDirectoryResult`)를 본문에 싣는다. 점검과 빈 서버는 오류가 아니라 정상 응답이며 클라이언트가 화면에 그린다.
 *          모르는 서버 종류만 키트 오류(`ServerDirectoryError::kUnknownKind`)이다.
 *          점검 창과 공지의 저장소 레코드는 다른 빌드가 읽으므로 자기 기술형(A)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryTypes.h"

namespace sw
{
    /** @brief 서버 목록 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class ServerDirectoryMethodName : uint8
    {
        GetStatus,
        AssignServer,
        ListServers,
        Count,
    };

    /** @brief 서버 목록 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class ServerDirectoryPushName : uint8
    {
        Status,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 메서드와 알림 번호입니다(영역 `OnlineMethodRange::kServerDirectory`). 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct ServerDirectoryMethod : OnlineMethodTable<ServerDirectoryMethodName, OnlineMethodRange::kServerDirectory, ServerDirectoryPushName>
    {
        static constexpr uint16 kGetStatus    = toMethod( ServerDirectoryMethodName::GetStatus );    ///< 본문은 없고 상태를 돌려준다. 익명으로 쓸 수 있다
        static constexpr uint16 kAssignServer = toMethod( ServerDirectoryMethodName::AssignServer ); ///< 배정 요청을 받아 배정을 돌려준다. 점검 허용 목록이 계정을 보므로 로그인한 뒤에 쓴다
        static constexpr uint16 kListServers  = toMethod( ServerDirectoryMethodName::ListServers );  ///< 종류를 받아 목록을 돌려준다. 서버 고르기 화면이 쓰며 익명으로 쓸 수 있다
        static constexpr uint16 kPushStatus   = toPush( ServerDirectoryPushName::Status );           ///< 보이는 상태가 바뀌었다는 알림이다. 본문은 상태이다
    };
} // namespace sw

namespace sw
{
    /** @brief 키트 오류 코드입니다. `respondError` 로 보내며 영역에 번호를 더한 값입니다. */
    struct ServerDirectoryError
    {
        static constexpr uint16 kUnknownKind = OnlineMethodRange::kServerDirectory + 0x01; ///< 이 서버가 배정과 목록을 받지 않는 종류이다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 목록(`kListServers`)의 요청입니다. */
    REFLECT()
    struct ServerListRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _kind{}; ///< 상한은 `ServerRecord::kMaxNameSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 목록(`kListServers`)의 응답입니다. */
    REFLECT()
    struct ServerListReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerDirectoryLimit::kMaxServerListCount )
        vector<ServerListEntry> _listEntry{}; ///< 상한은 `ServerDirectoryLimit::kMaxServerListCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerDirectoryProtocol
     * @brief 서버 디렉터리의 메시지 타입 목록, 등록 항목, 저장소 레코드 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 상태 응답과 상태 알림의 본문은 `ServerDirectoryStatus` 입니다.
     */
    struct SW_KIT_API ServerDirectoryProtocol
    {
        using MessageTypes = OnlineMessageTypes<ServerDirectoryStatus, ServerAssignmentRequest, ServerAssignment, ServerListRequest, ServerListReply>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 점검 창과 공지의 저장소 레코드를 읽고 씁니다. 둘 다 영속 레코드이므로 자기 기술형(A)이며 레코드 버전을 머리에 둡니다.
         * @details 다른 버전이거나 모르는 필드가 있거나 깨졌으면 손상으로 보고 오류 로그를 남기며 false 입니다(`ServiceRecordUtil::readDurable`).
         *          공지는 선언된 범위도 확인합니다. 점검 창은 고정 배치형 코덱이 없으므로 서버가 읽은 값을 쓰기와 같은 규칙으로 다시 확인합니다.
         */
        [[nodiscard]] static bool encodeMaintenance( const MaintenanceWindow& window, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeMaintenance( string_view key, const vector<uint8>& bytes, MaintenanceWindow& outWindow );
        [[nodiscard]] static bool encodeNotice( const ServiceNotice& notice, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeNotice( string_view key, const vector<uint8>& bytes, ServiceNotice& outNotice );
    };
} // namespace sw
