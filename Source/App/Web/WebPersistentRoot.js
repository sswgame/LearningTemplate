/**
 * @file WebPersistentRoot.js
 * @brief 웹 영속 폴더(IDBFS)를 붙이는 Emscripten JS 라이브러리 — C++ 쪽은 App/AppEntry.cpp 의 `swMountWebPersistentRoot` 선언.
 * @details JS 본문은 C++ 소스(EM_JS) 대신 여기 둔다: C++ 포맷터가 매크로 본문의 `=>` · `!==` 를 띄워 브라우저 JS 를 깨뜨렸다. 링크는 Platform/Web.cmake 가 `Source/** /Web/*.js` 를 모두 넘긴다.
 *          지난 세션의 파일을 IndexedDB 에서 읽어 올 때까지 기다린다(JSPI — 부팅 전에 끝나야 세이브 · 설정이 보인다).
 *          쓰기는 페이지가 숨거나 닫힐 때 IndexedDB 로 내린다(브라우저는 닫기 전에 비동기 일을 기다려 주지 않으므로 숨김 때 먼저 내린다).
 */
addToLibrary({
    swMountWebPersistentRoot__deps: ['$FS', '$IDBFS', '$UTF8ToString'],
    swMountWebPersistentRoot__async: true,
    swMountWebPersistentRoot: async (pRoot) => {
        const root = UTF8ToString(pRoot);
        FS.mkdirTree(root);
        FS.mount(IDBFS, {}, root);
        await new Promise((resolve) => FS.syncfs(true, (error) => {
            if (error) console.error('IDBFS load failed', error);
            resolve();
        }));
        const flush = () => FS.syncfs(false, (error) => {
            if (error) console.error('IDBFS save failed', error);
        });
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'hidden') flush();
        });
        window.addEventListener('pagehide', flush);
    },
});
