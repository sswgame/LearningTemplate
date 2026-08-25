# ==============================================================================
# @file Source/RuntimeAPI/CMakeLists.txt
# @brief App ↔ Editor/Game 모듈 공유 Runtime API (순수 헤더 온리)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) RuntimeAPI — INTERFACE 라이브러리 (순수 헤더)
#    구현체(ModuleHost)는 App 레이어로 이동되었습니다.
# ------------------------------------------------------------------------------
sw_addLibrary(RuntimeAPI
    TYPE INTERFACE
)

# Engine을 인터페이스 링크에 추가하여 API를 포함하는 측에서 Engine 헤더도 볼 수 있게 합니다
target_link_libraries(RuntimeAPI INTERFACE Engine)
