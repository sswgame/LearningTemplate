/**
 * @file AccountBlockList.h
 * @brief 계정끼리의 차단 목록을 묻는 창구입니다. 친구 키트(GF_Server_Social)가 구현하고, 채팅처럼 차단을 지켜야 하는 키트가 서버 조립에서 받습니다(키트끼리는 include 하지 못한다).
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class IAccountBlockList
     * @brief 서비스 스레드에서 부릅니다. 이 서버에 접속한 계정의 목록만 메모리에서 답합니다(받는 쪽 서버가 판단한다).
     */
    class SW_GF_API IAccountBlockList
    {
    public:
        IAccountBlockList()          = default;
        virtual ~IAccountBlockList() = default;

        IAccountBlockList( const IAccountBlockList& )            = delete;
        IAccountBlockList& operator=( const IAccountBlockList& ) = delete;

        /** @brief @p ownerID 가 @p otherID 를 막았으면 true 입니다. @p ownerID 가 이 서버에 없으면 false 입니다. */
        virtual bool isBlocked( AccountID ownerID, AccountID otherID ) const = 0;
    };
} // namespace sw
