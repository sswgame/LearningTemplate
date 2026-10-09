/**
 * @file NetSnapshot.h
 * @brief 스냅샷 — 한 틱의 엔티티 상태(엔티티 id · 종류 · 바이트) 묶음과 그 델타 직렬화입니다. 서버 · 클라이언트가 같은 코드로 쓰고 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Replication/NetInputWindow.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetClientServerMessage
    {
        static constexpr uint8 kSnapshot    = NetKitMessageRange::kClientServer + 0;
        static constexpr uint8 kSnapshotAck = NetKitMessageRange::kClientServer + 1;
        static constexpr uint8 kInput       = NetKitMessageRange::kClientServer + 2;
        static_assert( NetMessageRange::isInRange( kInput, NetKitMessageRange::kClientServer ), "message kinds must stay inside the kit's range" );

        static constexpr int32 kMaxInputBytes = 255; ///< 틱 하나의 입력 상한 — 클라이언트는 넘는 입력을 보내지 않고 서버는 넘는 길이를 깨짐으로 본다
        static constexpr int32 kMaxInputCount = 32;  ///< 입력 메시지 하나에 싣는 입력 수 상한이자 클라이언트가 확인을 기다리며 드는 입력 수 — 두 쪽이 같은 값을 쓴다
        /** @brief 입력 묶음의 선 형식(길이 붙인 덩어리 · 스탬프 없음 — 1-6 B5 가 스탬프를 켜면 와이어 판을 올린다)입니다. */
        static constexpr NetInputFormat kInputFormat{ 0, kMaxInputBytes, kMaxInputCount, SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 엔티티 하나의 상태입니다. 바이트의 뜻은 게임이 정합니다(위치 · 체력 — `BitWriter` 로 양자화해 넣는다). */
    struct NetEntityState
    {
        vector<uint8> _buffer{};
        uint32        _entityId{ 0 };
        uint32        _typeId{ 0 }; ///< 클라이언트가 무엇을 만들지(프리팹 번호)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱의 상태들입니다. 엔티티 id 오름차순으로 둡니다(델타가 둘을 나란히 걷는다). */
    struct SW_GF_API NetSnapshot
    {
        /** @brief 엔티티 하나의 상태 바이트 상한입니다. 넘는 엔티티는 싣지 않는다(재구성에도 넣지 않아 두 쪽 기준이 같다). */
        static constexpr int32 kMaxEntityBytes = 255;
        /** @brief `writeDelta` 의 엔티티별 결과입니다(`pOutListCurrent` 의 값). */
        static constexpr uint8 kEntityNotSent        = 0; ///< 예산에 못 들었다 — 받는 쪽은 기준 값 그대로
        static constexpr uint8 kEntityAlreadyCurrent = 1; ///< 받는 쪽 기준이 이미 지금 상태다(확인된 것)
        static constexpr uint8 kEntityWritten        = 2; ///< 이번 메시지에 실었다 — 받았는지는 확인을 봐야 안다

        vector<NetEntityState> _listEntity{};
        uint32                 _tick{ 0 };
        uint32                 _lastProcessedInputTick{ 0 }; ///< 받는 클라이언트의 입력을 서버가 어디까지 썼나(예측 맞추기)
        uint32                 _firstMissingInputTick{ 0 };  ///< 받는 클라이언트의 입력을 서버가 빈틈없이 받은(또는 이미 꺼낸) 다음 틱 — 클라이언트는 여기서부터 다시 보낸다

        const NetEntityState* findEntity( uint32 entityId ) const;
        void                  sortEntities();
        /**
         * @brief @p baseline 대비 바뀐 엔티티 · 사라진 엔티티를 씁니다. @p pBaseline 이 없으면 모두 씁니다.
         * @param writer 메시지 쓰기 — 이미 쓴 비트(종류 바이트)도 예산에 든다.
         * @param maxBytes 메시지 전체(이미 쓴 것 · 머리 · 사라진 목록 · 끝 표시 포함) 상한. `NetConnection::kMaxSingleMessageSize` 로 잘린다. 넘치는 엔티티 ·
         *        사라진 엔티티는 싣지 않고 @p outWritten 에 실은 것만 반영한다(받는 쪽 재구성 = 기준 + 실은 것 — 못 실은 것은 다음 델타가 다시 고른다).
         *        `kMaxEntityBytes` 를 넘는 엔티티도 싣지 않는다.
         * @param pListOrder 싣는 순서(`_listEntity` 의 자리 — 우선도 높은 것 먼저). 없으면 id 순.
         * @param pOutListCurrent 있으면 `_listEntity` 자리마다 `kEntityNotSent` · `kEntityAlreadyCurrent` · `kEntityWritten`.
         */
        void writeDelta( BitWriter& writer, const NetSnapshot* pBaseline, int32 maxBytes, NetSnapshot& outWritten, const vector<int32>* pListOrder = nullptr,
                         vector<uint8>* pOutListCurrent = nullptr ) const;
        /** @brief 델타를 읽어 @p pBaseline 위에 재구성합니다. 깨졌으면 false 입니다. */
        [[nodiscard]] static bool readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot );
    };
} // namespace sw
