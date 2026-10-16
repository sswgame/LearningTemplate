/**
 * @file ServerDirectoryProtocol.h
 * @brief 서버 디렉터리의 메서드, 알림, 키트 오류 번호와 본문 형식(`BitWriter`)을 정의합니다.
 * @details 배정 응답은 결과(`ServerDirectoryResult`)를 본문에 싣는다. 점검과 빈 서버는 오류가 아니라 정상 응답이며 클라이언트가 화면에 그린다.
 *          모르는 서버 종류만 키트 오류(`ServerDirectoryError::kUnknownKind`)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 메서드와 알림 번호입니다(영역 `OnlineMethodRange::kServerDirectory`). */
    struct ServerDirectoryMethod
    {
        static constexpr uint32 kWireVersion = 1;

        static constexpr uint16 kGetStatus    = OnlineMethodRange::kServerDirectory + 0x00; ///< 본문은 없고 상태를 돌려준다. 익명으로 쓸 수 있다
        static constexpr uint16 kAssignServer = OnlineMethodRange::kServerDirectory + 0x01; ///< 배정 요청을 받아 배정을 돌려준다. 점검 허용 목록이 계정을 보므로 로그인한 뒤에 쓴다
        static constexpr uint16 kListServers  = OnlineMethodRange::kServerDirectory + 0x02; ///< 종류를 받아 목록을 돌려준다. 서버 고르기 화면이 쓰며 익명으로 쓸 수 있다
        static constexpr uint16 kPushStatus   = OnlineMethodRange::kServerDirectory + 0x80; ///< 보이는 상태가 바뀌었다는 알림이다. 본문은 상태이다
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
    /**
     * @struct ServerDirectoryProtocol
     * @brief 본문 코덱입니다. 읽을 때 상한(`ServerDirectoryLimit`)을 넘으면 false 를 반환합니다.
     */
    struct SW_KIT_API ServerDirectoryProtocol
    {
        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static void                      writeMaintenance( BitWriter& outWriter, const MaintenanceWindow& window, bool bIncludeAllowList );
        [[nodiscard]] static bool        readMaintenance( BitReader& reader, bool bIncludeAllowList, MaintenanceWindow& outWindow );
        static void                      writeNotice( BitWriter& outWriter, const ServiceNotice& notice );
        [[nodiscard]] static bool        readNotice( BitReader& reader, ServiceNotice& outNotice );

        static void               writeStatus( BitWriter& outWriter, const ServerDirectoryStatus& status );
        [[nodiscard]] static bool readStatus( BitReader& reader, ServerDirectoryStatus& outStatus );
        static void               writeAssignmentRequest( BitWriter& outWriter, const ServerAssignmentRequest& request );
        [[nodiscard]] static bool readAssignmentRequest( BitReader& reader, ServerAssignmentRequest& outRequest );
        static void               writeAssignment( BitWriter& outWriter, const ServerAssignment& assignment );
        [[nodiscard]] static bool readAssignment( BitReader& reader, ServerAssignment& outAssignment );
        static void               writeServerList( BitWriter& outWriter, const vector<ServerListEntry>& listEntry );
        [[nodiscard]] static bool readServerList( BitReader& reader, vector<ServerListEntry>& outListEntry );
    };
} // namespace sw
