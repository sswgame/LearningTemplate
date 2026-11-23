/**
 * @file ExecFunctionCommand.h
 * @brief `FUNCTION( Exec )` 를 단 컴포넌트 메서드를 콘솔에서 부르는 개발 명령 `exec <오브젝트> <메서드> [인자…]` 입니다(언리얼 `UFUNCTION(Exec)` 자리).
 * @details 부를 수 있는 것은 `Exec` 를 적은 메서드뿐이다(허용 목록 — 에디터 메타데이터라 배포에는 없다). 오브젝트 · 메서드는 부를 때마다 이름으로 찾아 핫 리로드 뒤에도 옛 코드를
 *          가리키지 않는다. 인자 글은 메서드 인자 형식으로 바꾼다(`ReflectionInvoke::callWithText`) — 명령마다 손으로 적던 오브젝트 찾기 · 인자 읽기를 대신한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    class GameObject;

    /** @struct ExecFunctionCommand @brief 파일 머리말 참고. */
    struct SW_DEVTOOLS_API ExecFunctionCommand
    {
        /**
         * @brief @p object 의 컴포넌트에서 `Exec` 메서드 @p functionName 을 찾아 인자 글로 부릅니다. 여러 컴포넌트에 있으면 목록의 첫 컴포넌트다.
         * @return 불렀으면 true 이고 @p outReply 는 반환값 글이다. 그런 메서드가 없거나 인자가 틀리면 false 이고 까닭이다.
         */
        [[nodiscard]] static bool run( GameObject& object, string_view functionName, const vector<string>& listArgument, string& outReply );
    };
} // namespace sw
