/**
 * @file NetSlotTable.h
 * @brief `NetHost` 의 연결 슬롯 표와 주소에서 슬롯으로 가는 해시 맵입니다.
 * @details 슬롯 번호가 곧 연결 ID다. 클라이언트는 슬롯 하나(서버 연결, ID 0), 서버는 `_maxConnections` 개를 쓴다.
 *          주소 맵은 패킷마다 슬롯을 O(1)로 찾게 한다. 맵에는 지금 그 주소를 가진 슬롯만 있다(`bindAddress` 와 `unbindAddress` 가 짝을 맞춘다).
 *
 *          **스레드**: 잠금이 없다. `NetHost` 의 값 멤버로 그 잠금 안에서만 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetPacketSealer.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @brief `NetHost` 의 연결 슬롯 하나입니다. 소멸시키는 번역 단위는 `INetSecurityProvider.h` 를 include 해야 합니다(`NetSlotSecurity`). */
    struct NetHostSlot
    {
        unique_ptr<NetSlotSecurity> _security{}; ///< 암호화 방식일 때만 있다.
        NetConnection               _connection{};
        NetAddress                  _address{};
        uint64                      _clientSalt{ 0 };
        uint64                      _serverSalt{ 0 };
        float64                     _lastReceiveTime{ 0.0 };
        float64                     _lastSendTime{ -1.0 };
        float64                     _connectStartTime{ 0.0 };
        float64                     _sendCredit{ 0.0 };                         ///< 대역폭 토큰(바이트). 0보다 크면 꽉 찬 패킷을 보낼 수 있다. 보낸 뒤에는 음수가 될 수 있다.
        float64                     _lastCreditTime{ -1.0 };                    ///< 토큰을 마지막으로 채운 시각. 음수면 아직 채운 적이 없다는 뜻이며, 연결되면 가득 채운다.
        NetConnectionState          _state{ NetConnectionState::Disconnected }; ///< 서버 슬롯은 Connecting 을 거치지 않는다. 응답이 맞아야 슬롯을 할당하기 때문이다.
        uint8                       _bRemoteDebugHold{ SW_FALSE };              ///< 상대가 마지막 데이터 패킷에서 디버거 대기를 알렸다(개발 빌드만 읽는다).

        /** @brief 연결 값입니다. 두 솔트를 섞은 값으로, 같은 주소의 옛 연결이나 위조 패킷을 걸러 냅니다. */
        uint64 getConnectionToken() const { return _clientSalt ^ _serverSalt; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSlotTable
     * @brief 연결 슬롯과 주소 맵을 함께 쥡니다. 슬롯을 비우거나 다시 채우는 일은 맵과 짝을 맞춰 여기서만 합니다.
     */
    class NetSlotTable
    {
    public:
        NetSlotTable();
        ~NetSlotTable();

        NetSlotTable( const NetSlotTable& )            = delete;
        NetSlotTable& operator=( const NetSlotTable& ) = delete;

        /** @brief 모든 슬롯을 버리고 비어 있는(Disconnected) 슬롯 @p slotCount 개로 다시 채웁니다. 주소 맵도 비웁니다. */
        void reset( int32 slotCount );

        int32              getCount() const { return static_cast<int32>( _listSlot.size() ); }
        bool               isValid( int32 slotIndex ) const { return 0 <= slotIndex && slotIndex < getCount(); }
        NetHostSlot&       get( int32 slotIndex ) { return _listSlot[static_cast<size_t>( slotIndex )]; }
        const NetHostSlot& get( int32 slotIndex ) const { return _listSlot[static_cast<size_t>( slotIndex )]; }
        /** @brief 연결된(Connected) 슬롯이면 그 슬롯, 아니면 nullptr 입니다. 범위 밖 번호도 nullptr 입니다. */
        NetHostSlot*       findConnected( int32 slotIndex );
        const NetHostSlot* findConnected( int32 slotIndex ) const;
        NetHostSlot*       begin() { return _listSlot.data(); }
        NetHostSlot*       end() { return _listSlot.data() + _listSlot.size(); }
        const NetHostSlot* begin() const { return _listSlot.data(); }
        const NetHostSlot* end() const { return _listSlot.data() + _listSlot.size(); }

        /** @brief 이 주소에 묶인 슬롯 번호입니다. 없으면 -1 입니다. */
        int32 findByAddress( const NetAddress& address ) const;
        /** @brief 슬롯의 주소를 정하고 주소 맵에 묶습니다. */
        void bindAddress( int32 slotIndex, const NetAddress& address );
        /** @brief 슬롯의 주소를 주소 맵에서 풉니다. 같은 주소가 다른 슬롯에 묶여 있으면 그대로 둡니다. */
        void unbindAddress( int32 slotIndex );
        /** @brief 첫 빈(Disconnected) 슬롯 번호입니다. 없으면 -1 입니다. */
        int32 findFreeSlot() const;
        int32 countConnected() const;
        /** @brief 연결된 슬롯 번호를 @p outListConnection 에 비운 뒤 담습니다. */
        void collectConnected( vector<int32>& outListConnection ) const;

        /** @brief 주소 맵의 키입니다(IPv4 와 포트). */
        static uint64 makeAddressKey( const NetAddress& address ) { return ( static_cast<uint64>( address._ipv4 ) << 16 ) | address._port; }

    private:
        vector<NetHostSlot>          _listSlot;
        unordered_map<uint64, int32> _mapSlotByAddress; ///< 연결이 많아도 패킷마다 선형 탐색하지 않게 한다.
    };
} // namespace sw
