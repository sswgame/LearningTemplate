/**
 * @file NetSlotTable.h
 * @brief `NetEndpoint` 의 연결 슬롯 표와 주소에서 슬롯으로 가는 해시 맵입니다.
 * @details 슬롯 번호가 곧 연결 ID다. 클라이언트는 슬롯 하나(서버 연결, ID 0), 서버는 `_maxConnections` 개를 쓴다.
 *          주소 맵은 패킷마다 슬롯을 O(1)로 찾게 한다. 맵에는 지금 그 주소를 가진 슬롯만 있다(`bindAddress` 와 `unbindAddress` 가 짝을 맞춘다).
 *
 *          **살아 있는 슬롯 · 빈 슬롯 목록**(감사 N-8): 슬롯의 열기 · 닫기 · 놓기(`open` · `close` · `release`)는 여기서만 상태를 바꾼다.
 *          살아 있는(Connecting · Connected) 슬롯은 번호 순으로 정렬된 조밀한 목록에 다음에 볼 시각(`NetLiveSlot::_dueTime`)과 함께 있고,
 *          빈 슬롯은 내림차순 목록의 끝이 가장 낮은 번호다. 그래서 갱신은 정원(`_maxConnections`)이 아니라 기한이 된 연결만 보고,
 *          위조 요청마다 빈 슬롯을 선형으로 찾지 않는다. 열기 · 닫기는 목록을 밀어 O(N) 이지만 드물다.
 *
 *          **연결 객체는 열 때 만든다**: `NetConnection`(신뢰 창 · 순서 버퍼 — 약 75 KB)은 `open` 이 만들고 `close` 가 놓는다. 빈 슬롯은 몇십 바이트라
 *          정원이 커도 연결된 수만큼만 메모리를 쓴다. 언리얼(`UNetConnection` 을 연결마다 NewObject) · GameNetworkingSockets(연결마다 new)와 같고,
 *          ENet 처럼 `peerCount` 개를 미리 잡지 않는다. 풀은 두지 않는다 — 열기는 챌린지를 통과한 응답에만 일어나 드물다.
 *          언리얼은 연결 객체 배열(`ClientConnections`)만 돌고 빈 칸이 없다 — 슬롯 번호가 곧 연결 ID 인 이 구조에서 같은 효과를 내는 모양이다.
 *
 *          **스레드**: 잠금이 없다. `NetEndpoint` 의 값 멤버로 그 잠금 안에서만 쓴다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetPacketSealer.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @brief `NetEndpoint` 의 연결 슬롯 하나입니다. 소멸시키는 번역 단위는 `INetSecurityProvider.h` 를 include 해야 합니다(`NetSlotSecurity`). */
    struct NetEndpointSlot
    {
        unique_ptr<NetSlotSecurity> _security{};   ///< 암호화 방식일 때만 있다.
        unique_ptr<NetConnection>   _connection{}; ///< 살아 있는(Connecting · Connected) 동안만 있다(`NetSlotTable::open` 이 만들고 `close` 가 놓는다).
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
        /**
         * @brief 이 슬롯의 Disconnected 사건을 게임 스레드가 아직 꺼내지 않았다 — 그때까지 다른 연결에 주지 않는다(`NetSlotTable::findFreeSlot`).
         * @details 슬롯 번호가 곧 연결 ID 라, 끊긴 A 의 번호를 같은 갱신에서 B 가 받으면 게임 스레드가 A 에게 보내려던 상태 · 스냅샷이 B 에게 갔다(감사 P0-8).
         *          게임 스레드가 끊김을 알기 전에는 그 번호가 A 다(언리얼은 UNetConnection 객체가 신원, 유니티 NGO 는 단조 증가 clientId).
         */
        uint8 _bReleasePending{ SW_FALSE };

        /** @brief 연결 값입니다. 두 솔트를 섞은 값으로, 같은 주소의 옛 연결이나 위조 패킷을 걸러 냅니다. */
        uint64 getConnectionToken() const { return _clientSalt ^ _serverSalt; }
    };
} // namespace sw

namespace sw
{
    /** @brief 살아 있는(Connecting · Connected) 슬롯 하나와 갱신이 그 슬롯을 다시 볼 시각입니다. */
    struct NetLiveSlot
    {
        float64 _dueTime{ 0.0 }; ///< 이 시각이 되면 `NetEndpoint::updateSlots` 가 본다. 음의 무한대면 다음 갱신에서 바로 본다(`touch`).
        int32   _slotIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSlotTable
     * @brief 연결 슬롯과 주소 맵을 함께 쥡니다. 슬롯을 비우거나 다시 채우는 일은 맵과 짝을 맞춰 여기서만 합니다.
     */
    class SW_API NetSlotTable
    {
    public:
        NetSlotTable();
        ~NetSlotTable();

        NetSlotTable( const NetSlotTable& )            = delete;
        NetSlotTable& operator=( const NetSlotTable& ) = delete;

        /** @brief 모든 슬롯을 버리고 비어 있는(Disconnected) 슬롯 @p slotCount 개로 다시 채웁니다. 주소 맵 · 살아 있는 목록도 비우고 모두 빈 슬롯이 됩니다. */
        void reset( int32 slotCount );

        /** @brief 빈 슬롯 @p slotIndex 를 @p state(Connecting · Connected)로 열어 살아 있는 목록에 넣고 새 연결 객체를 만듭니다. 다음 갱신에서 바로 봅니다. 빈 슬롯이 아니면 단언입니다. */
        void open( int32 slotIndex, NetConnectionState state );
        /**
         * @brief 살아 있는 슬롯을 닫습니다(Disconnected, 살아 있는 목록에서 빼고 연결 객체를 놓는다 — 받은 메시지 · 신뢰 창도 함께 버린다).
         * @param bHoldUntilReleased 참이면 게임 스레드가 끊김 사건을 꺼낼 때까지(`release`) 빈 슬롯으로 돌리지 않는다(`NetEndpointSlot::_bReleasePending`).
         */
        void close( int32 slotIndex, bool bHoldUntilReleased );
        /** @brief 닫힌 뒤 붙잡아 둔 슬롯을 빈 슬롯으로 돌립니다. 붙잡지 않았거나 이미 놓았으면 아무것도 하지 않습니다. */
        void release( int32 slotIndex );
        /** @brief 이 슬롯을 다음 갱신에서 보게 합니다(받은 패킷 · 넣은 메시지가 보낼 것을 만들었다). 살아 있지 않으면 아무것도 하지 않습니다. */
        void touch( int32 slotIndex );
        /** @brief 살아 있는 슬롯 모두를 다음 갱신에서 보게 합니다(시계를 민 뒤처럼 기한이 모두 바뀌었을 때). */
        void touchAll();
        /** @brief 살아 있는 슬롯 @p slotIndex 를 다시 볼 시각을 정합니다. 살아 있지 않으면 단언입니다. */
        void setDueTime( int32 slotIndex, float64 dueTime );
        /** @brief 살아 있는 슬롯 가운데 가장 이른 기한입니다. 살아 있는 슬롯이 없으면 양의 무한대입니다(`NetEndpoint` 가 다음 갱신 시각을 정한다). */
        float64 getNextDueTime() const;
        /** @brief 기한(@p nowSeconds 이하)이 된 살아 있는 슬롯 번호를 @p outListSlot 에 비운 뒤 번호 순으로 담습니다. */
        void collectDue( float64 nowSeconds, vector<int32>& outListSlot ) const;
        /** @brief 살아 있는 슬롯 목록(번호 순)입니다. 받은 메시지 꺼내기 · 보내기처럼 연결된 것만 도는 쪽이 씁니다. */
        const vector<NetLiveSlot>& getLiveSlots() const { return _listLiveSlot; }

        int32                  getCount() const { return static_cast<int32>( _listSlot.size() ); }
        bool                   isValid( int32 slotIndex ) const { return 0 <= slotIndex && slotIndex < getCount(); }
        NetEndpointSlot&       get( int32 slotIndex ) { return _listSlot[static_cast<size_t>( slotIndex )]; }
        const NetEndpointSlot& get( int32 slotIndex ) const { return _listSlot[static_cast<size_t>( slotIndex )]; }
        /** @brief 연결된(Connected) 슬롯이면 그 슬롯, 아니면 nullptr 입니다. 범위 밖 번호도 nullptr 입니다. */
        NetEndpointSlot*       findConnected( int32 slotIndex );
        const NetEndpointSlot* findConnected( int32 slotIndex ) const;
        NetEndpointSlot*       begin() { return _listSlot.data(); }
        NetEndpointSlot*       end() { return _listSlot.data() + _listSlot.size(); }
        const NetEndpointSlot* begin() const { return _listSlot.data(); }
        const NetEndpointSlot* end() const { return _listSlot.data() + _listSlot.size(); }

        /** @brief 이 주소에 묶인 슬롯 번호입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<int32> findByAddress( const NetAddress& address ) const;
        /** @brief 슬롯의 주소를 정하고 주소 맵에 묶습니다. */
        void bindAddress( int32 slotIndex, const NetAddress& address );
        /** @brief 슬롯의 주소를 주소 맵에서 풉니다. 같은 주소가 다른 슬롯에 묶여 있으면 그대로 둡니다. */
        void unbindAddress( int32 slotIndex );
        /** @brief 가장 낮은 빈(Disconnected, 끊김 사건을 게임 스레드가 꺼낸) 슬롯 번호입니다. 없으면(가득 참) 비어 있습니다. O(1) 입니다. */
        [[nodiscard]] optional<int32> findFreeSlot() const;
        int32                         countConnected() const;
        /** @brief 닫혔지만 게임 스레드가 끊김 사건을 꺼내지 않아 붙잡아 둔 슬롯 수입니다(`NetEndpointSlot::_bReleasePending`). 곧 빈 슬롯이 된다. */
        int32 countReleasePending() const { return getCount() - static_cast<int32>( _listLiveSlot.size() ) - static_cast<int32>( _listFreeSlot.size() ); }
        /** @brief 연결된 슬롯 번호를 @p outListConnection 에 비운 뒤 담습니다. */
        void collectConnected( vector<int32>& outListConnection ) const;

        /** @brief 주소 맵의 키입니다(IPv4 와 포트). */
        static uint64 makeAddressKey( const NetAddress& address ) { return ( static_cast<uint64>( address._ipv4 ) << 16 ) | address._port; }

    private:
        /** @brief 살아 있는 목록에서 @p slotIndex 의 자리입니다(정렬돼 있어 이분 탐색). 없으면 비어 있습니다. */
        [[nodiscard]] optional<size_t> findLive( int32 slotIndex ) const;

    private:
        vector<NetEndpointSlot>      _listSlot;
        vector<NetLiveSlot>          _listLiveSlot;     ///< 살아 있는 슬롯, 번호 오름차순 — 순서를 번호 순으로 두어 사건 · 메시지를 꺼내는 순서가 예전 전 슬롯 훑기와 같다.
        vector<int32>                _listFreeSlot;     ///< 빈 슬롯, 번호 내림차순 — 끝이 가장 낮은 번호다.
        unordered_map<uint64, int32> _mapSlotByAddress; ///< 연결이 많아도 패킷마다 선형 탐색하지 않게 한다.
    };
} // namespace sw
