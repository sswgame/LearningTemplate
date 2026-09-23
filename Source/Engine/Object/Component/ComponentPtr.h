#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Component;
    class GameObjectManager;

    /**
     * @struct ComponentPtr
     * @brief 핫리로드 환경에서도 깨지지 않는 안전한 Component 참조 (Soft Component Pointer)
     * @details GameObject의 고유 이름과 Component의 타입을 저장하여, 언제든 현재 유효한 인스턴스를 동적으로 룩업합니다.
     */
    REFLECT()
    struct SW_API ComponentPtr
    {
        REFLECT_BODY();

    public:
        ComponentPtr();
        ComponentPtr( Component* pTarget );
        /** @brief 필드 전부 값이라 복사는 기본이다. 이동은 원본을 비운다(옮긴 뒤 원본이 같은 대상을 가리키면 안 된다). */
        ComponentPtr( const ComponentPtr& other ) = default;
        ComponentPtr( ComponentPtr&& other ) noexcept;
        ~ComponentPtr() = default;

        ComponentPtr& operator=( Component* pTarget );
        ComponentPtr& operator=( const ComponentPtr& other ) = default;
        ComponentPtr& operator=( ComponentPtr&& other ) noexcept;

        /** @brief 캐싱된 빠른 메모리 주소를 반환합니다. */
        Component* get() const
        {
            resolveLazy();
            return _pCachedPtr;
        }

        bool isValid() const
        {
            resolveLazy();
            return _pCachedPtr != nullptr;
        }

        Component* operator->() const
        {
            resolveLazy();
            return _pCachedPtr;
        }
        explicit operator bool() const { return isValid(); }

        bool operator==( const ComponentPtr& other ) const { return _targetObjectName == other._targetObjectName && _targetComponentType == other._targetComponentType; }
        bool operator!=( const ComponentPtr& other ) const { return ( *this == other ) == false; }

        hashed_string getTargetObjectName() const { return _targetObjectName; }
        hashed_string getTargetComponentType() const { return _targetComponentType; }

    private:
        void resolveLazy() const;

    private:
        PROPERTY()
        hashed_string _targetObjectName;

        PROPERTY()
        hashed_string _targetComponentType;

        mutable Component*             _pCachedPtr;
        mutable uint64                 _cachedObjectId;
        mutable uint32                 _cachedComponentGeneration; ///< 캐시를 잡을 때의 소유자 목록 세대 — 같으면 포인터가 아직 목록에 있다
        mutable sw::GameObjectManager* _pManager;
    };

} // namespace sw
