/**
 * @file WebTextInput.js
 * @brief 웹 글 입력의 브라우저 쪽(Emscripten JS 라이브러리) — 숨은 글 칸 하나가 입력기 조합 · 확정 글과 화면 키보드를 받는다. C++ 쪽은 Engine/Window/Web/WebWindow.cpp.
 * @details 캔버스는 글 칸이 아니라 브라우저가 입력기(IME) · 화면 키보드를 붙이지 않는다. 그래서 글을 원하는 동안 숨은 `<textarea>` 에 포커스를 주고
 *          `compositionupdate`(조합 중 글) · `compositionend`(확정) · `input`(조합 없는 확정, inputType insertText)을 받아 C++ 콜백으로 넘긴다.
 *          유니티 WebGL 의 화면 키보드 · Godot 웹의 `virtual_keyboard_show` 와 같은 숨은 입력 칸 방식이다(기억).
 *          모바일 브라우저는 사용자 입력 처리 안의 `focus()` 에서만 화면 키보드를 올린다 — 원하는데 포커스가 없으면 다음 터치 · 클릭(캡처 단계)에서 다시 잡는다.
 *          https://developer.mozilla.org/docs/Web/API/CompositionEvent · https://developer.mozilla.org/docs/Web/API/InputEvent/inputType
 */
addToLibrary({
    $swWebTextInput: { element: null, bWanted: false },

    // pCallback: void ( void* pUserData, int kind, const char* pText ) — kind 0 조합 중 글, 1 확정 글(WindowTextInputKind 순서).
    swWebTextInputInstall__deps: ['$swWebTextInput', '$stringToNewUTF8', 'free'],
    swWebTextInputInstall: (pCallback, pUserData) => {
        if (typeof document === 'undefined') return 0;
        if (swWebTextInput.element !== null) return 1;
        const element = document.createElement('textarea');
        element.setAttribute('autocomplete', 'off');
        element.setAttribute('autocorrect', 'off');
        element.setAttribute('autocapitalize', 'off');
        element.setAttribute('spellcheck', 'false');
        element.setAttribute('aria-hidden', 'true');
        // 보이지 않되 글 커서 자리에 둔다(swWebTextInputSetArea) — 입력기 후보 창이 그 옆에 뜬다. display:none 이면 포커스를 받지 못한다.
        Object.assign(element.style, {
            position: 'fixed', left: '0px', top: '0px', width: '1px', height: '1px', opacity: '0',
            border: '0', padding: '0', margin: '0', resize: 'none', overflow: 'hidden', pointerEvents: 'none',
        });
        document.body.appendChild(element);
        const send = (kind, text) => {
            const pText = stringToNewUTF8(text);
            {{{ makeDynCall('vpip', 'pCallback') }}}(pUserData, kind, pText);
            _free(pText);
        };
        element.addEventListener('compositionupdate', (event) => send(0, event.data || ''));
        element.addEventListener('compositionend', (event) => {
            send(1, event.data || '');
            send(0, '');
            element.value = '';
        });
        element.addEventListener('input', (event) => {
            if (event.isComposing) return; // 조합 중 글은 compositionupdate 가 낸다
            if (event.inputType === 'insertText' && event.data) send(1, event.data);
            element.value = ''; // 칸은 늘 비운다 — 지우기 · 커서 이동은 키(keydown)로 엔진이 한다
        });
        const refocus = () => {
            if (swWebTextInput.bWanted && document.activeElement !== element) element.focus({ preventScroll: true });
        };
        document.addEventListener('touchend', refocus, true);
        document.addEventListener('mouseup', refocus, true);
        swWebTextInput.element = element;
        return 1;
    },

    swWebTextInputSetWanted__deps: ['$swWebTextInput'],
    swWebTextInputSetWanted: (bWanted) => {
        const element = swWebTextInput.element;
        swWebTextInput.bWanted = bWanted !== 0;
        if (element === null) return;
        if (swWebTextInput.bWanted) {
            element.focus({ preventScroll: true });
        } else {
            element.value = '';
            element.blur();
        }
    },

    // 자리는 캔버스 안 CSS 픽셀이다(C++ 가 물리 픽셀을 devicePixelRatio 로 나눈다).
    swWebTextInputSetArea__deps: ['$swWebTextInput', '$UTF8ToString'],
    swWebTextInputSetArea: (pCanvasSelector, x, y, lineHeight) => {
        const element = swWebTextInput.element;
        const canvas = document.querySelector(UTF8ToString(pCanvasSelector));
        if (element === null || canvas === null) return;
        const rect = canvas.getBoundingClientRect();
        element.style.left = (rect.left + x) + 'px';
        element.style.top = (rect.top + y) + 'px';
        element.style.height = Math.max(1, lineHeight) + 'px';
    },
});
