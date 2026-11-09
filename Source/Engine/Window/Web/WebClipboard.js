/**
 * @file WebClipboard.js
 * @brief 웹 클립보드의 브라우저 쪽(Emscripten JS 라이브러리) — C++ 쪽은 Engine/Window/Web/WebClipboard.cpp(방식 설명도 거기).
 */
addToLibrary({
    $swWebClipboard: { text: null, bInstalled: false },

    swWebClipboardInstall__deps: ['$swWebClipboard'],
    swWebClipboardInstall: () => {
        if (typeof document === 'undefined') return 0;
        if (!swWebClipboard.bInstalled) {
            swWebClipboard.bInstalled = true;
            document.addEventListener('paste', (event) => {
                swWebClipboard.text = event.clipboardData ? event.clipboardData.getData('text/plain') : '';
            });
        }
        return 1;
    },

    swWebClipboardRead__deps: ['$swWebClipboard', '$stringToNewUTF8'],
    swWebClipboardRead: () => (swWebClipboard.text !== null ? stringToNewUTF8(swWebClipboard.text) : 0),

    swWebClipboardWrite__deps: ['$swWebClipboard', '$UTF8ToString'],
    swWebClipboardWrite: (pText) => {
        const text = UTF8ToString(pText);
        swWebClipboard.text = text;
        if (typeof navigator === 'undefined' || !navigator.clipboard || !navigator.clipboard.writeText) return 0;
        navigator.clipboard.writeText(text).catch((error) => console.warn('[Clipboard] the browser refused writeText: ' + error));
        return 1;
    },
});
