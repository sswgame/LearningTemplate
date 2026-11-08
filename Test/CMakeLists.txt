# ==============================================================================
# @file Test/CMakeLists.txt
# @brief CTest 등록만 한다 — 테스트 코드는 여기 없다. 단위 테스트는 기능 폴더 옆 `Tests/`(`SW_TEST_CASE`)에 있고 `App --test` 한 번이 모두 돈다.
#        시나리오는 `Content/**/automation/*.scenario.xml` 이고 `Test/Scenario/` 가 하나씩 올린다. 형식과 쓰는 법은 Test/README.md.
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

# 에셋 임포트 — 테스트는 임포트한 산출물(`.mesh` · `.dds` · 높이장)을 읽는다. 형식 · 임포터 버전을 올리면 스탬프가 낡아 다시 임포트해야 하는데,
# 손으로 돌리지 않으면 테스트가 낡은 산출물을 거절하며 돈다(2026-11-02 — `.mesh` 5 판에서 모델이 모두 빠진 채 시나리오가 돌았다). 그래서 ctest 가 먼저
# 낡은 것만 임포트한다(스탬프가 맞으면 곧바로 끝난다). 모든 테스트가 이 고정 단계를 요구한다(`ImportedAssets` — 이 파일과 Scenario 의 맨 아래).
# 라벨로 거른 실행(`-L hostgpu`)도 ctest 가 고정 단계를 함께 넣는다.
set(swPreviousImportTest "")
foreach(swImportKind IN ITEMS heightfields textures models)
	add_test(NAME Import.${swImportKind}
		COMMAND $<TARGET_FILE:App> --import-${swImportKind} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Import.${swImportKind}.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(Import.${swImportKind} PROPERTIES LABELS import TIMEOUT 3600 RUN_SERIAL TRUE FIXTURES_SETUP ImportedAssets DEPENDS "${swPreviousImportTest}")
	set(swPreviousImportTest Import.${swImportKind})
endforeach()

# 단위 테스트 — 헤드리스(창 · GPU 없음)라 nogpu. 순서는 매번 섞이고 씨앗을 찍는다. JSON 보고(판 번호 `TestRunner::kReportSchemaVersion` · 케이스 이름 · 파일 · 줄 · 결과 · 시간 · 실패 위치)는
# 항목마다 빌드 폴더의 고정 경로 `Testing/Reports/<항목>.json` 에 통과한 실행도 남는다 — 실패 추적과 케이스 시간 추세(샤드 균형을 정할 자료)를 남긴다.
# 몫(SW_UNIT_TEST_SHARDS)이 1 보다 크면 케이스를 이름 해시로 나눠 몫마다 프로세스 하나(`UnitTests.<i>`)가 돈다 — `ctest -j` 가 나란히 돌린다.
# 몫마다 엔진 기동(약 1 초)이 붙으므로 케이스 시간이 기동보다 커질 때 올린다. 지금(케이스 수십 개 · 합계 수십 ms)은 1 이다.
set(SW_UNIT_TEST_SHARDS 1 CACHE STRING "단위 테스트를 나눠 돌릴 프로세스 수(App --test --test-shard=i/n)")
if(SW_UNIT_TEST_SHARDS LESS_EQUAL 1)
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Reports")
	add_test(NAME UnitTests
		COMMAND $<TARGET_FILE:App> --test -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.log
			--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.json
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(UnitTests PROPERTIES LABELS nogpu TIMEOUT 600)

	# 작업 폴더에 기대지 않는다(R5) — 루트는 실행 파일 위치로 정하고(`ProcessRoots`), 상대 경로 인자는 기동 때의 작업 폴더로 한 번 절대화한다.
	# 그래서 `Bin` 이 아닌 폴더에서 상대 `-log-file` 로 띄워도 같은 테스트가 돈다(예전에는 작업 폴더에서 위로 `Content` 를 찾았다).
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Logs")
	add_test(NAME UnitTests.OtherWorkingFolder
		COMMAND $<TARGET_FILE:App> --test -unattended --test-filter=FileSystemTest.*,AbsolutePathTest.* -log-file=Logs/UnitTests.OtherWorkingFolder.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing")
	set_tests_properties(UnitTests.OtherWorkingFolder PROPERTIES LABELS nogpu TIMEOUT 300)

	# 작업 폴더를 바꿔도 같은 자리를 가리키는 상대 경로 인자의 산출물은 같은 자리에 생기고, 빈 작업 폴더에는 아무것도 생기지 않는다(R5 — 스크립트 머리말).
	add_test(NAME CommandLine.WorkingFolderPair
		COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DBIN_DIR=${CMAKE_BINARY_DIR}/Bin" "-DOTHER_DIR=${CMAKE_BINARY_DIR}/Testing/WorkingFolder/Empty"
			"-DOUT_DIR=${CMAKE_BINARY_DIR}/Testing/WorkingFolder/Out" "-DFILTER=FileSystemTest.*,AbsolutePathTest.*"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/CommandLine/RunWorkingFolderPair.cmake")
	set_tests_properties(CommandLine.WorkingFolderPair PROPERTIES LABELS nogpu TIMEOUT 300)

	# 틀린 명령줄은 기동을 멈춘다(값을 0 · false 로 바꿔 계속하던 폴백을 없앴다) — 모르는 인자 · 정수 아닌 정수 · 빈 경로.
	# Windows 의 드라이브 상대(`C:foo`) · 루트 상대(`\foo`) 경로는 절대도 상대도 아니라 디스크 경로 인자가 거절한다(`AbsolutePath::tryResolve`). POSIX 에서 `\foo` 는 이름일 뿐이다.
	set(listBadArgument "--no-such-argument" "-W=abc" "-log-file=")
	if(WIN32)
		list(APPEND listBadArgument "--test-report=C:foo.json" "--test-report=\\foo.json")
	endif()
	foreach(swBadArgument IN LISTS listBadArgument)
		string(MAKE_C_IDENTIFIER "${swBadArgument}" swBadArgumentName)
		add_test(NAME CommandLine.Rejects${swBadArgumentName}
			COMMAND $<TARGET_FILE:App> --test -unattended ${swBadArgument}
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(CommandLine.Rejects${swBadArgumentName} PROPERTIES LABELS nogpu TIMEOUT 120 WILL_FAIL TRUE)
	endforeach()
else()
	math(EXPR swLastShard "${SW_UNIT_TEST_SHARDS} - 1")
	foreach(shardIndex RANGE ${swLastShard})
		add_test(NAME UnitTests.${shardIndex}
			COMMAND $<TARGET_FILE:App> --test -unattended --test-shard=${shardIndex}/${SW_UNIT_TEST_SHARDS}
				-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${shardIndex}.log
				--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.${shardIndex}.json
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${shardIndex} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

# 키트 · 게임 단위 테스트 — 키트 `Tests/` 의 케이스는 그 키트를 싣는 게임으로 돌 때만 등록된다(활성 게임으로 도는 `UnitTests` 에는 없다).
# 함께 짓는 게임마다 그 게임으로 떠서 그 게임이 싣는 키트 · 게임 폴더의 케이스만 돈다(`--test-source`). 테스트 폴더가 하나도 없는 게임은 항목이 없다(빈 실행은 실패다).
get_property(swActiveGameTarget GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
get_property(listCoBuiltGame GLOBAL PROPERTY SW_GAME_PACKS)
if(swActiveGameTarget)
	foreach(game IN LISTS listCoBuiltGame)
		get_property(listGameDependency GLOBAL PROPERTY SW_GAME_PACK_${game}_DEPENDENCIES)
		set(listTestFolder "")
		# 공유 키트의 서버 짝(`GF_Server_<X>`)도 그 게임으로 뜰 때 올라온다(`sw_collectUnusedKits`) — 테스트 폴더를 함께 넘긴다.
		set(listGameKit ${listGameDependency})
		foreach(dependency IN LISTS listGameDependency)
			if(dependency MATCHES "^GF_(.+)$" AND TARGET GF_Server_${CMAKE_MATCH_1})
				list(APPEND listGameKit GF_Server_${CMAKE_MATCH_1})
			endif()
		endforeach()
		# 게임이 켠 모듈(서버 저장 드라이버 — `_listModuleOverride`)도 그 게임으로 뜰 때 올라온다.
		get_property(listGameOverride GLOBAL PROPERTY SW_GAME_PACK_${game}_ENABLED_OVERRIDES)
		list(APPEND listGameKit ${listGameOverride})
		list(REMOVE_DUPLICATES listGameKit)
		foreach(dependency IN LISTS listGameKit)
			get_property(dependencyKind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
			if(NOT dependencyKind STREQUAL "Kit" OR NOT TARGET ${dependency})
				continue()
			endif()
			get_target_property(kitFolder ${dependency} SOURCE_DIR)
			file(GLOB_RECURSE listKitTest "${kitFolder}/*/Tests/*.cpp" "${kitFolder}/Tests/*.cpp")
			list(FILTER listKitTest EXCLUDE REGEX "/Editor/") # 키트 폴더 아래의 에디터 확장 케이스는 `editor.unitTests` 가 돌린다 — 이 실행에는 등록되지 않는다
			if(listKitTest)
				file(RELATIVE_PATH kitRelative "${CMAKE_SOURCE_DIR}" "${kitFolder}")
				list(APPEND listTestFolder "${kitRelative}/")
			endif()
		endforeach()
		file(GLOB_RECURSE listGameTest "${CMAKE_SOURCE_DIR}/Source/Games/${game}/*/Tests/*.cpp" "${CMAKE_SOURCE_DIR}/Source/Games/${game}/Tests/*.cpp")
		if(listGameTest)
			list(APPEND listTestFolder "Source/Games/${game}/")
		endif()
		if(NOT listTestFolder)
			continue()
		endif()
		list(JOIN listTestFolder "," swTestSource)
		add_test(NAME UnitTests.${game}
			COMMAND $<TARGET_FILE:App> --test -unattended -game=${game} --test-source=${swTestSource} -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${game}.log
				--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.${game}.json
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${game} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

# 편집기 자체 테스트(`SW_EDITOR_SELF_TEST`)와 편집기 단위 테스트는 따로 된 항목이 없다 — 시나리오 단계 `<EditorSelfTest>`(`engine/automation/editor/selftests.scenario.xml`)로
# `Scenario.editor` 체인 안에서 돌아 백엔드 넷과 로그 게이트를 함께 받는다(예전 EditorTests 는 기본 백엔드에서 케이스 성패만 봐 로그로만 드러나는 결함을 놓쳤다).
if(TARGET EditorModule)

	# 헤드리스 도구 명령 하나 — 현지화 표가 코드 · 데이터와 맞는지 보고, 도구 실행의 기동 · 종료 길을 지난다(모듈이 내려갈 때 서비스를 찾다 죽던 결함이
	# 모든 도구 명령에 있었는데 위 항목들은 도구 명령을 돌리지 않아 숨었다). 임포트 검사는 산출물을 커밋하지 않아 새 체크아웃에서 늘 지므로 이것을 고른다.
	add_test(NAME ToolCheckText
		COMMAND $<TARGET_FILE:App> --check-text -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckText.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckText PROPERTIES LABELS nogpu TIMEOUT 300)

	# 에셋 참조 — 에셋은 경로 하나로 가리키므로(.meta · GUID 없음) 글 에셋 · 이 파일의 경로 칸이 모두 있는 것을 가리키는지, 리플렉션 `AssetPath` 프로퍼티가 모두
	# 칸 목록(`Config/Dev/AssetReferenceFields.json`)에 있는지 본다. 커밋 전 검사(`Scripts/lint/AssetReferences.py`)와 같은 목록이다. 임포트 산출물을 보므로 임포트 뒤에 돈다.
	add_test(NAME ToolCheckAssetRefs
		COMMAND $<TARGET_FILE:App> --check-asset-refs -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckAssetRefs.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckAssetRefs PROPERTIES LABELS nogpu TIMEOUT 300)

	# 초상화 굽기 — 스튜디오 씬 → 패킷(게임 스레드)과 그리기 · 읽기(렌더 스레드 명령 `RenderThread::executeAndWait`)를 한 번 지난다.
	# 예전에는 게임 스레드가 렌더 스레드를 세우고 직접 그렸고, 그 길을 도는 테스트가 없었다. 창 · GPU 가 필요해 hostgpu 다.
	# 백엔드마다 돈다 — 기본 백엔드(DX12)만 돌 때 DX11 에서 머티리얼 PSO 가 만드는 중인 프레임을 읽어 빈 사진이 되던 결함이 숨었다.
	foreach(backend DirectX11 DirectX12 Vulkan OpenGL)
		add_test(NAME Bake.Portraits.${backend}
			COMMAND $<TARGET_FILE:App> --render-portraits=common/prefabs/quaternius/scifi/gun_pistol.prefab.xml -portrait-size=64 -portrait-dir=${CMAKE_BINARY_DIR}/Testing/Portraits/${backend}
				-gv_rhiBackend=${backend} -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Bake.Portraits.${backend}.log
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(Bake.Portraits.${backend} PROPERTIES LABELS hostgpu TIMEOUT 300)
	endforeach()
endif()

# 생성 표 동시 갱신 — 빌드 폴더 여럿이 저장소의 참조 칸 표(`Config/Dev/AssetReferenceFields.generated.json`)를 함께 고쳐도 주인 줄을 잃지 않는다(잠금 + 원자 교체).
# 표의 사본을 임시 폴더에 두고 자식 프로세스 열둘이 서로 다른 주인을 동시에 갈아 끼운다.
add_test(NAME Scripts.AssetReferenceFieldList
	COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/generate/Tests/TestAssetReferenceFieldList.py")
set_tests_properties(Scripts.AssetReferenceFieldList PROPERTIES LABELS nogpu TIMEOUT 120)

# 배포 패키지의 모드 스테이징 — 묶은 모드만 담고, 형식이 틀린 id · 묶지 않은 모드는 실패다(Scripts/package/Pack.py --mods).
add_test(NAME Scripts.PackMods
	COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/package/Tests/TestPackMods.py")
set_tests_properties(Scripts.PackMods PROPERTIES LABELS nogpu TIMEOUT 60)

# 온라인 부하 봇의 짧은 시나리오 — 같은 프로세스 서버(`--local-server=0`, OS 가 고른 포트라 나란히 돌아도 다투지 않는다)에 봇 여덟이 붙어
# 로그인 · 채팅 · 순위를 한 번씩 돈다. 오류율 · 보고서 쓰기 실패가 종료 코드다(엔진 · 모듈이 서지 못하면 3). 봇 로그는 `-log-file` 로 같은 폴더에 남는다.
if(TARGET OnlineLoadBot)
	set(swLoadBotDir "${CMAKE_SOURCE_DIR}/Source/Programs/OnlineLoadBot")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Logs")
	add_test(NAME OnlineLoadBot.localServer
		COMMAND $<TARGET_FILE:OnlineLoadBot>
			--scenario=${swLoadBotDir}/Scenarios/smoke.json
			--load=${swLoadBotDir}/Scenarios/smoke.load.xml
			--local-server=0
			--online-config=${swLoadBotDir}/LoadBot.online.json
			--light-hash
			--report=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.json
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.localServer PROPERTIES LABELS nogpu TIMEOUT 120)

	# 온라인 키트(클라이언트 · 서버 쌍)의 단위 테스트 — 그 키트를 싣는 게임이 없어 `UnitTests.<게임>` 에 들지 않는다. 봇이 키트를 모두 싣으므로 엔진 인자(대시 하나)로
	# 테스트만 돌린다(`OnlineLoadBot` 의 `runBot` 이 헤드리스 결과로 끝낸다). `Tests/` 가 있는 키트 폴더만 넘긴다(빈 실행은 실패다).
	get_property(listLoadBotKit GLOBAL PROPERTY SW_LOADBOT_KITS)
	set(listOnlineKitTestFolder "")
	foreach(swKit IN LISTS listLoadBotKit)
		get_target_property(swKitFolder ${swKit} SOURCE_DIR)
		file(GLOB_RECURSE listOnlineKitTest "${swKitFolder}/*/Tests/*.cpp" "${swKitFolder}/Tests/*.cpp")
		list(FILTER listOnlineKitTest EXCLUDE REGEX "/Editor/")
		if(listOnlineKitTest)
			file(RELATIVE_PATH swKitRelative "${CMAKE_SOURCE_DIR}" "${swKitFolder}")
			list(APPEND listOnlineKitTestFolder "${swKitRelative}/")
		endif()
	endforeach()
	if(listOnlineKitTestFolder)
		list(JOIN listOnlineKitTestFolder "," swOnlineKitTestSource)
		add_test(NAME UnitTests.OnlineKits
			COMMAND $<TARGET_FILE:OnlineLoadBot> -test -unattended -test-source=${swOnlineKitTestSource}
				-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.OnlineKits.log
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.OnlineKits PROPERTIES LABELS nogpu TIMEOUT 300)
	endif()
endif()

# 셰이더 캐시 — DDC 와 쿠킹 스테이징을 모두 지운 뒤 굽고 판정한다(`Shader/RunShaderCacheRebuild.cmake`). DDC 는 빌드 폴더가 함께 쓰는 자리라 다른 테스트와 나란히 돌지 않는다.
add_test(NAME ShaderCache.RebuildFromEmpty
	COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DDDC_DIR=${CMAKE_BINARY_DIR}/Bin/Saved/ShaderCompileCache"
		"-DCOOKED_DIR=${CMAKE_BINARY_DIR}/Testing/ShaderCache/Cooked" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Shader/RunShaderCacheRebuild.cmake"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(ShaderCache.RebuildFromEmpty PROPERTIES LABELS nogpu TIMEOUT 3600 RUN_SERIAL TRUE)

# 리플렉션 파서 — Engine 을 링크하지 못해 단위 테스트 틀에 들어가지 못한다. 사례 헤더 하나마다 실제 실행 파일을 돌려 산출물 · 오류를 대조한다(`ReflectionParser/RunParserFixture.cmake`, 왜 이 모양인지 거기 적었다).
if(TARGET ReflectionParser)
	file(GLOB listParserFixture CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/Fixtures/*.h")
	foreach(swParserFixture IN LISTS listParserFixture)
		get_filename_component(swParserFixtureName "${swParserFixture}" NAME_WE)
		add_test(NAME ReflectionParser.${swParserFixtureName}
			COMMAND ${CMAKE_COMMAND}
				"-DPARSER=$<TARGET_FILE:ReflectionParser>"
				"-DFIXTURE=${swParserFixture}"
				"-DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/ReflectionParser/${swParserFixtureName}"
				"-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
				"-DGENERATED_DIR=${CMAKE_BINARY_DIR}/generated"
				"-DBUILTINS=${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}"
				"-DANNOTATION_META=${CMAKE_SOURCE_DIR}/${SW_FILE_ANNOTATION_META}"
				"-DTEMPLATES=${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}"
				"-DCONFIG=${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
				"-DTYPES_LIST_NAME=${SW_FILE_REFLECTED_TYPES_LIST}"
				"-DASSET_FIELDS_LIST_NAME=${SW_FILE_ASSET_REFERENCE_FIELDS_LIST}"
				"-DGEN_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_CPP}"
				"-DGEN_META_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_META_CPP}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/RunParserFixture.cmake")
		set_tests_properties(ReflectionParser.${swParserFixtureName} PROPERTIES LABELS nogpu TIMEOUT 120)
	endforeach()
	# 도구 입력 — 같은 입력 두 번이면 같은 바이트 · 시각 그대로, 설정 · 템플릿 · 철자 표가 깨지면 0 이 아닌 종료(`ReflectionParser/RunParserToolInputs.cmake`).
	foreach(swParserToolMode IN ITEMS determinism broken-config broken-templates broken-spelling)
		add_test(NAME ReflectionParser.ToolInputs.${swParserToolMode}
			COMMAND ${CMAKE_COMMAND}
				"-DMODE=${swParserToolMode}"
				"-DPARSER=$<TARGET_FILE:ReflectionParser>"
				"-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/Fixtures/ValidTypes.h"
				"-DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/ReflectionParser/ToolInputs.${swParserToolMode}"
				"-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
				"-DGENERATED_DIR=${CMAKE_BINARY_DIR}/generated"
				"-DBUILTINS=${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}"
				"-DANNOTATION_META=${CMAKE_SOURCE_DIR}/${SW_FILE_ANNOTATION_META}"
				"-DTEMPLATES=${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}"
				"-DCONFIG=${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
				"-DGEN_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_CPP}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/RunParserToolInputs.cmake")
		set_tests_properties(ReflectionParser.ToolInputs.${swParserToolMode} PROPERTIES LABELS nogpu TIMEOUT 120)
	endforeach()
	# 틀린 명령줄은 파서를 멈춘다 — 모르는 인자 · 필수 인자 빠짐. 입력을 읽기 전이라 사례 헤더가 필요 없다.
	add_test(NAME ReflectionParser.RejectsUnknownArgument COMMAND $<TARGET_FILE:ReflectionParser> --no-such-argument)
	add_test(NAME ReflectionParser.RejectsMissingOutput COMMAND $<TARGET_FILE:ReflectionParser> --input nothing.h)
	set_tests_properties(ReflectionParser.RejectsUnknownArgument ReflectionParser.RejectsMissingOutput PROPERTIES LABELS nogpu TIMEOUT 60 WILL_FAIL TRUE)
endif()

# 임포트 밖의 모든 테스트가 임포트한 산출물을 요구한다(맨 위 `Import.*`).
get_property(swAllTest DIRECTORY PROPERTY TESTS)
list(FILTER swAllTest EXCLUDE REGEX "^Import\.")
set_tests_properties(${swAllTest} PROPERTIES FIXTURES_REQUIRED ImportedAssets)

add_subdirectory(Scenario)
