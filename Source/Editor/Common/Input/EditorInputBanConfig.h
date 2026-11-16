/**
 * @file EditorInputBanConfig.h
 * @brief 금지 헤더(`EditorInputBan.h`)를 받지 않는 어댑터 표시의 기본값(0)입니다. 에디터 코드 타깃의 모든 소스가 소스 첫 줄보다 먼저 강제 포함한다(`sw_apply_editor_input_ban`).
 * @details 표시는 CMake 만 준다 — 어댑터 파일에 소스 속성(`SW_EDITOR_INPUT_ADAPTER=1` · `SW_EDITOR_NODE_GRAPH_ADAPTER=1` · `SW_EDITOR_WIDGET_ADAPTER=1`)으로 넣고, 나머지 파일은 여기서 0 이 된다.
 *          소스 안에서 `#define` 으로 1 을 주면 이 기본값을 다시 정의하는 것이라 `-Werror=macro-redefined` 로 컴파일이 멈춘다 — 다른 파일이 금지를 풀 수 없다.
 *          값으로 읽으므로(`-Werror=undef`) 이 헤더가 빠진 타깃은 금지 헤더에서 멈춘다(조용히 금지가 풀리지 않는다).
 */
#pragma once

#ifndef SW_EDITOR_INPUT_ADAPTER
    #define SW_EDITOR_INPUT_ADAPTER 0
#endif
#ifndef SW_EDITOR_NODE_GRAPH_ADAPTER
    #define SW_EDITOR_NODE_GRAPH_ADAPTER 0
#endif
#ifndef SW_EDITOR_WIDGET_ADAPTER
    #define SW_EDITOR_WIDGET_ADAPTER 0
#endif
