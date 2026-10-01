# ==============================================================================
# @file Test/AppTest/CMakeLists.txt
# @brief App(런처) 테스트 — 혼자 도는 정책(nogpu) + 실기동 스모크(hostgpu)
# ==============================================================================

# `App` 은 실행 파일이라 링크할 라이브러리가 없다. 그래서 테스트가 볼 수 있는 것은 **파일 단위로
# 가져온 소스**뿐이고, 여기 담는 것은 엔진 상태 없이 혼자 도는 정책 코드로 한정한다(프레임 시간
# 정책 등). 그 경계를 지켜야 이 타깃이 "App 을 통째로 세우는 두 번째 자리" 가 되지 않는다.
#
# **기동 전체는 프로세스를 띄워서 본다**(`TestAppSmoke.cpp`) — `EngineLoop` 은 어떤 단위 테스트도
# 돌리지 않으므로, 그 그물은 실제 `App.exe` 를 돌려 보는 것뿐이다. 그 집합은 GPU·창이 필요하니
# 아래에서 `hostgpu` 로 갈라 둔다.
#
# 소스를 명시하는 이유도 같다. 글롭이면 App 소스를 하나 끌어올 때마다 그 의존성이 조용히 따라온다.
sw_addTestExecutable(AppTest
	HOST_SPLIT
	SOURCES
		TestAppSmoke.cpp
		TestFrameTimeline.cpp
		${CMAKE_SOURCE_DIR}/Source/App/FrameTimeline.cpp
	LIBS
		Engine
	LABELS
		# `nogpu` · `hostgpu` 는 `HOST_SPLIT` 이 붙인다 — `AppTest_NoGPU` 는 실기동 스모크(`AppSmokeTest`)를 빼고,
		# `AppTest_HostOnly` 는 그것만 돈다(직렬 — 창을 띄우고 GPU 를 잡는다).
		"unit;app"
	TIMEOUT
		60
	HOST_TIMEOUT
		300
)

# 스모크는 **빌드된 App 을 띄운다** — 타깃이 옆에 서 있어야 한다.
if(TARGET App)
	add_dependencies(AppTest App)
endif()
if(TARGET EditorModule)
	add_dependencies(AppTest EditorModule)
endif()
if(TARGET SWGame)
	add_dependencies(AppTest SWGame)
endif()
sw_getDynamicModules(listRhiBackend KINDS rhi)
foreach(rhiMod IN LISTS listRhiBackend)
	add_dependencies(AppTest ${rhiMod})
endforeach()
