# ==============================================================================
# @file Test/AppTest/CMakeLists.txt
# @brief App(런처) 로직 단위 테스트 — 창·RHI·모듈 없이 혼자 도는 것만
# ==============================================================================

# `App` 은 실행 파일이라 링크할 라이브러리가 없다. 그래서 테스트가 볼 수 있는 것은 **파일 단위로
# 가져온 소스**뿐이고, 여기 담는 것은 엔진 상태 없이 혼자 도는 정책 코드로 한정한다(프레임 시간
# 정책 등). 창·RHI·모듈 로딩이 필요한 것은 `SmokeTest` 와 실기동의 몫이다 — 그 경계를 지켜야
# 이 타깃이 "App 을 통째로 세우는 두 번째 자리" 가 되지 않는다.
#
# 소스를 명시하는 이유도 같다. 글롭이면 App 소스를 하나 끌어올 때마다 그 의존성이 조용히 따라온다.
sw_addTestExecutable(AppTest
	SOURCES
		TestFrameTimeline.cpp
		${CMAKE_SOURCE_DIR}/Source/App/Frame/FrameTimeline.cpp
	LIBS
		Engine
	LABELS
		"unit;app;nogpu"
	TIMEOUT
		30
)
