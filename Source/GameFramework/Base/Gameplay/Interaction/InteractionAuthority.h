/**
 * @file InteractionAuthority.h
 * @brief 상호작용의 네트워크 권한 훅과 완료 이벤트입니다.
 * @details 게임이 `IInteractionAuthority` 를 게임 서비스로 등록하면(권위 서버나 롤백 세션) 권한이 `Server` 인 상호작용은 시작 전에 허락을 묻고, 완료는 훅에
 *          알린다. 서버는 요청을 검증해서 결과(문 열림, 아이템)를 복제하고, 클라이언트는 예측으로 진행 표시만 한다. 등록된 훅이 없으면 모두 로컬에서 처리한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 상호작용 요청 하나입니다. 누가, 무엇에, 어떤 종류를 요청하는지 담습니다. */
    struct InteractionRequest
    {
        GameObjectHandle _interactor{};
        GameObjectHandle _interactable{};
        hashed_string    _interaction{};
    };
} // namespace sw

namespace sw
{
    /** @brief 권한 훅 인터페이스입니다. 병렬 틱에서 불릴 수 있으므로 구현은 스레드에 안전해야 합니다. */
    class IInteractionAuthority
    {
    public:
        IInteractionAuthority()          = default;
        virtual ~IInteractionAuthority() = default;

        IInteractionAuthority( const IInteractionAuthority& )            = default;
        IInteractionAuthority& operator=( const IInteractionAuthority& ) = default;

        /** @brief 시작해도 되면 true 를 반환합니다. 서버 검증과, 이미 다른 사람이 쓰는 중인지 확인합니다. */
        virtual bool canBeginInteraction( const InteractionRequest& request ) = 0;
        /** @brief 상호작용이 끝났음을 알립니다. 서버가 결과를 적용하고 복제합니다. */
        virtual void notifyInteractionCompleted( const InteractionRequest& request ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 상호작용이 끝났음을 알립니다. "game" 채널로 보냅니다. */
    struct SW_GF_API InteractionCompletedEvent final : IEvent
    {
        InteractionRequest _request{};
        SW_DECLARE_GAMEPLAY_EVENT( InteractionCompletedEvent );
    };
} // namespace sw
