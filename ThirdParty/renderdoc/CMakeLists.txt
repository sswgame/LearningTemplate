# ==============================================================================
# @file ThirdParty/renderdoc/CMakeLists.txt
# @brief RenderDoc in-app API 헤더 하나(`renderdoc_app.h`, RenderDoc v1.35 원문 — MIT, `LICENSE.md`). 링크할 라이브러리는 없다.
#        엔진 `RenderDocCapture`(Dev 만)가 이미 올라온 renderdoc.dll · librenderdoc.so 에서 `RENDERDOC_GetAPI` 를 찾아 부른다.
#        헤더는 `Source/Engine/Renderer/Capture/` 에서만 include 한다(CheckThirdPartyIsolation.py).
# ==============================================================================

add_library(renderdoc INTERFACE)
sw_thirdPartySystemIncludes(renderdoc INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
