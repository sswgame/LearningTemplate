/**
 * @file NetSnapshot.h
 * @brief 한 틱의 엔티티 상태(엔티티 id, 종류, 바이트)를 모은 스냅샷과 그 델타 직렬화입니다.
 * @details 서버와 클라이언트가 같은 코드로 쓰고 읽는다.
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

    /** @brief 메시지 종류입니다. 메시지의 첫 바이트에 들어갑니다. */
    struct NetClientServerMessage
    {
        static constexpr uint8 kSnapshot    = NetKitMessageRange::kClientServer + 0;
        static constexpr uint8 kSnapshotAck = NetKitMessageRange::kClientServer + 1;
        static constexpr uint8 kInput       = NetKitMessageRange::kClientServer + 2;
        static_assert( NetMessageRange::isInRange( kInput, NetKitMessageRange::kClientServer ), "message kinds must stay inside the kit's range" );

        static constexpr int32 kMaxInputBytes = 255; ///< 틱 하나의 입력 상한. 클라이언트는 넘는 입력을 보내지 않고, 서버는 넘는 길이를 손상된 메시지로 본다
        static constexpr int32 kMaxInputCount = 32;  ///< 입력 메시지 하나에 싣는 입력 수의 상한이자, 클라이언트가 확인을 기다리며 보관하는 입력 수의 상한이다. 두 쪽이 같은 값을 쓴다
        /** @brief 입력 묶음의 전송 형식입니다. 길이를 붙인 덩어리로 보내며 스탬프는 쓰지 않습니다. 스탬프를 켜면 와이어 버전을 올려야 합니다. */
        static constexpr NetInputFormat kInputFormat{ 0, kMaxInputBytes, kMaxInputCount, SW_FALSE };
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
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱의 엔티티 상태 모음입니다. 엔티티 id 오름차순으로 보관하며, 델타가 두 스냅샷을 나란히 순회합니다. */
    struct SW_GF_API NetSnapshot
    {
        /** @brief 엔티티 하나의 상태 바이트 상한입니다. 넘는 엔티티는 싣지 않으며, 복원한 스냅샷에도 넣지 않으므로 서버와 클라이언트의 기준이 같습니다. */
        static constexpr int32 kMaxEntityBytes = 255;
        /** @brief `writeDelta` 의 엔티티별 결과입니다. `pOutListCurrent` 에 들어가는 값입니다. */
        static constexpr uint8 kEntityNotSent        = 0; ///< 예산에 들어가지 못했다. 받는 쪽은 기준 값을 그대로 쓴다
        static constexpr uint8 kEntityAlreadyCurrent = 1; ///< 받는 쪽 기준이 이미 지금 상태이다(확인된 것)
        static constexpr uint8 kEntityWritten        = 2; ///< 이번 메시지에 실었다. 받았는지는 확인을 봐야 안다

        vector<NetEntityState> _listEntity{};
        uint32                 _tick{ 0 };
        uint32                 _lastProcessedInputTick{ 0 }; ///< 서버가 받는 클라이언트의 입력을 어느 틱까지 썼는지 나타낸다. 클라이언트 예측을 맞추는 데 쓴다
        uint32                 _firstMissingInputTick{ 0 };  ///< 서버가 받는 클라이언트의 입력을 빈틈없이 받았거나 이미 꺼낸 마지막 틱의 다음 틱이다. 클라이언트는 여기서부터 다시 보낸다

        const NetEntityState* findEntity( uint32 entityID ) const;
        void                  sortEntities();
        /**
         * @brief @p baseline 대비 바뀐 엔티티와 사라진 엔티티를 씁니다. @p pBaseline 이 없으면 모두 씁니다.
         * @param writer 메시지를 쓰는 라이터입니다. 이미 쓴 비트(종류 바이트)도 예산에 포함합니다.
         * @param maxBytes 메시지 전체의 상한입니다. 이미 쓴 것, 헤더, 사라진 목록, 끝 표시를 포함하며 `NetConnection::kMaxSingleMessageSize` 로 잘립니다.
         *        넘치는 엔티티와 사라진 엔티티는 싣지 않고 @p outWritten 에 실은 것만 반영합니다.
         *        받는 쪽이 복원한 스냅샷은 기준에 실은 것을 더한 것이므로, 못 실은 것은 다음 델타가 다시 고릅니다.
         *        `kMaxEntityBytes` 를 넘는 엔티티도 싣지 않습니다.
         * @param pListOrder 싣는 순서입니다. `_listEntity` 의 인덱스이며 우선도가 높은 것이 먼저입니다. 없으면 id 순입니다.
         * @param pOutListCurrent 지정하면 `_listEntity` 의 항목마다 `kEntityNotSent`, `kEntityAlreadyCurrent`, `kEntityWritten` 중 하나를 채웁니다.
         */
        void writeDelta( BitWriter& writer, const NetSnapshot* pBaseline, int32 maxBytes, NetSnapshot& outWritten, const vector<int32>* pListOrder = nullptr,
                         vector<uint8>* pOutListCurrent = nullptr ) const;
        /** @brief 델타를 읽어 @p pBaseline 위에 복원합니다. 메시지가 손상됐으면 false 를 반환합니다. */
        [[nodiscard]] static bool readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot );
    };
} // namespace sw
