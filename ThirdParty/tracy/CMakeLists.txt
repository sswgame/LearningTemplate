# ==============================================================================
# @file ThirdParty/tracy/CMakeLists.txt
# @brief Tracy 프로파일러 클라이언트(BSD-3 — 배포물에 고지 필요). vcpkg 기능은 끄고(크래시 처리기 · 도구 없음) 클라이언트 라이브러리만 쓴다.
#        x64-windows 는 공유 라이브러리(TracyClient.dll, TRACY_ENABLE · TRACY_IMPORTS 는 임포트 타깃이 싣는다).
#        이 저장소의 clang-cl 트리플릿용 C++17 오버레이 포트는 `vcpkg-port/tracy`.
# ==============================================================================

sw_addVcpkgConfigLib(
	NAME tracy
	PACKAGE Tracy
	CONFIG_TARGET Tracy::TracyClient
)
