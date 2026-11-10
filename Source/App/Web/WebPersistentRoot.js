/**
 * @file WebPersistentRoot.js
 * @brief 웹 영속 폴더(IDBFS)를 붙이고 내리는 Emscripten JS 라이브러리 — C++ 쪽은 App/AppEntry.cpp 의 `swMountWebPersistentRoot` · `swFlushWebPersistentRoot` 선언.
 * @details JS 본문은 C++ 소스(EM_JS) 대신 여기 둔다: C++ 포맷터가 매크로 본문의 `=>` · `!==` 를 띄워 브라우저 JS 를 깨뜨렸다. 링크는 Platform/Web.cmake 가 `Source/** /Web/*.js` 를 모두 넘긴다.
 *          지난 세션의 파일을 IndexedDB 에서 읽어 올 때까지 기다린다(JSPI — 부팅 전에 끝나야 세이브 · 설정이 보인다).
 *          쓰기는 IndexedDB 로 내리는 길이 셋이다: 페이지가 숨을 때 · 닫힐 때(브라우저는 닫기 전에 비동기 일을 기다려 주지 않으므로 숨김 때 먼저), 그리고 앱이 끝날 때
 *          (`swFlushWebPersistentRoot` — 페이지는 남아 있어 내리기가 끝난다. 앱 종료만으로는 숨김 · 닫힘이 없어 마지막 세이브가 IndexedDB 에 닿지 않았다).
 *          내리기는 메모리 파일 시스템(JS 객체)을 읽으므로 런타임이 내려간 뒤에도 끝난다. 모든 길이 `$swFlushPersistentRoot` 하나다.
 */
addToLibrary({
    $swFlushPersistentRoot__deps: ['$FS'],
    $swFlushPersistentRoot: () => FS.syncfs(false, (error) => {
        if (error) console.error('IDBFS save failed', error);
    }),
    swMountWebPersistentRoot__deps: ['$FS', '$IDBFS', '$UTF8ToString', '$swFlushPersistentRoot'],
    swMountWebPersistentRoot__async: true,
    swMountWebPersistentRoot: async (pRoot) => {
        const root = UTF8ToString(pRoot);
        FS.mkdirTree(root);
        FS.mount(IDBFS, {}, root);
        await new Promise((resolve) => FS.syncfs(true, (error) => {
            if (error) console.error('IDBFS load failed', error);
            resolve();
        }));
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'hidden') swFlushPersistentRoot();
        });
        window.addEventListener('pagehide', () => swFlushPersistentRoot());
    },
    swFlushWebPersistentRoot__deps: ['$swFlushPersistentRoot'],
    swFlushWebPersistentRoot: () => swFlushPersistentRoot(),
});
