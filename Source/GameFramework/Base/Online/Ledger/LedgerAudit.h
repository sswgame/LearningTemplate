/**
 * @file LedgerAudit.h
 * @brief 원장 보존 검사 — 분개를 모두 훑어 자산마다 발행 · 소각을 합하고, 잔액을 모두 훑어 합 · 음수를 셉니다. 불변식: 보유 = 발행 − 소각.
 * @details 표 전체를 읽으므로 운영에서는 예약 작업(한가한 시간 · 읽기 복제본)으로 돌린다. 시험은 메모리 저장소로 이동마다 돌린다.
 *          계정의 음수 잔액은 환불 회수의 빚이라 위반이 아니다(따로 센다). 맡김의 음수는 위반이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 자산 하나의 검사 결과입니다. */
    struct LedgerAssetAudit
    {
        string _assetID{};
        int64  _issued{ 0 }; ///< 발행에서 나간 양의 합
        int64  _burned{ 0 }; ///< 소각으로 들어간 양의 합
        int64  _held{ 0 };   ///< 계정 · 맡김 잔액의 합(빚은 음수로 든다)

        bool isBalanced() const { return _held == _issued - _burned; }
    };
} // namespace sw

namespace sw
{
    /** @brief 검사 결과 전체입니다. */
    struct SW_GF_API LedgerAuditReport
    {
        vector<LedgerAssetAudit> _listAsset{}; ///< 자산 id 순
        int32                    _negativeEscrowCount{ 0 };
        int32                    _debtBalanceCount{ 0 }; ///< 빚이 있는 계정 잔액 수(위반 아님)
        int32                    _unreadableRecordCount{ 0 };
        int32                    _journalCount{ 0 };

        bool                    isBalanced() const;
        const LedgerAssetAudit* findAsset( string_view assetID ) const;
    };
} // namespace sw

namespace sw
{
    struct SW_GF_API LedgerAudit
    {
        /** @brief 저장소 스레드에서 표 둘을 훑어 @p outReport 를 채웁니다. */
        [[nodiscard]] static ServiceStoreResult computeReport( IServiceStoreConnection& connection, LedgerAuditReport& outReport );
    };
} // namespace sw
