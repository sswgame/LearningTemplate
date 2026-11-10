/**
 * @file ServiceFactoryList.h
 * @brief 정적 등록기가 올리는 서비스 팩토리 항목(`ServiceFactoryNode`)과 그 목록 정의 매크로입니다. 서버 서비스 · 온라인 서버 키트 · 온라인 클라이언트 ·
 *        온라인 프로토콜 · 푸시 중계 레지스트리가 같이 씁니다.
 * @details 목록은 공용 `RegistrationList`((순서 값, 이름) 순, 이름 필수)를 그대로 쓴다 — 중복 거절 · 핫 리로드 대체(`RegistrationReloadScope`) · 그림자 복원은
 *          목록이 하는 그대로다. 잠금은 들지 않는다: 등록 · 해제는 모듈 로드 · 언로드(정적 초기화, 게임 스레드)이고 읽기는 기동 때 같은 스레드다
 *          (다른 정적 등록 목록과 같은 규칙 — `RegistrationReloadScope`). 시작은 모은 순서, 멈춤은 반대 순서로 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    /** @brief 목록의 항목 하나입니다. 팩토리 타입이 이것을 물려받습니다. */
    struct SW_API ServiceFactoryNode
    {
        using List = RegistrationList<ServiceFactoryNode>;

        const utf8* _pName{ nullptr };
        int32       _order{ 0 }; ///< 작은 것부터 시작한다

        /**
         * @brief @p pNode 를 @p list 에 올립니다. 이름이 없거나 같은 이름이 있으면 공용 거절 로그를 남기고 false 입니다. 같은 항목을 다시 올리면 true 입니다.
         *        리로드 표지가 켜져 있으면 같은 이름을 대체하고 true 입니다.
         */
        static bool addTo( List& list, ServiceFactoryNode* pNode );

        /** @brief @p list 의 항목을 순서 값(같으면 이름 순)으로 팩토리 타입 @p TFactory 로 담습니다. 한 목록에는 한 팩토리 타입만 올리므로 정적 변환이 안전합니다. */
        template <typename TFactory>
        static void collectAs( const List& list, vector<TFactory*>& outListFactory )
        {
            outListFactory.clear();
            outListFactory.reserve( list.getCount() );
            for ( ServiceFactoryNode* pNode : list.getItems() )
            {
                outListFactory.push_back( static_cast<TFactory*>( pNode ) );
            }
        }
    };
} // namespace sw

/** @brief 서비스 팩토리 목록을 돌려주는 함수 @p getName 을 정의합니다(.cpp, `SW_DEFINE_REGISTRATION_LIST` 의 서비스 팩토리 모양 — (순서, 이름) 순, 이름 필수). */
#define SW_DEFINE_SERVICE_FACTORY_LIST( getName, pListName ) \
    SW_DEFINE_REGISTRATION_LIST( getName, ::sw::ServiceFactoryNode, pListName, ::sw::RegistrationOrder::ByOrderThenName, true )
