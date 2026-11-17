/**
 * @file WebCursor.js
 * @brief 웹 커서의 브라우저 쪽(Emscripten JS 라이브러리) — 캔버스 style.cursor 를 겁니다. C++ 쪽은 Engine/Window/Web/WebWindow.cpp(`setCursorShape` · `setCursorVisible`).
 * @details 이름은 CSS 표준 커서(`CursorShapeUtil::toCSSName`, 숨김은 "none")다. 포인터가 잠긴 동안은 브라우저가 커서를 숨긴다.
 */
addToLibrary({
    swWebCursorSet__deps: ['$UTF8ToString'],
    swWebCursorSet: (pCanvasSelector, pCSSName) => {
        if (typeof document === 'undefined') return;
        const canvas = document.querySelector(UTF8ToString(pCanvasSelector));
        if (canvas !== null) canvas.style.cursor = UTF8ToString(pCSSName);
    },
});
