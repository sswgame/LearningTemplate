/**
 * @file ServiceFactoryList.h
 * @brief 정적 등록기가 올리는 서비스 팩토리의 연결 목록입니다. 서버 서비스 레지스트리, 온라인 서버 키트 레지스트리, 온라인 프로토콜 레지스트리(`OnlineProtocolRegistry`)가 같이 씁니다.
 * @details - 팩토리는 등록기가 가진 정적 객체이고 이 목록은 그 노드를 잇기만 합니다(할당 없음). 그래서 정적 초기화와 모듈 언로드 중에도 안전합니다.
 *          - 같은 이름은 하나만 받습니다. 두 번째는 오류 로그를 남기고 거절합니다(두 모듈이 같은 서비스를 올린 실수를 드러낸다).
 *          - 핫 리로드가 교체 이미지를 올리는 동안(`RegistrationReloadScope`)에는 거절하지 않고 같은 이름을 **대체**합니다. 새 이미지의 정적 등록자는 옛 이미지가 도는 동안 돌기 때문입니다.
 *            대체된 옛 노드는 그림자 사슬(`_pNext` 재사용)에 두었다가 옛 이미지가 내려가며 빼면 버리고, 새 이미지가 취소되면 복원합니다.
 *          - 모으기는 순서 값, 같으면 이름 순입니다. 시작은 그 순서, 멈춤은 반대 순서로 합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 목록의 노드 하나입니다. 팩토리 타입이 이것을 물려받습니다. */
    struct ServiceFactoryNode
    {
        const utf8*         _pName{ nullptr };
        ServiceFactoryNode* _pNext{ nullptr };
        int32               _order{ 0 }; ///< 작은 것부터 시작한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceFactoryList
     * @brief 서비스 팩토리 노드의 연결 목록입니다. 잠금 하나로 지킵니다(등록과 해제는 모듈 로드와 언로드, 읽기는 기동 때).
     */
    class SW_API ServiceFactoryList
    {
    public:
        ServiceFactoryList();

        ServiceFactoryList( const ServiceFactoryList& )            = delete;
        ServiceFactoryList& operator=( const ServiceFactoryList& ) = delete;

        /** @brief 노드를 올립니다. 이름이 없거나 같은 이름이 있으면 false 입니다(같은 이름이면 오류 로그를 남긴다). 같은 노드를 다시 올리면 true 입니다. 리로드 표지가 켜져 있으면 같은 이름을 대체하고 true 입니다. */
        bool add( ServiceFactoryNode* pNode, const utf8* pListName );
        /** @brief 노드를 내립니다. 없으면 아무것도 하지 않습니다. 올라 있던 노드를 내리면 그것이 대체했던 같은 이름의 옛 노드가 되돌아옵니다. */
        void remove( ServiceFactoryNode* pNode );
        /** @brief 올라온 노드를 순서 값(같으면 이름 순)으로 @p outListNode 에 담습니다. 비운 뒤 채웁니다. */
        void  collect( vector<ServiceFactoryNode*>& outListNode ) const;
        int32 getCount() const;

        /** @brief `collect` 와 같고 노드를 팩토리 타입으로 돌려줍니다. 한 목록에는 한 팩토리 타입만 올리므로 정적 변환이 안전합니다. */
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
        /** @brief @p pName 과 같은 이름의 그림자 노드가 있으면 가장 나중 것을 목록으로 복원합니다. 잠금을 쥔 채 부릅니다. */
        void restoreShadowOf( const utf8* pName ) SW_REQUIRES_LOCK( _mutex );

        mutable mutex                    _mutex;
        ServiceFactoryNode* _pHead       SW_GUARDED_BY( _mutex );
        int32 _count                     SW_GUARDED_BY( _mutex );
        ServiceFactoryNode* _pShadowHead SW_GUARDED_BY( _mutex ); ///< 리로드로 대체된 옛 노드의 사슬(가장 나중에 대체된 것이 앞)
    };
} // namespace sw
