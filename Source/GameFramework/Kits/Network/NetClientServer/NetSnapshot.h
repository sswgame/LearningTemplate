/**
 * @file NetSnapshot.h
 * @brief 스냅샷 — 한 틱의 엔티티 상태(엔티티 id · 종류 · 바이트) 묶음과 그 델타 직렬화입니다. 서버 · 클라이언트가 같은 코드로 쓰고 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetClientServerMessage
    {
        static constexpr uint8 kSnapshot    = 0x10;
        static constexpr uint8 kSnapshotAck = 0x11;
        static constexpr uint8 kInput       = 0x12;
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
        vector<NetEntityState> _listEntity{};
        uint32                 _tick{ 0 };
        uint32                 _lastProcessedInputTick{ 0 }; ///< 받는 클라이언트의 입력을 서버가 어디까지 썼나(예측 맞추기)

        const NetEntityState* findEntity( uint32 entityId ) const;
        void                  sortEntities();
        /**
         * @brief @p baseline 대비 바뀐 엔티티 · 사라진 엔티티를 씁니다. @p pBaseline 이 없으면 모두 씁니다.
         * @param maxBytes 넘치는 엔티티는 싣지 않고 @p outWritten 에 실은 것만 남긴다(받는 쪽 재구성 = 기준 + 실은 것).
         * @param pListOrder 싣는 순서(`_listEntity` 의 자리 — 우선도 높은 것 먼저). 없으면 id 순.
         */
        void writeDelta( BitWriter& writer, const NetSnapshot* pBaseline, int32 maxBytes, NetSnapshot& outWritten,
                         const vector<int32>* pListOrder = nullptr ) const;
        /** @brief 델타를 읽어 @p pBaseline 위에 재구성합니다. 깨졌으면 false 입니다. */
        [[nodiscard]] static bool readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot );
    };
} // namespace sw
