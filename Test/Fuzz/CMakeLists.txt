# ==============================================================================
# @file Test/Fuzz/CMakeLists.txt
# @brief 퍼저 타깃 — 신뢰 경계의 파서만(`security-and-hardening`). 프리셋 Linux-Fuzz(SW_ENABLE_FUZZING)에서만 짓는다.
#        타깃마다 `Fuzz_<이름>` 과 `Fuzz.<이름>.Smoke`(cmake/Modules/Build/FuzzTargets.cmake). 씨앗은 `Seeds/<이름>/`.
#        찾은 크래시는 바이트를 `Tests/Snapshots/` 에 넣고 단위 테스트 케이스로 남긴다 — 퍼저 없는 구성(Windows 포함)에서 회귀를 본다.
# ==============================================================================

# 엔진 범위(`LLVMFuzzerInitialize`)는 타깃마다 같은 파일 하나를 함께 짓는다 — 타깃 파일은 `LLVMFuzzerTestOneInput` 하나뿐이다.
set(fuzz_engine_scope "${CMAKE_CURRENT_SOURCE_DIR}/FuzzEngineScope.cpp")

# ① 모드 · 세이브 글의 XML(Engine) ② 받은 패치 · DLC · 모드 팩의 머리 · 색인(Engine) ③ 게임 세션 스트림 프레임(Core)
sw_add_fuzz_target(XMLDocument SOURCES FuzzXMLDocument.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(PackReader SOURCES FuzzPackReader.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(StreamFrame SOURCES FuzzStreamFrame.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
