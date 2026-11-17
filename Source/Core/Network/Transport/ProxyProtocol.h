/**
 * @file ProxyProtocol.h
 * @brief PROXY protocol v2(HAProxy 명세 2.2) 머리의 해석기입니다. 길이 · 서명 · 판 · 명령 · 주소족을 보는 순수 함수라 퍼징하기 쉽습니다.
 * @details - 머리: 서명 12 B · 판/명령 1 B · 주소족/전송 1 B · 길이(빅엔디언) 2 B · 주소(와 TLV) `길이` B.
 *          - 받는 것: 판 2, 명령 LOCAL(프록시 자신의 건강 검사 — 출처 주소 없음)과 PROXY, PROXY 는 TCP over IPv4(`NetAddress` 가 IPv4 만 나타낸다). IPv6 · UDP · UNIX 는 거절한다.
 *          - 길이 상한(`kMaxHeaderSize`)을 넘거나 주소족이 요구하는 길이보다 짧은 머리는 거절한다 — 거짓 길이로 받기 버퍼를 묶거나 주소 밖을 읽지 못한다.
 *          AWS NLB · GCP · HAProxy 가 보내는 머리이고, 언리얼 · 유니티 백엔드가 L4 프록시 뒤에서 실제 클라이언트 주소를 얻는 표준 길이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @brief 머리 읽기의 결과입니다. */
    enum class ProxyHeaderResult : uint8
    {
        NeedMore = 0, ///< 아직 머리가 다 오지 않았다(지금까지는 맞다)
        Parsed,       ///< 머리를 읽었다 — `ProxyHeader::_headerSize` 바이트 뒤가 실제 스트림이다
        Invalid       ///< 서명 · 판 · 명령 · 주소족 · 길이가 틀렸다 — 연결을 닫는다
    };

    /** @brief PROXY protocol v2 머리 하나입니다. 해석(`parse`)과 쓰기(`appendIpv4`)가 이 형식의 일입니다. */
    struct SW_API ProxyHeader
    {
        static constexpr int32 kSignatureSize   = 12;
        static constexpr int32 kFixedSize       = 16;  ///< 서명 + 판/명령 + 주소족 + 길이
        static constexpr int32 kMaxHeaderSize   = 536; ///< 고정부 + 명세의 최대 주소(UNIX 216 B) + TLV 여유 — 이보다 긴 머리는 거절한다
        static constexpr int32 kIpv4AddressSize = 12;  ///< 출처 · 목적지 주소 4 B 씩 + 포트 2 B 씩

        NetAddress _source{};        ///< 실제 클라이언트 주소(PROXY). LOCAL 이면 비어 있다(`_bLocal`)
        int32      _headerSize{ 0 }; ///< 서명부터 TLV 끝까지의 바이트 수
        uint8      _bLocal{ SW_FALSE };

        /** @brief @p pData 의 앞 @p size 바이트에서 머리를 읽어 @p outHeader 에 둡니다. 부족하면 NeedMore, 틀리면 Invalid 입니다. */
        [[nodiscard]] static ProxyHeaderResult parse( const uint8* pData, int32 size, ProxyHeader& outHeader );
        /** @brief TCP over IPv4 PROXY 머리를 @p outBytes 뒤에 씁니다(테스트 · 프록시 흉내). */
        static void appendIpv4( vector<uint8>& outBytes, const NetAddress& source, const NetAddress& destination );
    };
} // namespace sw
