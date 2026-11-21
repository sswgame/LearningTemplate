/**
 * @file NetSnapshot.h
 * @brief 한 틱의 엔티티 상태(엔티티 id, 종류, 바이트)를 모은 스냅샷과 그 델타 직렬화입니다.
 * @details 서버와 클라이언트가 같은 코드로 쓰고 읽는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/Replication/NetInputWindow.h"

#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 스냅샷 확인(클라이언트 → 서버) 본문입니다 — 복원한 가장 새 틱. */
    REFLECT()
    struct NetSnapshotAckBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _tick{ 0 };
    };

    /**
     * @brief 클라이언트-서버 복제 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`).
     * @details 확인 본문은 고정 배치형(`NetMessageBodyDef`)이다. 스냅샷(기준 틱 델타 · 비트 예산)과 입력(입력 창 형식 `kInputFormat`)은 손 본문이다.
     */
    struct NetClientServerMessage
    {
        using Snapshot    = NetMessageDef<0, NetMessageDirection::ToClient, NetChannelKind::UnreliableSequenced>;
        using SnapshotAck = NetMessageBodyDef<1, NetMessageDirection::ToServer, NetChannelKind::UnreliableSequenced, NetSnapshotAckBody>; ///< 옛 확인은 새 확인이 덮는다
        using Input       = NetMessageDef<2, NetMessageDirection::ToServer, NetChannelKind::Unreliable, 6>;                               ///< 비신뢰 — 입력 창을 겹쳐 보내고 서버가 틱 번호로 거르므로 순서 보장이 필요 없다(언리얼 이동 RPC 와 같다).                      ///< 판 6: 의도 끝에 발동 순간 시선 필드(선택)가 붙는다(5: 입력 항목마다의 보던 틱 스탬프, 4: 소유자 상태 필드)
        using Table       = NetMessageTable<NetKitMessageRange::kClientServer, Snapshot, SnapshotAck, Input>;

        static constexpr uint8 kSnapshot    = Table::getKindOf<Snapshot>();
        static constexpr uint8 kSnapshotAck = Table::getKindOf<SnapshotAck>();
        static constexpr uint8 kInput       = Table::getKindOf<Input>();

        static constexpr int32 kMaxInputBytes      = 255; ///< 틱 하나의 입력 상한. 클라이언트는 넘는 입력을 보내지 않고, 서버는 넘는 길이를 손상된 메시지로 본다
        static constexpr int32 kMaxInputCount      = 32;  ///< 입력 메시지 하나에 싣는 입력 수의 상한이자, 클라이언트가 확인을 기다리며 보관하는 입력 수의 상한이다. 두 쪽이 같은 값을 쓴다
        static constexpr int32 kViewTickStampScale = 256; ///< 입력 항목 스탬프는 그 입력을 만들 때 보던 틱(소수) × 이 값이다(1/256 틱 단위)
        static constexpr int32 kMaxClaimsPerIntent = 8;   ///< 의도 입력 하나(`ReplicationClient::sendIntent`)에 싣는 맞음 주장의 상한. 의도 + 주장이 `kMaxInputBytes` 안에 든다
        /**
         * @brief 입력 묶음의 전송 형식입니다. 길이를 붙인 덩어리로 보내고, 항목마다 스탬프(그 입력을 만들 때 보던 틱 × `kViewTickStampScale`)를 싣습니다.
         * @details 스탬프가 항목마다라서 다시 보낸 옛 입력도 그 입력의 보던 틱으로 랙 보정한다(메시지마다 하나면 옛 입력을 최신 틱으로 판정한다). 형식을 바꾸면 와이어 버전을 올린다.
         */
        static constexpr NetInputFormat kInputFormat{ 0, kMaxInputBytes, kMaxInputCount, SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 엔티티 하나의 상태입니다. 바이트의 뜻은 게임이 정하며, 위치와 체력 같은 값을 `BitWriter` 로 양자화해 넣습니다. */
    struct NetEntityState
    {
        vector<uint8> _buffer{};
        uint32        _entityID{ 0 };
        uint32        _typeID{ 0 }; ///< 클라이언트가 어떤 오브젝트를 생성할지 정하는 프리팹 번호
        /**
         * @brief 상태 판 번호입니다. 서버(`ReplicationServer::endTick`)만 채우고 클라이언트 복원본은 0 입니다.
         * @details 종류나 바이트가 지난 틱과 다르면 서버 전체에서 하나씩 오르는 새 번호를 받는다 — 같은 판이면 같은 상태이고, 엔티티를 지웠다 같은 id 로 다시 만들어도
         *          옛 판과 겹치지 않는다. "안 바뀜" 은 바이트 비교가 아니라 이 번호 비교다(언리얼 Iris 의 상태 바뀜 표시 · 소스 엔진의 엔티티 변경 틱 자리).
         *          64 비트라 틱마다 엔티티 수천이 바뀌어도 감기지 않는다.
         */
        uint64 _revision{ 0 };
        /**
         * @brief 마지막으로 깨어난 서버 틱입니다 — 판이 오른 틱이거나 정책이 관련 여부가 바뀌었다고 알린 틱입니다. 서버만 채웁니다.
         * @details 이 틱에서 `ReplicationServerConfig::_dormancyTicks` 가 지나면 휴면이다(`ReplicationServer` 머리말). 판을 올리는 자리(`endTick`)가 함께 정하므로 깨우는 호출을 따로 두지 않는다.
         */
        uint32 _wakeTick{ 0 };
    };

    /** @brief 연결에 보낸 엔티티 하나의 기록입니다 — 바이트 없이 판만 듭니다(퀘이크 3 · 소스 엔진의 클라이언트 프레임이 엔티티 번호만 들듯). */
    struct NetSentEntity
    {
        uint64 _revision{ 0 };
        uint32 _entityID{ 0 };
    };

    /**
     * @brief 서버가 연결 하나에 보낸 스냅샷의 기록입니다. 받는 쪽이 복원한 스냅샷과 같은 엔티티를 같은 판으로 듭니다. 받는 쪽이 확인하면 다음 델타의 기준입니다.
     * @details 연결당 기록 메모리는 엔티티 바이트 합이 아니라 엔티티 수에 비례한다. 와이어가 바뀐 엔티티를 통째로 실으므로 기준의 바이트는 쓸 일이 없다 —
     *          기준 바이트가 필요해지는 칸 델타(측정 뒤)에서 서버 월드의 판별 바이트 저장소를 둔다(언리얼 Iris 의 공유 상태 버퍼).
     */
    struct SW_KIT_API NetSentSnapshot
    {
        vector<NetSentEntity> _listEntity{}; ///< 엔티티 id 오름차순
        uint32                _tick{ 0 };

        const NetSentEntity* findEntity( uint32 entityID ) const;
    };

    /**
     * @brief 서버가 연결 하나에 쓰는 델타의 재료입니다. 월드 목록을 연결마다 복사하지 않고 번호로 가리킵니다.
     * @details 예전에는 연결마다 관련 엔티티의 상태 바이트를 스크래치 스냅샷에 복사했다.
     */
    struct NetDeltaSource
    {
        const vector<NetEntityState>& _listWorld;    ///< 이번 틱 월드(id 오름차순, `ReplicationServer::endTick`)
        const vector<uint32>&         _listRelevant; ///< 받는 쪽이 가져야 할 엔티티의 `_listWorld` 번호(오름차순). 기준에 있고 여기 없으면 사라진 엔티티로 싣는다
        const vector<int32>&          _listOrder;    ///< 이번에 실을 후보 — `_listRelevant` 의 인덱스이며 우선도가 높은 것이 먼저다. 없는 항목은 기준 값 그대로다
        const NetOwnerState&          _ownerState;   ///< 받는 클라이언트가 조종하는 엔티티의 상태와 그 입력 틱
        uint32                        _tick{ 0 };
        uint32                        _firstMissingInputTick{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 받는 클라이언트가 조종하는 엔티티의 예측용 상태와, 그 상태를 만든 마지막 입력 틱입니다. 클라이언트 예측이 이 둘을 한 쌍으로 맞춥니다.
     * @details 틱과 상태를 한 칸에 묶는다. 예전에는 틱만 스냅샷 머리에 싣고 상태는 엔티티 목록에서 찾았는데,
     *          엔티티는 예산 · 우선도 · 관련 정책으로 빠지거나 지난 기준 값으로 남을 수 있어 "새 틱 + 옛 상태" 를 비교하는 헛된 재조정이 났다.
     *          이 칸은 예산 순서 밖에서 스냅샷마다 통째로 싣는다(델타 없음, `NetSnapshot::kMaxEntityBytes` 이하). 언리얼의 자율 프록시 보정(ClientAdjustPosition)과 같은 자리이다.
     */
    struct NetOwnerState
    {
        vector<uint8> _buffer{};
        uint32        _inputTick{ 0 };       ///< 서버가 이 상태를 만들 때 쓴 마지막 입력 틱
        uint8         _bPresent{ SW_FALSE }; ///< 서버가 이 클라이언트의 상태를 아직 정하지 않았으면 false 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱의 엔티티 상태 모음입니다. 엔티티 id 오름차순으로 보관하며, 델타가 두 스냅샷을 나란히 순회합니다. */
    struct SW_KIT_API NetSnapshot
    {
        /** @brief 엔티티 하나의 상태 바이트 상한입니다. 넘는 엔티티는 싣지 않으며, 복원한 스냅샷에도 넣지 않으므로 서버와 클라이언트의 기준이 같습니다. */
        static constexpr int32 kMaxEntityBytes = 255;
        /** @brief `writeDelta` 의 엔티티별 결과입니다. `outListCurrent` 에 들어가는 값입니다. */
        static constexpr uint8 kEntityNotSent        = 0; ///< 예산에 들어가지 못했다. 받는 쪽은 기준 값을 그대로 쓴다
        static constexpr uint8 kEntityAlreadyCurrent = 1; ///< 받는 쪽 기준이 이미 지금 상태이다(확인된 것)
        static constexpr uint8 kEntityWritten        = 2; ///< 이번 메시지에 실었다. 받았는지는 확인을 봐야 안다

        vector<NetEntityState> _listEntity{};
        uint32                 _tick{ 0 };
        NetOwnerState          _ownerState{};               ///< 받는 클라이언트가 조종하는 엔티티의 상태와 그 입력 틱. 클라이언트 예측을 맞추는 데 쓴다
        uint32                 _firstMissingInputTick{ 0 }; ///< 서버가 받는 클라이언트의 입력을 빈틈없이 받았거나 이미 꺼낸 마지막 틱의 다음 틱이다. 클라이언트는 여기서부터 다시 보낸다

        const NetEntityState* findEntity( uint32 entityID ) const;
        void                  sortEntities();
        /**
         * @brief 서버가 @p pBaseline 대비 판이 바뀐 엔티티와 사라진 엔티티를 씁니다. @p pBaseline 이 없으면 모두 씁니다.
         * @param writer 메시지를 쓰는 라이터입니다. 이미 쓴 비트(종류 바이트)도 예산에 포함합니다.
         * @param maxBytes 메시지 전체의 상한입니다. 이미 쓴 것, 헤더, 사라진 목록, 끝 표시를 포함하며 `NetConnection::kMaxSingleMessageSize` 로 잘립니다.
         *        넘치는 엔티티와 사라진 엔티티는 싣지 않고 @p outWritten 에 실은 것만 반영합니다.
         *        받는 쪽이 복원한 스냅샷은 기준에 실은 것을 더한 것이므로, 못 실은 것은 다음 델타가 다시 고릅니다.
         *        `kMaxEntityBytes` 를 넘는 엔티티도 싣지 않습니다.
         * @param outWritten 받는 쪽이 복원할 스냅샷의 기록(엔티티 · 판)입니다. 항목을 덮어써 용량을 재사용하므로 서버는 보낸 링 버퍼의 슬롯을 넘깁니다.
         * @param outListCurrent `_listRelevant` 의 항목마다 `kEntityNotSent`, `kEntityAlreadyCurrent`, `kEntityWritten` 중 하나를 채웁니다.
         */
        static void writeDelta( BitWriter& writer, const NetDeltaSource& source, const NetSentSnapshot* pBaseline, int32 maxBytes, NetSentSnapshot& outWritten,
                                vector<uint8>& outListCurrent );
        /** @brief 델타를 읽어 @p pBaseline 위에 복원합니다. 메시지가 손상됐으면 false 를 반환합니다. */
        [[nodiscard]] static bool readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot );
    };
} // namespace sw
