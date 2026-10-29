/**
 * @file ServiceFactoryList.h
 * @brief 정적 등록기가 올리는 서비스 팩토리 목록입니다. 서버 서비스 레지스트리, 온라인 서버 키트 레지스트리, 온라인 프로토콜 레지스트리(`OnlineProtocolRegistry`)가 같이 씁니다.
 * @details 저장은 공용 `RegistrationList`(순서 값 · 이름 순, 이름 필수)에 맡기고 이 클래스는 잠금만 더합니다 — 중복 거절 · 핫 리로드 대체(`RegistrationReloadScope`) ·
 *          그림자 복원은 목록이 하는 그대로입니다. 여러 스레드(모듈 로드 · 언로드, 기동 읽기)가 만지므로 잠금이 필요한 정적 등록부의 모양이 이것입니다.
 *          시작은 모은 순서, 멈춤은 반대 순서로 합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    /** @brief 목록의 항목 하나입니다. 팩토리 타입이 이것을 물려받습니다. */
    struct ServiceFactoryNode
    {
        const utf8* _pName{ nullptr };
        int32       _order{ 0 }; ///< 작은 것부터 시작한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceFactoryList
     * @brief 서비스 팩토리 항목의 목록입니다. 잠금 하나로 지킵니다(등록과 해제는 모듈 로드와 언로드, 읽기는 기동 때).
     */
    class SW_API ServiceFactoryList
    {
    public:
        /** @param pListName 전역 열거와 거절 로그에 나오는 목록 이름입니다. 문자열 리터럴이어야 합니다. */
        explicit ServiceFactoryList( const utf8* pListName );

        ServiceFactoryList( const ServiceFactoryList& )            = delete;
        ServiceFactoryList& operator=( const ServiceFactoryList& ) = delete;

        /** @brief 항목을 올립니다. 이름이 없거나 같은 이름이 있으면 공용 거절 로그를 남기고 false 입니다. 같은 항목을 다시 올리면 true 입니다. 리로드 표지가 켜져 있으면 같은 이름을 대체하고 true 입니다. */
        bool add( ServiceFactoryNode* pNode );
        /** @brief 항목을 내립니다. 없으면 아무것도 하지 않습니다. 올라 있던 항목을 내리면 그것이 대체했던 같은 이름의 옛 항목이 되돌아옵니다. */
        void remove( ServiceFactoryNode* pNode );
        /** @brief 올라온 항목을 순서 값(같으면 이름 순)으로 @p outListNode 에 담습니다. 비운 뒤 채웁니다. */
        void  collect( vector<ServiceFactoryNode*>& outListNode ) const;
        int32 getCount() const;

        /** @brief `collect` 와 같고 항목을 팩토리 타입으로 돌려줍니다. 한 목록에는 한 팩토리 타입만 올리므로 정적 변환이 안전합니다. */
        template <typename TFactory>
        void collectAs( vector<TFactory*>& outListFactory ) const
        {
            vector<ServiceFactoryNode*> listNode;
            collect( listNode );
            outListFactory.clear();
            outListFactory.reserve( listNode.size() );
            for ( ServiceFactoryNode* pNode : listNode )
            {
                outListFactory.push_back( static_cast<TFactory*>( pNode ) );
            }
        }

    private:
        const utf8*                                _pListName;
        mutable mutex                              _mutex;
        RegistrationList<ServiceFactoryNode> _list SW_GUARDED_BY( _mutex );
    };
} // namespace sw
