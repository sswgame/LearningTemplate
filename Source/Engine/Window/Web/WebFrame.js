/**
 * @file WebFrame.js
 * @brief 브라우저의 다음 프레임을 기다리는 Emscripten JS 라이브러리 — C++ 쪽은 App/AppEntry.cpp(루프)와 Engine/Window/Web/WebWindow.cpp(간격 · 프레임 사이인지).
 * @details 메인 루프는 `main` 안의 반복문이고 프레임 사이를 JSPI 로 멈춘다(`emscripten_sleep` 과 같은 `__async: 'auto'`). `emscripten_set_main_loop` 의 콜백은
 *          WebAssembly.promising 으로 감싼 호출이 아니라(emsdk 6.0.13 libeventloop.js) 그 안에서 GPU 를 기다리면(`wgpuInstanceWaitAny`) SuspendError 로 멈춘다.
 *          https://github.com/WebAssembly/js-promise-integration/blob/main/proposals/js-promise-integration/Overview.md
 */
addToLibrary({
    $swWebFrame: { intervalMilliseconds: 0, startTime: 0, bWaiting: false },

    swWebFrameSetInterval__deps: ['$swWebFrame'],
    swWebFrameSetInterval: (milliseconds) => {
        swWebFrame.intervalMilliseconds = milliseconds;
    },

    swWebFrameIsWaiting__deps: ['$swWebFrame'],
    swWebFrameIsWaiting: () => (swWebFrame.bWaiting ? 1 : 0),

    swWebFrameWait__deps: ['$swWebFrame'],
    swWebFrameWait__async: 'auto',
    swWebFrameWait: () => new Promise((resolve) => {
        const resume = () => {
            swWebFrame.bWaiting = false;
            swWebFrame.startTime = performance.now();
            resolve();
        };
        swWebFrame.bWaiting = true;
        if (swWebFrame.intervalMilliseconds > 0) setTimeout(resume, Math.max(0, swWebFrame.startTime + swWebFrame.intervalMilliseconds - performance.now()));
        else requestAnimationFrame(resume);
    }),
});
