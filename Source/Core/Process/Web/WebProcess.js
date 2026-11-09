/**
 * @file WebProcess.js
 * @brief 웹 프로세스 창구의 브라우저 쪽(Emscripten JS 라이브러리) — C++ 쪽은 Core/Process/Web/WebProcess.cpp.
 */
addToLibrary({
    swWebOpenURL__deps: ['$UTF8ToString'],
    swWebOpenURL: (pURL) => (window.open(UTF8ToString(pURL), '_blank') !== null ? 1 : 0),

    swWebHostName__deps: ['$stringToNewUTF8'],
    swWebHostName: () => stringToNewUTF8(typeof location !== 'undefined' ? location.hostname : ''),
});
