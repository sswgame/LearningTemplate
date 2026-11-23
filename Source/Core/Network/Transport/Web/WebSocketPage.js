/**
 * @file WebSocketPage.js
 * @brief 스트림 전송이 묻는 페이지 정보(Emscripten JS 라이브러리) — C++ 쪽은 Core/Network/Transport/Web/WebSocketStreamTransport.cpp.
 */
addToLibrary({
    // https 페이지는 ws:// 를 열지 못한다(혼합 콘텐츠). location 은 창의 것을 본다 - 워커의 location 은 blob: 일 수 있다.
    swWebSocketIsSecurePage__proxy: 'sync',
    swWebSocketIsSecurePage: () => (typeof location !== 'undefined' && location.protocol === 'https:' ? 1 : 0),
});
