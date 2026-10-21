/**
 * @file ComponentHandleArgument.h
 * @brief 리플렉션 호출 썽크가 타입 없는 핸들 인자를 타입 핸들(`TComponentHandle<T>`)로 바꾸는 확인입니다. 타입 핸들 인자가 있는 `.gen.cpp` 만 이 헤더를 넣습니다.
 * @details 부르는 쪽(인스펙터 선택기 · 콘솔 · 비주얼 스크립팅)은 인자 타입을 모르고 `ComponentHandle` 을 넘긴다. 바꾸는 길은 대상의 타입을 확인하는
 *          `GameObjectManager::makeTypedHandle` 하나이고, 그 매니저는 **불린 컴포넌트의 씬**이다(파서가 타입 핸들 인자를 컴포넌트의 비정적 메서드에만 허락한다).
 *          확인이 지면 오류를 남기고 메서드를 부르지 않는다 — 빈 핸들로 바꿔 부르면 타입이 틀린 값이 "대상 없음" 으로 조용히 바뀐다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /**
     * @struct ComponentHandleArgument
     * @brief 호출 썽크의 타입 핸들 인자 확인입니다.
     */
    struct SW_API ComponentHandleArgument
    {
        /**
         * @brief @p handle 을 @p self 의 씬에서 확인해 @p outHandle 에 씁니다. 무효 핸들은 무효 타입 핸들("대상 없음")로 통과합니다.
         * @param functionName 오류 글에 넣을 `타입::메서드` 이름
         * @return 대상이 없거나 타입이 다르거나 @p self 가 씬에 없으면 오류를 남기고 false 입니다(@p outHandle 은 그대로).
         */
        template <typename T>
        [[nodiscard]] static bool cast( const Component& self, ComponentHandle handle, TComponentHandle<T>& outHandle, string_view functionName )
        {
            if ( handle.isValid() == false )
            {
                outHandle = TComponentHandle<T>{};
                return true;
            }
            const GameObjectManager* pManager = findManager( self, functionName );
            return pManager != nullptr && pManager->makeTypedHandle( handle, outHandle ) == TypedHandleResult::Success;
        }

        /** @brief @p self 의 씬 매니저입니다. 오너나 매니저가 없으면 오류를 남기고 nullptr 입니다. */
        static const GameObjectManager* findManager( const Component& self, string_view functionName );
    };
} // namespace sw
