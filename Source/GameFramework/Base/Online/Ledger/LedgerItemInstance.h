/**
 * @file LedgerItemInstance.h
 * @brief 인스턴스 아이템(내구도, 강화처럼 한 개마다 상태가 있는 아이템)입니다. 원장은 개수만 가지므로 아이템 하나를 개수 1 짜리 자산 `item.<정의>-<id 16진>` 으로 두고,
 *        상태는 표 `item_state`(키 = 그 자산 id)의 레코드로 둡니다.
 * @details - 상태 레코드의 키가 주인이 아니라 자산 id 이므로, 원장 이동(거래, 우편, 맡김)이 개수 1 을 옮기면 상태는 따로 옮기지 않아도 새 주인의 것이 됩니다.
 *          - 원장이 인스턴스 자산을 따로 지킵니다. 다리 하나의 양은 1 이어야 하고, 어느 보유자도 2 개 이상 가질 수 없습니다(`Ledger::stageTransfer`).
 *          - 만들기는 발행 1 개와 상태 레코드("없어야 함")를 한 트랜잭션에, 없애기는 소각 1 개와 상태 지우기를 한 트랜잭션에 넣습니다.
 *          - 상태 값은 판 머리가 붙은 자기 기술형(A, `ServiceRecordUtil`)입니다. 상태를 바꾸는 쪽(게임 서버의 강화, 수리)은 읽은 판을 조건으로 씁니다.
 *          PlayFab Economy v2 의 stack id 와 인스턴스 속성, 국내 MMO 의 아이템 고유 번호와 같은 구조입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUnknownFields.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 인스턴스 아이템 하나의 상태입니다. 저장소 레코드(A)이며 GM 조회와 인벤토리 응답에 실을 수 있습니다. */
    REFLECT()
    struct SW_GF_API LedgerItemState
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 48 )
        string _assetID{}; ///< `item.<정의>-<id 16진>` 이다. 레코드 키와 같다
        PROPERTY( Min = 0 )
        int32 _durability{ 0 };
        PROPERTY( Min = 0 )
        int32 _maxDurability{ 0 }; ///< 0 이면 내구도가 없는 아이템이다
        PROPERTY( Min = 0, Max = 100 )
        int32 _enhanceLevel{ 0 };
        PROPERTY()
        int64 _createdMs{ 0 };
        /// 이 빌드가 모르는 칸(배포 중 새 빌드가 더한 칸)이다. PROPERTY 가 아니다 — 영속 읽기가 담고 쓰기가 그대로 싣는다(`ServiceRecordUtil`).
        ServiceRecordUnknownFields _unknownFields{};
    };
} // namespace sw

namespace sw
{
    /** @brief 인스턴스 아이템의 자산 id 규칙과 상태 레코드 함수입니다. 저장소 함수는 저장소 스레드에서 부릅니다. */
    struct SW_GF_API LedgerItemInstance
    {
        static constexpr uint32 kRecordVersion   = 1;
        static constexpr int32  kMaxDefIDSize    = 24; ///< `item.` + 정의 + `-` + 16진 16자가 자산 id 상한(48) 안에 든다
        static constexpr int32  kMaxEnhanceLevel = 100;

        static const hashed_string& getStateTable(); ///< "item_state"

        /** @brief 정의 @p defID(`[0-9a-z_]`, 24자 이하)와 고유 번호 @p instanceID 로 자산 id 를 만듭니다. 규칙 밖이면 false 입니다. */
        [[nodiscard]] static bool makeAssetID( string_view defID, uint64 instanceID, string& outAssetID );
        /** @brief 인스턴스 아이템의 자산 id 이면 true 입니다. 원장이 개수 1 규칙을 지킬 때 씁니다. */
        static bool isInstanceAssetID( string_view assetID );
        /** @brief 상태가 규칙 안이면 true 입니다(자산 id, 내구도 0..최대, 강화 0..100). */
        static bool isValidState( const LedgerItemState& state );

        /**
         * @brief 아이템 하나를 만들어 @p ownerHolder 에게 줍니다. 발행 1 개(분개 @p journalKey, 사유 @p reason)와 상태 레코드를 @p inoutTransaction 에 넣습니다.
         * @return 상태가 규칙 밖이거나 같은 id 의 아이템이 이미 있으면 Invalid 입니다.
         */
        [[nodiscard]] static LedgerResult stageCreate( IServiceStoreConnection& connection, const LedgerHolder& ownerHolder, const LedgerItemState& state, string_view journalKey,
                                                       string_view reason, int64 nowMs, ServiceTransaction& inoutTransaction );
        /** @brief 아이템 하나를 없앱니다. @p ownerHolder 의 1 개를 소각하고 상태 레코드를 지웁니다. */
        [[nodiscard]] static LedgerResult stageDestroy( IServiceStoreConnection& connection, const LedgerHolder& ownerHolder, string_view assetID, string_view journalKey,
                                                        string_view reason, int64 nowMs, ServiceTransaction& inoutTransaction );
        /** @brief 상태를 읽습니다. 없으면 NotFound, 손상이면 오류 로그와 Invalid 입니다. @p outVersion 은 바꿀 때의 조건입니다. */
        [[nodiscard]] static ServiceStoreResult readState( IServiceStoreConnection& connection, string_view assetID, LedgerItemState& outState, uint64& outVersion );
        /** @brief 바꾼 상태를 읽은 판 @p version 을 조건으로 넣습니다. 규칙 밖이거나 쓸 수 없으면 false 이고 트랜잭션은 그대로입니다. */
        [[nodiscard]] static bool stageWriteState( ServiceTransaction& inoutTransaction, const LedgerItemState& state, uint64 version );
    };
} // namespace sw
