#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"

#include "Core/Container/EnumArray.h"

namespace sw
{
    const utf8* toString( AdminRole role )
    {
        static constexpr auto kArrName = makeEnumTable<AdminRole, const utf8*>( "none", "viewer", "support", "operator", "super" );
        return role < AdminRole::Count ? kArrName[role] : "unknown";
    }

    AdminRole AdminProtocol::getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind )
    {
        switch ( method )
        {
            case AdminMethod::LookupAccount::kID:
            case AdminMethod::ListAudit::kID:
            case AdminMethod::ListReports::kID:
            case AdminMethod::ListLiveEvents::kID:
            {
                return AdminRole::Viewer;
            }
            case AdminMethod::ResolveReport::kID:
            {
                return AdminRole::Support;
            }
            case AdminMethod::SetSanction::kID:
            {
                if ( sanctionKind == ServiceSanctionKind::ChatMute )
                    return AdminRole::Support;
                return sanctionKind == ServiceSanctionKind::Suspend ? AdminRole::Operator : AdminRole::Super;
            }
            case AdminMethod::AdjustAsset::kID:
            case AdminMethod::SendMail::kID:
            case AdminMethod::SetMaintenance::kID:
            case AdminMethod::ClearMaintenance::kID:
            case AdminMethod::PostAnnouncement::kID:
            case AdminMethod::RemoveAnnouncement::kID:
            case AdminMethod::PutLiveEvent::kID:
            case AdminMethod::RemoveLiveEvent::kID:
            {
                return AdminRole::Operator;
            }
            case AdminMethod::BulkMail::kID:
            case AdminMethod::CreateCampaign::kID:
            case AdminMethod::SetRole::kID:
            {
                return AdminRole::Super;
            }
            default:
            {
                return AdminRole::Count; // 모르는 메서드이므로 누구도 가질 수 없는 등급이다
            }
        }
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry AdminProtocol::kOnlineEntry{ "Admin", AdminProtocol::Table{} };
} // namespace sw
