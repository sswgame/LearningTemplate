/**
 * @file NetProtocol.h
 * @brief 와이어 형식의 판 · 프로토콜 ID · 패킷 크기 한도를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include <initializer_list>

namespace sw
{
    /** @brief 한 UDP 패킷의 최대 바이트입니다. IPv4와 IPv6의 MTU 안에 들어가 IP 조각화가 일어나지 않게 합니다. */
    constexpr int32 kNetMaxPacketSize = 1200;

    /**
     * @struct NetWireVersion
     * @brief 와이어 형식의 버전입니다. 계층마다 하나씩 있으며, Core(핸드셰이크, 패킷, 채널, 메시지 헤더)는 `kCore`, 키트는 메시지 표의 해시(`NetMessageTable::computeWireVersion`), 게임은 자기 값을 씁니다.
     * @details 형식(필드, 의미, 종류 바이트)을 바꾸는 커밋은 그 계층의 버전을 올린다. 버전이 다른 빌드끼리는 핸드셰이크에서 `VersionMismatch` 로 갈리며,
     *          옛 형식을 읽는 경로는 두지 않는다. 게임과 키트 버전은 `combine` 으로 묶어 `NetEndpointConfig::_wireVersion` 에 둔다(Core 버전은 자동으로 섞인다).
     */
    struct NetWireVersion
    {
        static constexpr uint32 kCore          = 0;
        static constexpr int32  kValueBitCount = 32; ///< 판 값 하나의 비트 수 — 바이트씩 섞는다

        /** @brief 계층별 버전을 값 하나로 섞습니다. FNV-1a 이므로 순서도 결과에 반영됩니다. */
        static constexpr uint32 combine( std::initializer_list<uint32> listVersion )
        {
            uint32 hash = HashUtil::kFNVOffset32;
            for ( const uint32 version : listVersion )
            {
                for ( int32 shift = 0; shift < kValueBitCount; shift += 8 )
                {
                    hash = HashUtil::fnvStep32( hash, ( version >> shift ) & 0xFFu );
                }
            }
            return hash;
        }
    };

    /** @brief 프로토콜 ID에 섞는 기능입니다. 한쪽만 켜면 프로토콜 ID가 달라 `SecurityMismatch` 로 거절되므로, 평문으로 내려가는 경로가 없습니다. */
    struct NetProtocolFeature
    {
        static constexpr uint32 kEncrypted  = 1u << 0;                  ///< X25519와 패킷 AEAD(`NetSecurityMode::Encrypted`).
        static constexpr uint32 kTokenBound = 1u << 1;                  ///< 세션 토큰 결속(서버 인증기, 클라이언트 자격).
        static constexpr uint32 kAllMask    = kEncrypted | kTokenBound; ///< 거절 이유를 가릴 때 확인하는 범위.
    };

    /**
     * @struct NetProtocol
     * @brief 프로토콜 ID입니다. 게임 ID와 와이어 버전(Core 버전, 게임과 키트 버전)을 섞은 값이며, 패킷 헤더와 체크섬에 들어가 다른 버전이나 다른 게임의 패킷을 걸러 냅니다.
     */
    struct NetProtocol
    {
        static constexpr uint32 kDefaultGameID = 0x53574E31u;
        /** @brief 연결 요청과 거절 패킷의 헤더 값입니다. 버전이 달라도 읽혀야 하므로 프로토콜 ID 대신 이 고정 값으로 싸며, 두 패킷의 배치는 버전과 함께 바꾸지 않습니다. */
        static constexpr uint32 kHandshakeID = 0x5357484Bu;

        /** @brief 프로토콜 ID를 만듭니다. @p featureMask 는 `NetProtocolFeature` 이며, 양쪽이 같아야 연결됩니다(협상하지 않습니다). */
        static constexpr uint32 makeProtocolID( uint32 gameID, uint32 wireVersion, uint32 featureMask = 0 )
        {
            const uint32 protocolID = NetWireVersion::combine( { gameID, NetWireVersion::kCore, wireVersion, featureMask } );
            return protocolID != kHandshakeID ? protocolID : protocolID ^ 1u;
        }
    };
} // namespace sw
