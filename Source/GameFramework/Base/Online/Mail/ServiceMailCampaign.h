/**
 * @file ServiceMailCampaign.h
 * @brief 전체 우편(캠페인) — 캠페인 레코드 하나(`mail_campaign`, 키 = id 16 진)와 계정마다 수령 표식(`mail_campaign_claim`, 키 `<id 16 진>/<계정 16 진>`)입니다.
 * @details 운영 "전 서버 우편" 은 계정마다 행을 만들지 않는다(수십만 쓰기) — 레코드 하나 + 받은 계정만 표식("없어야 한다"). 재원은 늘 발행이다.
 *          GM 키트(GF_Admin)가 만들고(`stageCreate`), 우편함 키트(GF_Server_Mailbox)가 목록에 끼우고 수령시킨다 — 쓰는 쪽 · 읽는 쪽이 다른 키트라 기반에 둔다.
 *          저장소 스레드의 동기 함수다. 국내 MMO 의 "전체 우편 = 캠페인 + 수령 기록" 과 같은 모양.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 캠페인 하나입니다. */
    struct ServiceMailCampaign
    {
        vector<ServiceMailAttachment> _listAttachment{};
        string                        _titleKey{};
        string                        _body{};
        string                        _senderName{};
        uint64                        _campaignID{ 0 };
        uint64                        _actorID{ 0 }; ///< 만든 GM
        int64                         _startMs{ 0 };
        int64                         _endMs{ 0 }; ///< 이 시각부터 받을 수 없다(목록에서도 빠진다)
        uint8                         _bLiteralText{ SW_FALSE };

        bool isActive( int64 nowMs ) const { return _startMs <= nowMs && nowMs < _endMs; }
    };
} // namespace sw

namespace sw
{
    /** @brief 캠페인 레코드 · 수령 표식입니다(저장소 스레드). */
    struct SW_GF_API ServiceMailCampaignTable
    {
        static const hashed_string& getCampaignTable(); ///< "mail_campaign"
        static const hashed_string& getClaimTable();    ///< "mail_campaign_claim"

        /** @brief 캠페인을 붙입니다("없어야 한다"). 규칙 위반(id 0 · 기간 · 첨부 수 · 금액 · 글 길이)은 false 이고 아무것도 붙이지 않는다. */
        [[nodiscard]] static bool stageCreate( const ServiceMailCampaign& campaign, ServiceTransaction& inoutTransaction );
        /** @brief 모든 캠페인(id 순)을 @p outListCampaign 뒤에 붙입니다 — 서비스가 주기마다 읽어 둔다. 읽지 못한 레코드는 오류 로그와 함께 건너뛴다. */
        [[nodiscard]] static ServiceStoreResult listCampaigns( IServiceStoreConnection& connection, vector<ServiceMailCampaign>& outListCampaign );
        static string                           makeClaimKey( uint64 campaignID, uint64 accountID );
        static vector<uint8>                    encode( const ServiceMailCampaign& campaign );
        [[nodiscard]] static bool               decode( const vector<uint8>& bytes, ServiceMailCampaign& outCampaign );
    };
} // namespace sw
