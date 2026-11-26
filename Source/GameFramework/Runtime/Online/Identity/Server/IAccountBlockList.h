/**
 * @file IAccountBlockList.h
 * @brief 서버의 계정 차단 목록 포트입니다(`IAccountDirectory` 와 같은 계정 포트 묶음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

namespace sw
{
    class IServiceStoreConnection;
    enum class ServiceStoreResult : uint8;

    // 계정끼리의 차단 목록을 묻는 포트 — 친구 키트(GF_Server_Social)가 구현하고, 채팅처럼 차단을 지켜야 하는 키트가 서버 조립에서 받는다(키트끼리는 include 하지 못한다).
    /**
     * @class IAccountBlockList
     * @brief 서비스 스레드에서 부릅니다. 이 서버에 접속한 계정의 목록만 메모리에서 답합니다(받는 쪽 서버가 판단한다).
     */
    class IAccountBlockList
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kSocial; ///< 이 인터페이스를 내는 키트다. 받는 키트의 의존 이름이 여기서 나온다(`OnlineServerKit`)

        IAccountBlockList()          = default;
        virtual ~IAccountBlockList() = default;

        IAccountBlockList( const IAccountBlockList& )            = delete;
        IAccountBlockList& operator=( const IAccountBlockList& ) = delete;

        /** @brief @p ownerID 가 @p otherID 를 막았으면 true 입니다. @p ownerID 가 이 서버에 없으면 false 입니다. */
        virtual bool isBlocked( AccountID ownerID, AccountID otherID ) const = 0;
    };
} // namespace sw
