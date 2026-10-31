/**
 * @file ServerRecord.h
 * @brief 서버 등록 기록입니다. 서버 하나의 설명(id, 종류, 지역, 주소, 빌드 버전, 정원)과 상태(하트비트, 부하, 열림 상태), 캐시 키와 와이어 형식을 둡니다.
 * @details 캐시 키 `sd/srv/<서버 16 진 16>` 은 기록을 가리키며 시한이 있어 하트비트가 멈추면 사라진다.
 *          `sd/idx/<종류>` 는 정렬 집합이다(멤버는 서버 16 진, 점수는 하트비트 초).
 *          종류와 지역은 `[0-9a-z_-]` 16 바이트 이하다("game", "chat", "match", "kr", "eu-west"). 주소는 클라이언트가 접속하는 호스트 이름이나 IP 문자열이다.
 *          기록 첫 바이트는 형식 버전(2 — 게임 포트 수가 생겼다)이고, 다른 버전은 읽지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 서버의 열림 상태입니다. 배정은 Open 만 받는다(점검 허용 계정은 Maintenance 도). 서버 목록 응답에 실린다. */
    ENUM( Count = Count )
    enum class ServerState : uint8
    {
        Starting = 0, ///< 기동 중이라 아직 받지 않는다
        Open,
        Draining,    ///< 새 손님은 받지 않고 있는 손님은 둔다(종료 전)
        Maintenance, ///< 점검 중이라 허용 계정만 받는다
        Count
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서버 하나의 설명(띄울 때 정해진다)입니다.
     * @details 게임 포트는 `_port` 부터 `_portCount` 개다(포트 샤딩 — `ServerConfig::_gamePortShardCount`, 한 포트를 나누면 1). 배정은 `pickPort` 로 계정마다 한 포트를 고른다.
     */
    struct ServerDescriptor
    {
        /** @brief 게임 포트 수의 상한입니다. `ServerConfig::_gamePortShardCount` 의 상한과 같습니다. */
        static constexpr int32 kMaxPortCount = 64;

        string _kind{};
        string _region{};
        string _address{};
        uint64 _serverID{ 0 };
        uint32 _buildVersion{ 0 };
        int32  _capacity{ 0 };
        uint16 _port{ 0 };
        uint8  _portCount{ 1 }; ///< 1 ~ `kMaxPortCount`. 기록을 읽을 때 범위 밖은 거절한다

        /**
         * @brief @p accountID 가 붙을 게임 포트입니다. 계정마다 늘 같은 샤드라 다시 배정받아도 같은 포트이고, 계정이 고르게 퍼진다(나머지 연산).
         * @details 포트 수 0 은 만들 수 없는 값이다 — 기록을 읽을 때 거절하고(`ServerRecord::readStatus`), 서버는 설정의 1 이상을 싣는다.
         */
        uint16 pickPort( uint64 accountID ) const { return pickShardPort( _port, _portCount, accountID ); }
        /** @brief 첫 포트 @p firstPort 부터 @p portCount 개인 서버에서 @p accountID 의 포트입니다. 배정(디렉터리 · 매칭 · 백필)이 모두 이 규칙 하나를 쓴다. */
        static uint16 pickShardPort( uint16 firstPort, uint8 portCount, uint64 accountID )
        {
            SW_ASSERT( portCount > 0 );
            return static_cast<uint16>( firstPort + accountID % portCount );
        }
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
        static constexpr uint8 kFormatVersion  = 2; ///< 2: 설명에 게임 포트 수(`ServerDescriptor::_portCount`)

        static string             makeRecordKey( uint64 serverID );
        static string             makeIndexKey( string_view kind );
        static string             makeMember( uint64 serverID );
        [[nodiscard]] static bool parseMember( string_view member, uint64& outServerID );
        /** @brief 종류 · 지역 이름 규칙(`[0-9a-z_-]`, 1..16 바이트)을 지키는가입니다. */
        static bool isValidName( string_view name );

        static void               writeStatus( BitWriter& outWriter, const ServerStatus& status );
        [[nodiscard]] static bool readStatus( BitReader& reader, ServerStatus& outStatus );
        static vector<uint8>      encode( const ServerStatus& status );
        [[nodiscard]] static bool decode( const vector<uint8>& bytes, ServerStatus& outStatus );
    };
} // namespace sw
