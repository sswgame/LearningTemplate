#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

namespace sw
{
    const utf8* toString( AdminRole role )
    {
        static constexpr const utf8* kArrName[] = { "none", "viewer", "support", "operator", "super" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( AdminRole::Count ), "AdminRole names must match the enum" );
        const size_t index = static_cast<size_t>( role );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "unknown";
    }

    AdminResult AdminProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<AdminResult> kArrErrorResult[] = {
            {             OnlineError::kOk,             AdminResult::Ok},
            {OnlineError::kUnauthenticated,    AdminResult::NotSignedIn},
            {      OnlineError::kForbidden,      AdminResult::Forbidden},
            { OnlineError::kInvalidRequest, AdminResult::InvalidRequest},
            {       OnlineError::kConflict,           AdminResult::Busy},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, AdminResult::Unavailable );
    }

    AdminRole AdminProtocol::getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind )
    {
        switch ( method )
        {
            case AdminMethod::kLookupAccount:
            case AdminMethod::kListAudit:
            case AdminMethod::kListReports:
            {
                return AdminRole::Viewer;
            }
            case AdminMethod::kResolveReport:
            {
                return AdminRole::Support;
            }
            case AdminMethod::kSetSanction:
            {
                if ( sanctionKind == ServiceSanctionKind::ChatMute )
                    return AdminRole::Support;
                return sanctionKind == ServiceSanctionKind::Suspend ? AdminRole::Operator : AdminRole::Super;
            }
            case AdminMethod::kAdjustAsset:
            case AdminMethod::kSendMail:
            {
                return AdminRole::Operator;
            }
            case AdminMethod::kBulkMail:
            case AdminMethod::kCreateCampaign:
            case AdminMethod::kSetRole:
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
    const OnlineProtocolEntry AdminProtocol::kOnlineEntry{ "Admin", AdminMethod{}, AdminProtocol::MessageTypes{} };
} // namespace sw
