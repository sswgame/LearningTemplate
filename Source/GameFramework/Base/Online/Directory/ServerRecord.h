/**
 * @file ServerRecord.h
 * @brief 서버 등록 기록 — 서버 하나의 설명(id · 종류 · 지역 · 주소 · 빌드 판 · 정원)과 상태(하트비트 · 부하 · 열림 상태), 캐시 키와 와이어 형식입니다.
 * @details 캐시: `sd/srv/<서버 16 진 16>` → 기록(시한 — 하트비트가 멈추면 사라진다), `sd/idx/<종류>` 정렬 집합(멤버 = 서버 16 진, 점수 = 하트비트 초).
 *          종류 · 지역은 `[0-9a-z_-]` 16 바이트 이하("game" · "chat" · "match", "kr" · "eu-west"). 주소는 클라이언트가 붙는 호스트 이름 또는 IP 글.
 *          기록 첫 바이트는 형식 판(1) — 다른 판은 읽지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 서버의 열림 상태입니다. 배정은 Open 만 받는다(점검 허용 계정은 Maintenance 도). */
    enum class ServerState : uint8
    {
        Starting = 0, ///< 띄우는 중 — 아직 받지 않는다
        Open,
        Draining,    ///< 새 손님은 받지 않고 있는 손님은 둔다(내리기 전)
        Maintenance, ///< 점검 — 허용 계정만
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 하나의 설명(띄울 때 정해진다)입니다. */
    struct ServerDescriptor
    {
        string _kind{};
        string _region{};
        string _address{};
        uint64 _serverId{ 0 };
        uint32 _buildVersion{ 0 };
        int32  _capacity{ 0 };
        uint16 _port{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 하나의 상태입니다. */
    struct ServerStatus
    {
        ServerDescriptor _descriptor{};
        int64            _heartbeatMs{ 0 }; ///< 마지막으로 기록을 쓴 UTC 밀리초
        int32            _load{ 0 };        ///< 지금 손님 수(정원과 같은 단위)
        ServerState      _state{ ServerState::Starting };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerRecord
     * @brief 키 · 형식입니다.
     */
    struct SW_GF_API ServerRecord
    {
        static constexpr int32 kMaxNameSize    = 16;
        static constexpr int32 kMaxAddressSize = 128;
        static constexpr uint8 kFormatVersion  = 1;

        static string             makeRecordKey( uint64 serverId );
        static string             makeIndexKey( string_view kind );
        static string             makeMember( uint64 serverId );
        [[nodiscard]] static bool parseMember( string_view member, uint64& outServerId );
        /** @brief 종류 · 지역 이름 규칙(`[0-9a-z_-]`, 1..16 바이트)을 지키는가입니다. */
        static bool isValidName( string_view name );

        static void               writeStatus( BitWriter& outWriter, const ServerStatus& status );
        [[nodiscard]] static bool readStatus( BitReader& reader, ServerStatus& outStatus );
        static vector<uint8>      encode( const ServerStatus& status );
        [[nodiscard]] static bool decode( const vector<uint8>& bytes, ServerStatus& outStatus );
    };
} // namespace sw
