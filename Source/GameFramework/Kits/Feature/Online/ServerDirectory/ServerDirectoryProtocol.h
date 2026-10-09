/**
 * @file ServerDirectoryProtocol.h
 * @brief 서버 디렉터리의 메서드 · 알림 · 키트 오류 번호와 몸 형식(`BitWriter`)입니다.
 * @details 배정 응답은 결과(`ServerDirectoryResult`)를 몸에 싣는다 — 점검 · 빈 서버는 오류가 아니라 답이다(클라이언트가 화면에 그린다).
 *          모르는 서버 종류만 키트 오류(`ServerDirectoryError::kUnknownKind`)다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/ServerDirectoryTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 메서드 · 알림 번호입니다(영역 `OnlineMethodRange::kServerDirectory`). */
    struct ServerDirectoryMethod
    {
        static constexpr uint32 kWireVersion = 1;

        static constexpr uint16 kGetStatus    = OnlineMethodRange::kServerDirectory + 0x00; ///< 몸 없음 → 상태(익명)
        static constexpr uint16 kAssignServer = OnlineMethodRange::kServerDirectory + 0x01; ///< 배정 요청 → 배정(로그인 뒤 — 점검 허용 목록이 계정을 본다)
        static constexpr uint16 kListServers  = OnlineMethodRange::kServerDirectory + 0x02; ///< 종류 → 목록(익명 — 서버 고르기 화면)
        static constexpr uint16 kPushStatus   = OnlineMethodRange::kServerDirectory + 0x80; ///< 보이는 상태가 바뀜(몸 = 상태)
    };
} // namespace sw

namespace sw
{
    /** @brief 키트 오류 코드입니다(`respondError` — 영역 + n). */
    struct ServerDirectoryError
    {
        static constexpr uint16 kUnknownKind = OnlineMethodRange::kServerDirectory + 0x01; ///< 이 서버가 배정 · 목록을 받지 않는 종류
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerDirectoryProtocol
     * @brief 몸 코덱입니다. 읽기는 상한(`ServerDirectoryLimit`)을 넘거나 넘치면 false.
     */
    struct SW_GF_API ServerDirectoryProtocol
    {
        static void               writeMaintenance( BitWriter& outWriter, const MaintenanceWindow& window, bool bIncludeAllowList );
        [[nodiscard]] static bool readMaintenance( BitReader& reader, bool bIncludeAllowList, MaintenanceWindow& outWindow );
        static void               writeNotice( BitWriter& outWriter, const ServiceNotice& notice );
        [[nodiscard]] static bool readNotice( BitReader& reader, ServiceNotice& outNotice );

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
