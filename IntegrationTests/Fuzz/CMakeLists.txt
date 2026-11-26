# ==============================================================================
# @file IntegrationTests/Fuzz/CMakeLists.txt
# @brief 퍼저 타깃 — 신뢰 경계의 파서만(`security-and-hardening`). 프리셋 Linux-Fuzz(SW_ENABLE_FUZZING)에서만 짓는다.
#        타깃마다 `Fuzz_<이름>` 과 `Fuzz.<이름>.Smoke`(cmake/Modules/Build/FuzzTargets.cmake). 씨앗은 `Seeds/<이름>/`.
#        찾은 크래시는 바이트를 `Tests/Snapshots/` 에 넣고 단위 테스트 케이스로 남긴다 — 퍼저 없는 구성(Windows 포함)에서 회귀를 본다.
# ==============================================================================

# 엔진 범위(`LLVMFuzzerInitialize`)는 타깃마다 같은 파일 하나를 함께 짓는다 — 타깃 파일은 `LLVMFuzzerTestOneInput` 하나뿐이다.
set(fuzz_engine_scope "${CMAKE_CURRENT_SOURCE_DIR}/FuzzEngineScope.cpp")

# ① 모드 · 세이브 글의 XML(Engine) ② 받은 패치 · DLC · 모드 팩의 머리 · 색인(Engine) ③ 게임 세션 스트림 프레임(Core) ④ 게임 세션 UDP 패킷(Core)
# ⑤ HTTP 응답 · 설정의 JSON(Engine) ⑥ 세이브 봉투 · 스냅샷 머리(GameFramework)
sw_add_fuzz_target(XMLDocument SOURCES FuzzXMLDocument.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(PackReader SOURCES FuzzPackReader.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(StreamFrame SOURCES FuzzStreamFrame.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(SessionPacket SOURCES FuzzSessionPacket.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)
sw_add_fuzz_target(JSONDocument SOURCES FuzzJSONDocument.cpp ${fuzz_engine_scope} LINK Engine ModuleManager)

# 세이브 머리의 몸은 GameFramework 를 부르는 모듈이다 — 실행 파일이 GameFramework 를 링크하면 이름 풀이 서기 전에 올라온다(Source/Programs/OnlineLoadBot/CMakeLists.txt 와 같은 까닭).
add_library(FuzzSaveHeaderCore SHARED FuzzSaveHeaderModule.cpp)
target_link_libraries(FuzzSaveHeaderCore PRIVATE sw_public_source_includes Engine GameFramework sw_global_options)
target_compile_definitions(FuzzSaveHeaderCore PRIVATE "SW_LOG_TAG=\"FuzzSaveHeaderCore\"" "SW_REGISTRAR_MODULE=\"FuzzSaveHeaderCore\"")
sw_configure_dll_exports(FuzzSaveHeaderCore SHARED MODULE_SHARED)
sw_configure_pch(FuzzSaveHeaderCore "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(FuzzSaveHeaderCore PROPERTIES
	FOLDER "IntegrationTests/Fuzz"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>"
)
sw_add_fuzz_target(SaveHeader SOURCES FuzzSaveHeader.cpp ${fuzz_engine_scope} LINK Engine ModuleManager MODULES GameFramework)
add_dependencies(Fuzz_SaveHeader FuzzSaveHeaderCore)
