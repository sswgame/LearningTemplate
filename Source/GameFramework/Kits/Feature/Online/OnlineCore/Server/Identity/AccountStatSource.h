/**
 * @file AccountStatSource.h
 * @brief 이 서버에 접속한 계정의 통계 값을 메모리에서 읽는 인터페이스입니다. 순위표 키트가 구현해 서버 조립 문맥(`OnlineServerKitContext::_kits`)에 내놓고,
 *        매칭 키트가 모드의 실력 통계(`MatchModeDef::_ratingStat`)를 읽습니다.
 * @details - 키트끼리 include 하지 않도록 통계는 이름으로만 가리킵니다.
 *          - 접속한 계정의 통계는 구현이 접속 때 저장소에서 읽어 두므로, 그 전에는 `NotLoaded` 입니다. 기본값으로 바꾸지 않고 부르는 쪽이 다시 하라고 알립니다.
 *          - 서비스 스레드 하나에서만 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"

namespace sw
{
    /** @brief 통계 찾기의 결과입니다. */
    enum class AccountStatLookup : uint8
    {
        Found = 0, ///< 값이 있다
        Missing,   ///< 읽어 두었지만 그 통계가 없다(한 번도 바뀌지 않았다)
        NotLoaded  ///< 이 서버가 아직 그 계정의 통계를 읽지 않았다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAccountStatSource
     * @brief 계정 통계를 메모리에서 읽는 인터페이스입니다.
     */
    class IAccountStatSource
    {
    public:
        IAccountStatSource()          = default;
        virtual ~IAccountStatSource() = default;

        IAccountStatSource( const IAccountStatSource& )            = delete;
        IAccountStatSource& operator=( const IAccountStatSource& ) = delete;

        /** @brief @p accountID 의 통계 @p statName 을 찾습니다. Found 일 때만 @p outValue 를 채웁니다. */
        virtual AccountStatLookup findStat( AccountID accountID, string_view statName, int64& outValue ) const = 0;
        /**
         * @brief 계정의 통계를 메모리에 올려 둡니다. 접속한 계정과, 다른 서버에 접속한 파티원을 줄 세울 때 부릅니다. 부른 만큼 `releaseStats` 를 부릅니다.
         * @details 저장소에서 읽으므로 곧바로 올라오지 않습니다. `findStat` 이 NotLoaded 가 아니면 다 읽은 것입니다. 앞선 읽기가 실패했으면 다시 읽습니다.
         */
        virtual void retainStats( AccountID accountID ) = 0;
        /** @brief 아무도 잡고 있지 않으면 메모리에서 지웁니다. */
        virtual void releaseStats( AccountID accountID ) = 0;
    };
} // namespace sw
