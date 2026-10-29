/**
 * @file UIScreenTypes.h
 * @brief 화면을 가리키는 번호와 명령 함수 형식입니다. 화면을 번호로만 들고 있는 쪽(서비스 · 컨트롤러 · 패널)이 `UIScreen.h`(위젯 트리 · 애니메이션 · 바인딩)를 끌지 않게 뗐다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Widget;

    /** @brief 화면을 가리키는 번호입니다. 다시 쓰지 않습니다(0 이면 없음). */
    using UIScreenHandle = uint32;
    /** @brief 화면이 없음을 뜻하는 번호입니다. */
    inline constexpr UIScreenHandle kInvalidUIScreenHandle = 0;

    /** @brief 위젯이 낸 명령(버튼의 `_command`)을 받는 함수입니다(`UIScreen::registerCommand`). */
    using UICommandDelegate = Delegate<void( const hashed_string&, Widget& )>;
} // namespace sw
