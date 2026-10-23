# ==============================================================================
# @file Source/RuntimeAPI/CMakeLists.txt
# @brief 순수 C-ABI 런타임 인터페이스 (INTERFACE 라이브러리)
# @details ABI/ 계약, Service/ 로케이터, Export/ 모듈 매크로
# ==============================================================================

add_library(RuntimeAPI INTERFACE)

# 헤더는 `RuntimeAPI/...` 전체 경로로 쓴다 — 이 폴더는 include 경로가 아니다(`sw_verifySourceIncludeRoots`).
# Export/ 의 매크로는 모듈 자신의 서비스 로케이터(Editor/, GameFramework/)를 끌어오므로
# Source/ 가 include 경로에 있어야 한다. 그 선언은 sw_public_source_includes 하나가 갖는다(여기서 따로 적지 않는다).
target_link_libraries(RuntimeAPI INTERFACE sw_public_source_includes)
set_target_properties(RuntimeAPI PROPERTIES FOLDER "Source/RuntimeAPI")
