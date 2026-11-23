/**
 * @file AutomationDOMEvent.js
 * @brief 자동화 단계 `DispatchDOMEvent` 의 브라우저 쪽(Emscripten JS 라이브러리) — 사람의 입력과 같은 DOM 이벤트를 페이지에 냅니다. C++ 쪽은 DevTools/Client/Automation/Scenario/AutomationWindowSteps.cpp.
 * @details 창(`WebWindow`)과 숨은 글 칸(`WebTextInput.js`)이 건 처리기를 그대로 지난다. 글 · 조합은 지금 포커스를 가진 글 칸에 낸다 — 글 칸이 포커스를 잡지 못했으면 0(실패)이다.
 *          https://developer.mozilla.org/docs/Web/API/EventTarget/dispatchEvent · https://developer.mozilla.org/docs/Web/API/CompositionEvent
 */
addToLibrary({
    swAutomationDispatchDOMEvent__deps: ['$UTF8ToString'],
    // kind 은 AutomationDOMEventKind 순서, x · y 는 캔버스 안의 비율(0 ~ 1)이다.
    swAutomationDispatchDOMEvent: (kind, pValue, x, y) => {
        const value = UTF8ToString(pValue);
        const field = document.activeElement instanceof HTMLTextAreaElement ? document.activeElement : null;
        switch (kind) {
        case 0: // KeyDown
        case 1: // KeyUp
            (document.activeElement || document.body).dispatchEvent(new KeyboardEvent(kind === 0 ? 'keydown' : 'keyup', { code: value, key: value, bubbles: true, cancelable: true }));
            return 1;
        case 2: // Text - 조합 없는 확정
            if (field === null) return 0;
            field.value = value;
            field.dispatchEvent(new InputEvent('input', { data: value, inputType: 'insertText', bubbles: true }));
            return 1;
        case 3: // Composition - 조합 중 글 전체
            if (field === null) return 0;
            field.value = value;
            field.dispatchEvent(new CompositionEvent('compositionupdate', { data: value, bubbles: true }));
            field.dispatchEvent(new InputEvent('input', { data: value, inputType: 'insertCompositionText', isComposing: true, bubbles: true }));
            return 1;
        case 4: // CompositionEnd - 확정
            if (field === null) return 0;
            field.dispatchEvent(new CompositionEvent('compositionend', { data: value, bubbles: true }));
            return 1;
        default: { // PointerMove · PointerDown · PointerUp
            const canvas = document.querySelector('#canvas');
            if (canvas === null) return 0;
            const rect = canvas.getBoundingClientRect();
            const name = kind === 5 ? 'mousemove' : (kind === 6 ? 'mousedown' : 'mouseup');
            canvas.dispatchEvent(new MouseEvent(name, { clientX: rect.left + rect.width * x, clientY: rect.top + rect.height * y, button: 0, bubbles: true, cancelable: true }));
            return 1;
        }
        }
    },
});
