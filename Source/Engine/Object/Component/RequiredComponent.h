/**
 * @file RequiredComponent.h
 * @brief 같은 오브젝트의 필요 컴포넌트를 형식으로 적는 멤버(`Required<T>`)입니다 — 등록 때 한 번 찾아 핸들로 쥐고, 없으면 그 자리에서 오류입니다.
 * @details 예전에는 필요 컴포넌트를 틱 · 물리 단계마다 `getComponent<T>()` 로 다시 찾았다 — 컴포넌트 목록을 돌며 원소마다 상속 사슬 검사(`castTo`)를 했고,
 *          없으면 조용히 건너뛰어 빠진 의존이 "아무 일도 안 함" 으로 숨었다. 이제 주인이 멤버로 선언하고 `bindRequiredComponents` 에서 묶는다:
 *          `Component::dispatchRegister` 가 `onRegister` 바로 앞에 그것을 부르므로 등록(붙이기 · 씬 로드 배치 끝 · 모듈 리로드 뒤 다시 등록)마다 같은 함수로 다시 묶인다.
 *          없으면 오류 로그(주인 · 필요 형식 이름)를 남기고 묶지 않는다 — 읽기는 묶인 뒤에만 한다(검사 구성 단언).
 *          언리얼은 생성자 서브오브젝트라 모델이 다르고, 유니티 `[RequireComponent]`(붙일 때 강제)와 Godot `@onready`(준비 때 한 번 찾기)를 합친 자리다.
 *          값은 핸들이라 필요 컴포넌트가 먼저 사라져도 매달린 포인터가 없다(찾기가 nullptr).
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"

namespace sw
{
    /** @brief `Required<T>::bind` 가 못 찾았을 때의 오류 로그입니다(템플릿 밖 — 로그 매크로를 헤더마다 펼치지 않는다). */
    SW_API void reportMissingRequiredComponent( const Component& owner, const TypeInfo* pRequiredType );

    /**
     * @class Required
     * @brief 주인과 같은 오브젝트에 있어야 하는 컴포넌트 @p T 입니다(파일 머리말). 주인 컴포넌트의 멤버로만 둡니다.
     */
    template <typename T>
    class Required
    {
        static_assert( std::is_base_of_v<Component, T>, "Required<T>: T must derive from sw::Component" );

    public:
        Required()
            : _handle{}
            , _pManager{ nullptr }
            , _bBindAttempted{ SW_FALSE }
        {
        }

        /**
         * @brief @p owner 의 오브젝트에서 @p T 를 찾아 쥡니다. 주인의 `bindRequiredComponents` 에서 부릅니다.
         * @return 없으면 오류 로그를 남기고 false 입니다(묶이지 않은 채 남는다).
         */
        [[nodiscard]] bool bind( const Component& owner )
        {
            _handle                  = TComponentHandle<T>{};
            _pManager                = nullptr;
            _bBindAttempted          = SW_TRUE;
            const GameObject* pObj   = owner.getOwner();
            const T*          pFound = pObj != nullptr ? pObj->getComponent<T>() : nullptr;
            if ( pFound == nullptr || pObj->getManager() == nullptr )
            {
                reportMissingRequiredComponent( owner, findStaticType<T>() );
                return false;
            }
            _handle   = TComponentHandle<T>{ pFound };
            _pManager = pObj->getManager();
            return true;
        }

        /** @brief 묶였는지입니다. */
        bool isBound() const { return _pManager != nullptr; }

        /**
         * @brief 필요 컴포넌트를 읽습니다(목록을 돌지 않는 핸들 찾기). 못 찾아 묶지 못했거나(오류는 이미 남겼다) 그 사이 사라졌으면 nullptr 입니다.
         * @details 묶기를 한 번도 하지 않았으면(주인이 `bindRequiredComponents` 를 재정의하지 않았다) 검사 구성에서 멈춘다.
         */
        const T* find() const
        {
            SW_ASSERT( _bBindAttempted == SW_TRUE ); // 주인이 `bindRequiredComponents` 에서 이 멤버를 묶지 않았다
            return _pManager != nullptr ? _pManager->resolveComponent( _handle ) : nullptr;
        }

        /** @brief 쥔 핸들입니다(틱 뒤 작업 · 쓰기 길이 매니저로 찾는다). */
        TComponentHandle<T> getHandle() const { return _handle; }

    private:
        TComponentHandle<T>      _handle;
        const GameObjectManager* _pManager;
        uint8                    _bBindAttempted : 1; ///< `bind` 를 부른 적이 있다(못 찾았어도)
    };
} // namespace sw
