# ==============================================================================
# @file cmake/Modules/Build/AssetAndToolTargets.cmake
# @brief 에셋 쿠킹(CookAssets), 서드파티 라이선스 고지
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/../Environment/PythonUtils.cmake")

# ------------------------------------------------------------------------------
# 스크립트 타겟 — 에셋 쿠킹, 서드파티 고지
# ------------------------------------------------------------------------------
# 파이썬은 PythonUtils.cmake(머리의 include)가 찾았다 — 없으면 GenerateConfigConstants 의 REQUIRED 호출에서 configure 가 그전에 섰다.
# 팩은 실행 파일 옆(Bin/Packs)에 놓는다. App 이 exeDir/Packs 를 먼저 찾기 때문.
# 주의: CMAKE_RUNTIME_OUTPUT_DIRECTORY 를 이어 붙이지 않는다 — 생성기 식(`$<0:>`)이 든 값이라 명령줄 경로가 되지 않는다.
set(pack_output_dir "${CMAKE_BINARY_DIR}/Bin/Packs")

# 배포 빌드는 런타임 셰이더 컴파일이 없다 — 쿠킹해 둔 바이너리가 소스와 어긋나 있으면 화면이
# 통째로 비고, 그 사실이 실행해 보기 전까지 드러나지 않는다. 그래서 팩을 구울 때 셰이더를 함께 굽고 판정하며, 어긋나면 패킹하지 않고 빌드를 세운다.
# 프리팹·씬 쿠킹 산출물은 소스 트리(Content/)가 아니라 빌드 폴더에 스테이징한다 — 소스 옆에 두면
# 낡은 .bin 이 남아 Dev 런타임이 그것으로 물러나 실패를 가린다. 팩 안 경로는 같다(cookPack 이 병합).
# 씬 쿠킹은 리플렉션이 필요해 엔진 실행 파일(App · Server 의 DevTools/Cook)이 한다 — 전용 서버 타깃에는 App 이 없어 Server 가 쿠킹한다(같은 Headless 단계).
# 서버는 셰이더를 쿠킹 · 검증하지 않는다(DXC 가 없고, 그릴 것이 없다).
# 누가 굽는가: 개발 구성은 그 빌드의 실행 파일, 교차 빌드와 배포 빌드는 호스트 개발 빌드의 실행 파일이다 — 쿠커(DevTools)는 배포 구성에 없다(언리얼도 에디터 빌드가 배포본을 굽는다).
if(SW_TARGET_TYPE STREQUAL "Server")
	set(cook_host_name Server)
else()
	set(cook_host_name App)
endif()
# 쿠킹은 DevTools/Cook 한 곳이다(`App --cook-all`: 씬 · 프리팹 · 레지스트리 · VAT · 내비메시를 스테이징하고 팩을 굽는다 — `PackCooker`).
# 빌드 타깃이 빼는 에셋 종류(Config/Engine/CookContract.json 의 target_excluded_asset_kinds — 서버: 텍스처 · 셰이더 바이너리 · 오디오)는
# 쿠킹하는 호스트가 정한다(Server 가 구우면 서버 팩이다). 팩 설정은 Config/Engine/PackConfig.json 이다.
# 셰이더도 같은 스테이징에 굽고(바뀐 것만) 소스와 맞는지 본 뒤 팩에 담는다(`cookShadersForPack`) — 따로 주는 인자가 없다.
# 쿠킹 로그는 고정 자리(`<빌드>/Testing/Logs/Cook.log`)에 남긴다 — 빌드 출력에 섞이면 첫 굽기만 틀린 일(팩에 빠진 셰이더 바이너리)을 나중에 볼 수 없었다.
# `Scripts/dev/shipping_check.py` 가 실패할 때 이 로그의 오류 · 경고를 함께 찍는다(같은 자리 이름 `COOK_LOG_NAME`).
# 꾸미기 스키마와 상태 배치(굽는 실행 파일이 쓴다)를 출시 스냅숏과 먼저 견준다(Scripts/dev/format_release.py) — 출시 뒤에는 옛 세이브 · 공유 코드를 올릴 단계가 빠진 바뀜을 굽지 않는다.
# 빌드는 검사만 한다 — 소스 트리에 쓰지 않는다(빌드 뒤 작업 트리가 깨끗해야 검증한 커밋이 곧 지은 것이다). 추론할 수 있는 것(매개변수 제거)은
# 오류가 `py -3 -m Scripts format-release --fix` 를 안내하고(그 명령이 Remove 단계 · 판 올림을 적는다), 이름 · 종류 바꾸기는 적을 줄을 안내하고 멈춘다.
set(format_release_check "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/dev/format_release.py" check --layouts "${CMAKE_BINARY_DIR}/StateLayouts.json")
set(cook_args --cook-all "--cooked-dir=${CMAKE_BINARY_DIR}/Cooked" "--pack-dir=${pack_output_dir}" "-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Cook.log")
# 정적 모듈 구성(Shipping · 웹 · Android 개발)은 활성 게임(SW_ACTIVE_GAME)의 팩만 굽는다 — 링크한 게임이 하나라 다른 팩은 쓰이지 않는다(`Pack` 도 그 게임의 팩만 고른다).
# 동적 Dev 프리셋은 실행할 때 `-game=` 으로 게임을 고르므로 어느 팩이 필요할지 몰라 모두 굽는다.
if(SW_STATIC_MODULES)
	list(APPEND cook_args "--game=${SW_ACTIVE_GAME}")
endif()
# 배포 팩은 시나리오를 담지 않는다. 정적 개발 구성(웹 · Android)은 담는다 — 그 빌드의 자동화 실행기가 `-scenario` 로 돌린다(감사 P-9).
if(SW_SHIPPING_BUILD)
	list(APPEND cook_args --pack-for-shipping)
endif()
# 정적 모듈 구성은 CookAssets 가 기본 빌드에 든다(ExecutableTargets.cmake) — 쿠킹 명령을 입력 지문 도구로 감싸 바뀐 것이 없으면 건너뛴다.
# 입력: 콘텐츠 · 설정 폴더, 쿠커가 든 실행 파일 폴더와 그 Modules(바로 아래 파일만 — 교차 빌드는 호스트 빌드의 것). 출력(스테이징 · 팩 폴더)을 지우거나 고쳐도 다시 돈다.
# 지문 파일은 출력 폴더 밖에 둔다(안에 두면 쓴 파일이 다음 지문을 바꾼다). 동적 Dev 는 손으로 부르는 타깃이라 늘 돈다.
# 꾸미기 대조와 호스트 빌드는 감싸지 않는다 — 앞은 싸고, 뒤는 쿠커 실행 파일을 지문 전에 새로 지어야 한다.
function(sw_cook_command_prefix COOKER_DIR OUT_VAR)
	if(NOT SW_STATIC_MODULES)
		set(${OUT_VAR} "" PARENT_SCOPE)
		return()
	endif()
	set(${OUT_VAR} "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/cook_if_inputs_changed.py"
		--stamp "${CMAKE_BINARY_DIR}/CookAssets.inputs"
		--input "${CMAKE_SOURCE_DIR}/Content" --input "${CMAKE_SOURCE_DIR}/Config"
		--binaries "${COOKER_DIR}" --binaries "${COOKER_DIR}/Modules"
		--output "${CMAKE_BINARY_DIR}/Cooked" --output "${pack_output_dir}"
		-- PARENT_SCOPE)
endfunction()

if(CMAKE_CROSSCOMPILING OR SW_SHIPPING_BUILD)
	# 교차 빌드(Android · 웹)의 App 은 이 기계에서 돌지 않고 배포 구성에는 쿠커가 없다 — 호스트 개발 빌드의 App(서버 타깃은 Server)이 굽는다(cmake/Modules/Build/HostTools.cmake).
	# 팩 내용은 다른 배포 대상과 같다. 대상마다 다른 것은 셋뿐이다:
	#   - 셰이더 바이너리: 이 빌드의 배포 백엔드(SW_SHIPPING_RHI_BACKEND — Android Vulkan, 웹 WebGPU)를 명령줄 이름(CookContract.json command_line_name)으로 넘긴다.
	#   - 조건(`_when`)의 platform 축: 이 빌드의 플랫폼(`--cook-platform`).
	#   - 텍스처(교차 빌드만): 모든 배포 대상의 형식 변형(ASTC · ETC2 — EngineConfig.json _textureFormats)을 먼저 굽는다(--cook-texture-variants, 에디터 모듈이 있는 개발 빌드의 일).
	#     팩은 곁 파일 규칙으로 변형을 함께 담고, 실행이 플랫폼의 대상(_listPlatform)에서 장치가 지원하는 첫 묶음을 고른다.
	# shortcut: 같은 작업 트리의 데스크톱 팩도 구워 둔 변형을 곁 파일로 담는다(배포 크기), 크기가 문제가 되면 PackCooker 가 굽는 대상의 묶음 변형만 담게 한다.
	if(CMAKE_HOST_WIN32)
		set(host_exe_suffix ".exe")
	elseif(CMAKE_HOST_UNIX)
		set(host_exe_suffix "")
	else()
		message(FATAL_ERROR "[Cook] unsupported host (Windows or Unix)")
	endif()
	# 굽는 실행 파일은 호스트 빌드(SW_HOST_BUILD_DIR — HostTools.cmake)의 것이고, 굽기 앞에 그 빌드를 다시 짓는다 — 형식(EngineConfig 스키마 등)이 바뀐 소스로
	# 낡은 쿠커가 돌면 설정 읽기부터 멈췄다. 게임 모듈 · 에디터 모듈(텍스처 변형 굽기)도 함께라 대상 하나가 아니라 빌드 전체를 짓는다.
	# 같은 호스트 빌드 폴더를 다른 빌드가 동시에 짓지 않게 하는 것은 부르는 쪽(preflight.sh `job_shipping_build`)이다 — ninja 는 폴더 잠금이 없다.
	if(NOT SW_HOST_BUILD_DIR)
		message(FATAL_ERROR "[Cook] SW_HOST_BUILD_DIR is empty - include cmake/Modules/Build/HostTools.cmake before this file (CMakeLists.txt)")
	endif()
	set(host_cooker "${SW_HOST_BUILD_DIR}/Bin/${cook_host_name}${host_exe_suffix}")
	unset(host_exe_suffix)
	sw_check_rhi_backend_name("${SW_SHIPPING_RHI_BACKEND}")
	set(shipping_backend_flag "-${SW_RHI_BACKEND_${SW_SHIPPING_RHI_BACKEND}_COMMAND_LINE_NAME}")
	# 리소스 조건(`_when`)의 platform 축은 굽는 App 이 아니라 이 빌드의 플랫폼이다 — 호스트 App 이 구운 Android 팩이 Windows 조건을 푼 채 나가지 않게.
	string(TOLOWER "${SW_PLATFORM_NAME}" cook_platform)
	list(APPEND cook_args "--cook-platform=${cook_platform}")
	unset(cook_platform)
	if(NOT SW_STATIC_MODULES)
		message(FATAL_ERROR "[Cook] cross and shipping builds link modules statically (SW_STATIC_MODULES) - the cook command chain below needs the input fingerprint wrapper")
	endif()
	set(import_steps "")
	if(CMAKE_CROSSCOMPILING)
		# 원본(`textures_raw/`)을 `textures/*.dds` 로 먼저 임포트한다(바뀐 것만 — 내용 해시). 임포트 산출물은 저장소에 없어(.gitignore) 깨끗한 트리에서는 팩이 참조를 못 찾고 멈춘다.
		set(import_steps "${host_cooker}" --import-textures --then "${host_cooker}" --cook-texture-variants --then)
	endif()
	sw_cook_command_prefix("${SW_HOST_BUILD_DIR}/Bin" cook_prefix)
	add_custom_target(CookAssets
		COMMAND "${CMAKE_COMMAND}" --build "${SW_HOST_BUILD_DIR}"
		COMMAND "${host_cooker}" "--write-state-layouts=${CMAKE_BINARY_DIR}/StateLayouts.json"
		COMMAND ${format_release_check}
		COMMAND ${cook_prefix} ${import_steps} "${host_cooker}" ${cook_args} ${shipping_backend_flag}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Cooking Resource packs for ${SW_SHIPPING_RHI_BACKEND} with the host development build (${host_cooker} --cook-all ${shipping_backend_flag})..."
		VERBATIM
	)
	unset(import_steps)
	unset(shipping_backend_flag)
	unset(host_cooker)
else()
	# 이 빌드의 실행 파일이 굽는다 — 씬 쿠킹이 그 실행 파일이라 **뒤**에 와야 하고(의존은 sw_configure_app_dependencies 가 건다), 경로는 빌드 폴더를 뒤지지 않고 CMake 가 그대로 넘긴다.
	# 방향을 거꾸로(App 이 CookAssets 에 의존) 걸면 깨끗한 트리(CI)에서 아직 없는 App 을 찾다가 죽는다 — 로컬에서는 다른 프리셋의 낡은 App.exe 가 우연히 있어 지나간다.
	set(SW_COOK_HOST_TARGET ${cook_host_name})
	sw_cook_command_prefix("$<TARGET_FILE_DIR:${SW_COOK_HOST_TARGET}>" cook_prefix)
	add_custom_target(CookAssets
		COMMAND "$<TARGET_FILE:${SW_COOK_HOST_TARGET}>" "--write-state-layouts=${CMAKE_BINARY_DIR}/StateLayouts.json"
		COMMAND ${format_release_check}
		COMMAND ${cook_prefix} "$<TARGET_FILE:${SW_COOK_HOST_TARGET}>" ${cook_args}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Cooking scenes, prefabs (XML/JSON) and Resource packs to binary (${SW_COOK_HOST_TARGET} --cook-all)..."
		VERBATIM
	)
endif()
unset(cook_host_name)
unset(format_release_check)
unset(cook_prefix)
set_target_properties(CookAssets PROPERTIES FOLDER "Engine/Scripts")

# 서드파티 고지 — 배포물(Bin · Shipping 패키지)에 함께 놓는다. vcpkg 설치 트리의 `share/<포트>/copyright` 를 이 매니페스트가
# 끌어오는 포트만 모아 쓴다(소스 트리 밖 설치 트리를 읽기 전용으로 빌려 쓸 수 있어 트리 전체가 아니다 — VcpkgInstallPolicy.cmake). 포트가 바뀌면 status 가 바뀌어 다시 만든다.
# 저장소에 원문 그대로 둔 코드(`ThirdParty/<이름>/LICENSE.md`)의 라이선스도 뒤에 붙인다.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND EXISTS "${VCPKG_INSTALLED_DIR}/vcpkg/status")
	set(third_party_notices "${CMAKE_BINARY_DIR}/Bin/THIRD_PARTY_NOTICES.txt")
	file(GLOB vendored_licenses CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ThirdParty/*/LICENSE.md")
	# 켠 매니페스트 기능(복합 문자)의 포트도 배포물에 들어간다 — 고지에 넣게 넘긴다.
	string(JOIN "," manifest_features ${VCPKG_MANIFEST_FEATURES})
	add_custom_command(
		OUTPUT "${third_party_notices}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/third_party_notices.py"
			--manifest "${CMAKE_SOURCE_DIR}/vcpkg.json" --installed "${VCPKG_INSTALLED_DIR}"
			--triplet "${VCPKG_TARGET_TRIPLET}" --out "${third_party_notices}" --vendored-root "${CMAKE_SOURCE_DIR}/ThirdParty"
			"--features=${manifest_features}"
		DEPENDS "${CMAKE_SOURCE_DIR}/vcpkg.json" "${VCPKG_INSTALLED_DIR}/vcpkg/status"
			"${CMAKE_SOURCE_DIR}/Scripts/generate/third_party_notices.py" ${vendored_licenses}
		COMMENT "Collecting third-party license notices..."
		VERBATIM
	)
	add_custom_target(ThirdPartyNotices ALL DEPENDS "${third_party_notices}")
	set_target_properties(ThirdPartyNotices PROPERTIES FOLDER "Engine/Scripts")
endif()
