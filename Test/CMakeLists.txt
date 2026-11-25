# ==============================================================================
# @file Test/CMakeLists.txt
# @brief CTest 등록만 한다 — 테스트 코드는 여기 없다. 단위 테스트는 기능 폴더 옆 `Tests/`(`SW_TEST_CASE`)에 있고 `App --test` 한 번이 모두 돈다.
#        시나리오는 `Content/**/automation/*.scenario.xml` 이고 `Test/Scenario/` 가 하나씩 올린다. 쓰는 법은 Test/README.md(단위 테스트) · Test/Scenario/README.md(시나리오).
# ==============================================================================

if(NOT TARGET App)
	return()   # 전용 서버 타깃은 App 을 짓지 않는다
endif()

# 에셋 임포트 — 테스트는 임포트한 산출물(`.mesh` · `.dds` · 높이장)을 읽는다. 형식 · 임포터 버전을 올리면 스탬프가 낡아 다시 임포트해야 하는데,
# 손으로 돌리지 않으면 테스트가 낡은 산출물을 거절하며 돈다(2026-11-02 — `.mesh` 5 판에서 모델이 모두 빠진 채 시나리오가 돌았다). 그래서 ctest 가 먼저
# 낡은 것만 임포트한다(스탬프가 맞으면 곧바로 끝난다). 모든 테스트가 이 고정 단계를 요구한다(`ImportedAssets` — 이 파일과 Scenario 의 맨 아래).
# 라벨로 거른 실행(`-L hostgpu`)도 ctest 가 고정 단계를 함께 넣는다.
# 세 종류를 한 프로세스가 차례로 돈다 — 무변경 임포트는 일보다 기동 · 종료(모듈 전부 올리고 내리기)가 크다. 어느 종류가 졌는지는 로그의 종류별 요약(`AssetImportEntry.SourcesImportedProblems`)이 말한다.
add_test(NAME Import.assets
	COMMAND $<TARGET_FILE:App> --import-textures --import-models --import-heightfields -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Import.assets.log
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(Import.assets PROPERTIES LABELS import TIMEOUT 7200 RUN_SERIAL TRUE FIXTURES_SETUP ImportedAssets)

# 단위 테스트 — 헤드리스(창 · GPU 없음)라 nogpu. 순서는 매번 섞이고 씨앗을 찍는다. JSON 보고(판 번호 `TestRunner::kReportSchemaVersion` · 케이스 이름 · 파일 · 줄 · 결과 · 시간 · 실패 위치)는
# 항목마다 빌드 폴더의 고정 경로 `Testing/Reports/<항목>.json` 에 통과한 실행도 남는다 — 실패 추적과 케이스 시간 추세(샤드 균형을 정할 자료)를 남긴다.
# 몫(SW_UNIT_TEST_SHARDS)이 1 보다 크면 케이스를 이름 해시로 나눠 몫마다 프로세스 하나(`UnitTests.<i>`)가 돈다 — `ctest -j` 가 나란히 돌린다.
# 몫마다 엔진 기동(약 1 초)이 붙는다. 케이스가 5 천 개를 넘어 한 프로세스 합계가 ASan 에서 약 4 분(2026-11-21, 247 초)이 되자, 묶음 검증이 CPU 일을 겹쳐 돌 때
# 600 초 시한을 넘겼다 — 시한을 늘리지 않고 몫으로 나눠 `ctest -j` 가 나란히 돌게 한다(GoogleTest 샤딩). 값은 프리셋(base-vcpkg)이 준다 — 캐시 기본값만 바꾸면 이미 있는 빌드 폴더는 옛 1 을 쥔다.
set(SW_UNIT_TEST_SHARDS 4 CACHE STRING "단위 테스트를 나눠 돌릴 프로세스 수(App --test --test-shard=i/n)")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Reports")
if(SW_UNIT_TEST_SHARDS LESS_EQUAL 1)
	add_test(NAME UnitTests
		COMMAND $<TARGET_FILE:App> --test -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.log
			--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.json
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(UnitTests PROPERTIES LABELS nogpu TIMEOUT 600)
else()
	math(EXPR last_shard "${SW_UNIT_TEST_SHARDS} - 1")
	foreach(shard_index RANGE ${last_shard})
		add_test(NAME UnitTests.${shard_index}
			COMMAND $<TARGET_FILE:App> --test -unattended --test-shard=${shard_index}/${SW_UNIT_TEST_SHARDS}
				-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${shard_index}.log
				--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.${shard_index}.json
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${shard_index} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

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
set(list_bad_argument "--no-such-argument" "-W=abc" "-log-file=")
if(WIN32)
	list(APPEND list_bad_argument "--test-report=C:foo.json" "--test-report=\\foo.json")
endif()
foreach(bad_argument IN LISTS list_bad_argument)
	string(MAKE_C_IDENTIFIER "${bad_argument}" bad_argument_name)
	add_test(NAME CommandLine.Rejects${bad_argument_name}
		COMMAND $<TARGET_FILE:App> --test -unattended ${bad_argument}
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(CommandLine.Rejects${bad_argument_name} PROPERTIES LABELS nogpu TIMEOUT 120 WILL_FAIL TRUE)
endforeach()

# 키트 · 게임 단위 테스트 — 키트 `Tests/` 의 케이스는 그 키트를 싣는 게임으로 돌 때만 등록된다(활성 게임으로 도는 `UnitTests` 에는 없다).
# 함께 짓는 게임마다 그 게임으로 떠서 그 게임이 싣는 키트 · 게임 폴더의 케이스만 돈다(`--test-source`). 테스트 폴더가 하나도 없는 게임은 항목이 없다(빈 실행은 실패다).
get_property(active_game_target GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET)
get_property(list_co_built_game GLOBAL PROPERTY SW_GAME_PACKS)
if(active_game_target)
	foreach(game IN LISTS list_co_built_game)
		get_property(list_game_dependency GLOBAL PROPERTY SW_GAME_PACK_${game}_DEPENDENCIES)
		set(list_test_folder "")
		# 공유 키트의 서버 짝(`GF_Server_<X>`)도 그 게임으로 뜰 때 올라온다(`sw_collect_unused_kits`) — 테스트 폴더를 함께 넘긴다.
		set(list_game_kit ${list_game_dependency})
		foreach(dependency IN LISTS list_game_dependency)
			if(dependency MATCHES "^GF_(.+)$" AND TARGET GF_Server_${CMAKE_MATCH_1})
				list(APPEND list_game_kit GF_Server_${CMAKE_MATCH_1})
			endif()
		endforeach()
		# 게임이 켠 모듈(서버 저장 드라이버 — `_listModuleOverride`)도 그 게임으로 뜰 때 올라온다.
		get_property(list_game_override GLOBAL PROPERTY SW_GAME_PACK_${game}_ENABLED_OVERRIDES)
		list(APPEND list_game_kit ${list_game_override})
		list(REMOVE_DUPLICATES list_game_kit)
		foreach(dependency IN LISTS list_game_kit)
			get_property(dependency_kind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
			if(NOT dependency_kind STREQUAL "Kit" OR NOT TARGET ${dependency})
				continue()
			endif()
			get_target_property(kit_folder ${dependency} SOURCE_DIR)
			sw_glob_tests_under(list_kit_test "${kit_folder}")
			list(FILTER list_kit_test EXCLUDE REGEX "/Editor/") # 키트 폴더 아래의 에디터 확장 케이스는 `UnitTests.Editor` 가 돌린다 — 이 실행에는 등록되지 않는다
			if(list_kit_test)
				file(RELATIVE_PATH kit_relative "${CMAKE_SOURCE_DIR}" "${kit_folder}")
				list(APPEND list_test_folder "${kit_relative}/")
			endif()
		endforeach()
		sw_glob_tests_under(list_game_test "${CMAKE_SOURCE_DIR}/Source/Games/${game}")
		if(list_game_test)
			list(APPEND list_test_folder "Source/Games/${game}/")
		endif()
		if(NOT list_test_folder)
			continue()
		endif()
		list(JOIN list_test_folder "," test_source)
		add_test(NAME UnitTests.${game}
			COMMAND $<TARGET_FILE:App> --test -unattended -game=${game} --test-source=${test_source} -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.${game}.log
				--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.${game}.json
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.${game} PROPERTIES LABELS nogpu TIMEOUT 600)
	endforeach()
endif()

# 편집기 자체 테스트(`SW_EDITOR_SELF_TEST`)는 따로 된 항목이 없다 — 시나리오 단계 `<EditorSelfTest>`(`engine/automation/editor/selftests.scenario.xml`)로
# `Scenario.editor` 체인 안에서 돌아 백엔드 넷과 로그 게이트를 함께 받는다(예전 EditorTests 는 기본 백엔드에서 케이스 성패만 봐 로그로만 드러나는 결함을 놓쳤다).
if(TARGET EditorModule)
	# 에디터 단위 테스트 — 창 · GPU 없이 에디터 인스턴스를 세워 `/Editor/` 소스의 케이스를 한 번 돈다(예전에는 편집기 체인이 백엔드 넷에서 되풀이했다).
	# 에디터 화면이 필요한 케이스(태그 `editorApp`)만 편집기 체인의 자체 테스트 `editor.unitTests` 에 남는다.
	add_test(NAME UnitTests.Editor
		COMMAND $<TARGET_FILE:App> --test-editor -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.Editor.log
			--test-report=${CMAKE_BINARY_DIR}/Testing/Reports/UnitTests.Editor.json
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(UnitTests.Editor PROPERTIES LABELS nogpu TIMEOUT 600)

	# 헤드리스 도구 명령 하나 — 현지화 표가 코드 · 데이터와 맞는지 보고, 도구 실행의 기동 · 종료 길을 지난다(모듈이 내려갈 때 서비스를 찾다 죽던 결함이
	# 모든 도구 명령에 있었는데 위 항목들은 도구 명령을 돌리지 않아 숨었다). 임포트 검사는 산출물을 커밋하지 않아 새 체크아웃에서 늘 지므로 이것을 고른다.
	add_test(NAME ToolCheckText
		COMMAND $<TARGET_FILE:App> --check-text -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckText.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckText PROPERTIES LABELS nogpu TIMEOUT 300)

	# 에셋 참조 — 에셋은 경로 하나로 가리키므로(.meta · GUID 없음) 글 에셋 · 이 파일의 경로 칸이 모두 있는 것을 가리키는지, 리플렉션 `AssetPath` 프로퍼티가 모두
	# 칸 목록(`Config/Dev/AssetReferenceFields.json`)에 있는지 본다. 커밋 전 검사(`Scripts/lint/asset_references.py`)와 같은 목록이다. 임포트 산출물을 보므로 임포트 뒤에 돈다.
	add_test(NAME ToolCheckAssetRefs
		COMMAND $<TARGET_FILE:App> --check-asset-refs -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheckAssetRefs.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheckAssetRefs PROPERTIES LABELS nogpu TIMEOUT 300)

	# 임포트 검사 — 원본과 임포트 산출물(`.dds` · `.mesh` · `.heightfield`)이 맞는지 보기만 한다(고정 단계 `Import.*` 뒤라 새 체크아웃에서도 맞아야 한다).
	# 임포트가 산출물을 쓰고 검사가 다른 판정(스탬프 · 판 번호)을 하면 여기서 갈린다. 형식 변형 검사(`--check-texture-variants`)는 넣지 않는다 — 모든 텍스처를
	# ASTC · ETC2 로 먼저 구워야 해(`--cook-texture-variants`, 교차 빌드의 CookAssets 몫) 묶음마다 돌 크기가 아니다.
	add_test(NAME ToolCheck.imports
		COMMAND $<TARGET_FILE:App> --check-textures --check-models --check-heightfields -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/ToolCheck.imports.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ToolCheck.imports PROPERTIES LABELS nogpu TIMEOUT 300)

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

# 헤드리스 도구 명령 — 사람이 손으로 쓰는 길이라 자동으로 돌지 않으면 깨져도 모른다. 작은 입력으로 한 번씩 돌고 산출물을 본다.
# 리플렉션 문서: 등록된 타입으로 Markdown 을 쓴다(한 장도 못 쓰면 종료 코드가 실패다).
add_test(NAME Tool.WriteReflectionDocs
	COMMAND $<TARGET_FILE:App> --write-reflection-docs=${CMAKE_BINARY_DIR}/Testing/ReflectionDocs -unattended -log-file=${CMAKE_BINARY_DIR}/Testing/Logs/Tool.WriteReflectionDocs.log
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(Tool.WriteReflectionDocs PROPERTIES LABELS nogpu TIMEOUT 300)
# 립싱크 임포트: 트랙을 소스 트리가 아니라 `Testing/` 아래 출력 폴더(`-lipsync-dir`)에 쓴다(`Tool/RunImportLipSync.cmake`).
add_test(NAME Tool.ImportLipSync
	COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DOUT_DIR=${CMAKE_BINARY_DIR}/Testing/LipSync" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Tool/RunImportLipSync.cmake"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(Tool.ImportLipSync PROPERTIES LABELS nogpu TIMEOUT 300)
# 리타깃 베이크: 커밋된 프로필로 임포트한 기사 클립 하나를 굽는다(`Tool/RunBakeRetarget.cmake`).
add_test(NAME Tool.BakeRetarget
	COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DCONTENT_DIR=${CMAKE_SOURCE_DIR}/Content" "-DOUT_DIR=${CMAKE_BINARY_DIR}/Testing/Retarget"
		"-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs" -P "${CMAKE_CURRENT_SOURCE_DIR}/Tool/RunBakeRetarget.cmake"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(Tool.BakeRetarget PROPERTIES LABELS nogpu TIMEOUT 300)
# 패치 만들기: 기준 빌드 → 새 빌드 → 서명한 패치 → 런타임 적용 결과 비교(`Pack/RunMakePatch.cmake`, 언리얼 `-generatepatch` 자리).
add_test(NAME Pack.MakePatch
	COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/MakePatch" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Pack/RunMakePatch.cmake"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set_tests_properties(Pack.MakePatch PROPERTIES LABELS nogpu TIMEOUT 300)

# 스크립트 자체 테스트 — `Scripts/<폴더>/tests/test_*.py` 를 모두 돈다. 손으로 한 줄씩 등록하면 빠뜨린다(test_cook_code_fingerprint ·
# test_shipping_host_defaults 가 빠져 있었다). 무엇을 보는지는 각 파일 머리말에 있다.
# 이름은 `Scripts.<줄기의 낙타 이름>`(test_move_asset.py → Scripts.MoveAsset). cmake 를 부르는 테스트(vcpkg 설치 보호)는 SW_CMAKE_COMMAND 로 받는다.
# 도구 정책 훅의 판정 테스트(`.claude/hooks/test_*.py`)도 함께 돈다 — 손으로만 돌면 규칙 데이터가 바뀌어도 모른다.
file(GLOB list_script_test CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Scripts/*/tests/test_*.py" "${CMAKE_SOURCE_DIR}/.claude/hooks/test_*.py")
foreach(script_test IN LISTS list_script_test)
	get_filename_component(script_test_stem "${script_test}" NAME_WE)
	string(REGEX REPLACE "^test_" "" script_test_stem "${script_test_stem}")
	string(REPLACE "_" ";" list_script_test_word "${script_test_stem}")
	set(script_test_name "")
	foreach(script_test_word IN LISTS list_script_test_word)
		string(SUBSTRING "${script_test_word}" 0 1 script_test_head)
		string(SUBSTRING "${script_test_word}" 1 -1 script_test_tail)
		string(TOUPPER "${script_test_head}" script_test_head)
		string(APPEND script_test_name "${script_test_head}${script_test_tail}")
	endforeach()
	add_test(NAME Scripts.${script_test_name} COMMAND "${Python3_EXECUTABLE}" -X utf8 "${script_test}")
	set_tests_properties(Scripts.${script_test_name} PROPERTIES LABELS nogpu TIMEOUT 120 ENVIRONMENT "SW_CMAKE_COMMAND=${CMAKE_COMMAND}")
endforeach()

# 온라인 부하 봇의 짧은 시나리오 — 같은 프로세스 서버(`--local-server=0`, OS 가 고른 포트라 나란히 돌아도 다투지 않는다)에 봇 여덟이 붙어
# 로그인 · 채팅 · 순위와 원장 경로(영수증 · 구매 · 우편 · 점수 · 길드 창고 · 거래)를 한 번씩 돈다. 오류율 · 보고서 쓰기 실패 · 끝의 원장 감사 불균형(5)이 종료 코드다
# (엔진 · 모듈이 서지 못하면 3). 봇 로그는 `-log-file` 로 같은 폴더에 남는다.
if(TARGET OnlineLoadBot)
	set(load_bot_dir "${CMAKE_SOURCE_DIR}/Source/Programs/OnlineLoadBot")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Logs")
	add_test(NAME OnlineLoadBot.localServer
		COMMAND $<TARGET_FILE:OnlineLoadBot>
			--scenario=${load_bot_dir}/Scenarios/smoke.json
			--load=${load_bot_dir}/Scenarios/smoke.load.xml
			--local-server=0
			--online-config=${load_bot_dir}/LoadBot.online.json
			--light-hash
			--report=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.json
			-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/OnlineLoadBot.localServer.log
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.localServer PROPERTIES LABELS nogpu TIMEOUT 120)

	# 대상 인자의 짝 규칙 — 전용 서버에 붙는 길(`--server` · `--tls` · `--game`)과 외부 저장소(`--store-config` — PostgreSQL · RESP)는 띄울 서버 · 서비스가 있어야 해
	# 여기서 돌지 않는다(`Net.*` 항목 몫 — Test/Net). 대신 그 인자를 읽고 맞지 않는 짝을 거절하는지 본다(종료 코드가 아니라 거절 글로 판정한다).
	set(load_bot_base_argument --scenario=${load_bot_dir}/Scenarios/smoke.json --load=${load_bot_dir}/Scenarios/smoke.load.xml -unattended)
	add_test(NAME OnlineLoadBot.RejectsTwoTargets
		COMMAND $<TARGET_FILE:OnlineLoadBot> ${load_bot_base_argument} --server=127.0.0.1:1 --local-server=0
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.RejectsTwoTargets PROPERTIES PASS_REGULAR_EXPRESSION "exactly one of --server")
	add_test(NAME OnlineLoadBot.RejectsTlsWithLocalServer
		COMMAND $<TARGET_FILE:OnlineLoadBot> ${load_bot_base_argument} --local-server=0 --online-config=${load_bot_dir}/LoadBot.online.json --tls=${load_bot_dir}/LoadBot.online.json
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.RejectsTlsWithLocalServer PROPERTIES PASS_REGULAR_EXPRESSION "--tls goes with --server")
	add_test(NAME OnlineLoadBot.RejectsStoreConfigWithServer
		COMMAND $<TARGET_FILE:OnlineLoadBot> ${load_bot_base_argument} --server=127.0.0.1:1 --store-config=${load_bot_dir}/LoadBot.online.json
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(OnlineLoadBot.RejectsStoreConfigWithServer PROPERTIES PASS_REGULAR_EXPRESSION "--store-config goes with --local-server")
	set_tests_properties(OnlineLoadBot.RejectsTwoTargets OnlineLoadBot.RejectsTlsWithLocalServer OnlineLoadBot.RejectsStoreConfigWithServer PROPERTIES LABELS nogpu TIMEOUT 60)

	# 온라인 키트(클라이언트 · 서버 쌍)의 단위 테스트 — 그 키트를 싣는 게임이 없어 `UnitTests.<게임>` 에 들지 않는다. 봇이 키트를 모두 싣으므로 엔진 인자(대시 하나)로
	# 테스트만 돌린다(`OnlineLoadBot` 의 `runBot` 이 헤드리스 결과로 끝낸다). `Tests/` 가 있는 키트 폴더만 넘긴다(빈 실행은 실패다).
	get_property(list_load_bot_kit GLOBAL PROPERTY SW_LOADBOT_KITS)
	set(list_online_kit_test_folder "")
	foreach(kit IN LISTS list_load_bot_kit)
		get_target_property(kit_folder ${kit} SOURCE_DIR)
		sw_glob_tests_under(list_online_kit_test "${kit_folder}")
		list(FILTER list_online_kit_test EXCLUDE REGEX "/Editor/")
		if(list_online_kit_test)
			file(RELATIVE_PATH kit_relative "${CMAKE_SOURCE_DIR}" "${kit_folder}")
			list(APPEND list_online_kit_test_folder "${kit_relative}/")
		endif()
	endforeach()
	list(APPEND list_online_kit_test_folder "Source/Programs/OnlineLoadBot/") # 봇 자체의 테스트(보내기 방식 · 시나리오 읽기)
	if(list_online_kit_test_folder)
		list(JOIN list_online_kit_test_folder "," online_kit_test_source)
		add_test(NAME UnitTests.OnlineKits
			COMMAND $<TARGET_FILE:OnlineLoadBot> -test -unattended -test-source=${online_kit_test_source}
				-log-file=${CMAKE_BINARY_DIR}/Testing/Logs/UnitTests.OnlineKits.log
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
		set_tests_properties(UnitTests.OnlineKits PROPERTIES LABELS nogpu TIMEOUT 300)
	endif()
endif()

# 셰이더 캐시 — 제 DDC(`-ddc-dir`)와 쿠킹 스테이징을 지운 뒤 굽고 판정한다(`Shader/RunShaderCacheRebuild.cmake`). 빌드 폴더가 함께 쓰는 DDC 는 건드리지 않는다 —
# 지우면 뒤에 도는 시나리오 체인이 모두 빈 캐시로 돌아 백엔드마다 실행 시한에 걸렸다.
# 묶음 검증의 CPU 구성 하나에만 둔다: 보는 것은 빈 캐시에서 다시 만드는 길(구성과 무관한 로직)이다. 그 구성은 ASan(Ninja-Release-ASan — 쿠커 단언은 SW_DO_CHECKS 로 들어 있다)이고,
# 사람이 짓는 Debug(Linux 단위 포함)에도 남긴다. Ninja-Release 에 두면 GPU 단계의 시간 판정 시나리오와 CPU 를 다툰다. 최적화 빌드의 쿠킹은 shipping-check(배포 구성 빌드의 CookAssets)가 본다.
# 두 구성에서 돌면 묶음 검증마다 Debug 503 초 + Release 361 초를 차례로 썼다(2026-11-13).
# CPU 를 다 쓰는 굽기라 시간 판정이 있는 항목(hostgpu · load)은 이것 뒤에 돈다(`SW_CPU_HEAVY_TEST` → DEPENDS, 아래 · Scenario/) — 다른 nogpu 항목과는 함께 돈다(예전 RUN_SERIAL 은 그것까지 막았다).
set(SW_CPU_HEAVY_TEST "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR SW_ENABLE_SANITIZER)
	add_test(NAME ShaderCache.RebuildFromEmpty
		COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DDDC_DIR=${CMAKE_BINARY_DIR}/Testing/ShaderCache/DDC"
			"-DCOOKED_DIR=${CMAKE_BINARY_DIR}/Testing/ShaderCache/Cooked" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Shader/RunShaderCacheRebuild.cmake"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ShaderCache.RebuildFromEmpty PROPERTIES LABELS nogpu TIMEOUT 3600)
	set(SW_CPU_HEAVY_TEST ShaderCache.RebuildFromEmpty)
	# 요청 기록만 컴파일하는 데우기(`Shader/RunShaderWarm.cmake`) — 같은 요청의 둘째 컴파일이 DDC 에 맞는지 본다. 셰이더 하나라 몇 초다. 묶음 검증 cpu 단계의 shader warm 이 이 길이다.
	add_test(NAME ShaderCache.WarmFromRequestLog
		COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DDDC_DIR=${CMAKE_BINARY_DIR}/Testing/ShaderWarm/DDC"
			"-DREQUEST_DIR=${CMAKE_BINARY_DIR}/Testing/ShaderWarm/Requests" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Shader/RunShaderWarm.cmake"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(ShaderCache.WarmFromRequestLog PROPERTIES LABELS nogpu TIMEOUT 300)
endif()

# 쿠킹본 실행 — 개발 구성의 `--cook-all` → `-cooked-content` 기동 → 셀 스트리밍 시나리오(`Pack/RunCookedContent.cmake`). 어떤 테스트도 이 길을 돌지 않아 낡았다
# (상대 `--cooked-dir` 의 스테이징이 팩에서 빠졌다 — 감사 A-13). 배포 쿠킹(shipping-check)은 절대 경로 · 배포 구성이라 이 길을 지나지 않는다.
# Release 하나 · 게임 하나(Playground) · 백엔드 하나(DX12 — Windows)다: 셰이더 굽기가 길어 구성 · 백엔드를 늘릴 일이 아니다. 시나리오가 그림을 그려 hostgpu 다.
# 굽기는 CPU 를 다 써서 Release 의 CPU 무거운 항목(`SW_CPU_HEAVY_TEST`)이다 — 시간 판정이 있는 시나리오는 이것 뒤에 돈다.
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT SW_ENABLE_SANITIZER AND WIN32)
	add_test(NAME Pack.CookedContent
		COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:App> "-DBIN_DIR=${CMAKE_BINARY_DIR}/Bin" "-DLOG_DIR=${CMAKE_BINARY_DIR}/Testing/Logs" -DSKIP_CODE=${SW_EXIT_CODE_Skipped}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Pack/RunCookedContent.cmake"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
	set_tests_properties(Pack.CookedContent PROPERTIES LABELS hostgpu TIMEOUT 3600 SKIP_RETURN_CODE ${SW_EXIT_CODE_Skipped})
	set(SW_CPU_HEAVY_TEST Pack.CookedContent)
endif()

# 리플렉션 파서 — Engine 을 링크하지 못해 단위 테스트 틀에 들어가지 못한다. 사례 헤더 하나마다 실제 실행 파일을 돌려 산출물 · 오류를 대조한다(`ReflectionParser/RunParserFixture.cmake`, 왜 이 모양인지 거기 적었다).
if(TARGET ReflectionParser)
	# 빌드와 같은 대상 인자(`--target-arg`)를 넘긴다 — 파서는 대상 매크로를 추측하지 않는다(교차 빌드). 목록 구분자 ';' 는 -D 에서 갈라지므로 '|' 로 잇는다.
	sw_collect_reflection_target_args(list_parser_test_target_arg)
	if(NOT list_parser_test_target_arg)
		message(FATAL_ERROR "[ReflectionParser tests] no target macros found on sw_global_options / SW_FLAG_LIBRARIES")
	endif()
	string(JOIN "|" parser_test_target_args ${list_parser_test_target_arg})
	sw_glob_source_tree(list_parser_fixture "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/Fixtures/*.h")
	foreach(parser_fixture IN LISTS list_parser_fixture)
		get_filename_component(parser_fixture_name "${parser_fixture}" NAME_WE)
		add_test(NAME ReflectionParser.${parser_fixture_name}
			COMMAND ${CMAKE_COMMAND}
				"-DPARSER=$<TARGET_FILE:ReflectionParser>"
				"-DFIXTURE=${parser_fixture}"
				"-DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/ReflectionParser/${parser_fixture_name}"
				"-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
				"-DGENERATED_DIR=${CMAKE_BINARY_DIR}/generated"
				"-DBUILTINS=${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}"
				"-DTEMPLATES=${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}"
				"-DCONFIG=${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
				"-DTYPES_LIST_NAME=${SW_FILE_REFLECTED_TYPES_LIST}"
				"-DASSET_FIELDS_LIST_NAME=${SW_FILE_ASSET_REFERENCE_FIELDS_LIST}"
				"-DGEN_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_CPP}"
				"-DGEN_META_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_META_CPP}"
				"-DTARGET_ARGS=${parser_test_target_args}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/RunParserFixture.cmake")
		set_tests_properties(ReflectionParser.${parser_fixture_name} PROPERTIES LABELS nogpu TIMEOUT 120)
	endforeach()
	# 도구 입력 — 같은 입력 두 번이면 같은 바이트 · 시각 그대로, 명령줄만 바꾸면 다시 파싱, 설정 · 템플릿 · 철자 표 · 내장 표가 깨지면 0 이 아닌 종료(`ReflectionParser/RunParserToolInputs.cmake`).
	foreach(parser_tool_mode IN ITEMS determinism args-changed broken-config broken-templates broken-builtins-interp broken-builtins-widget broken-builtins-layout broken-builtins-container)
		add_test(NAME ReflectionParser.ToolInputs.${parser_tool_mode}
			COMMAND ${CMAKE_COMMAND}
				"-DMODE=${parser_tool_mode}"
				"-DPARSER=$<TARGET_FILE:ReflectionParser>"
				"-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/Fixtures/ValidTypes.h"
				"-DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/ReflectionParser/ToolInputs.${parser_tool_mode}"
				"-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
				"-DGENERATED_DIR=${CMAKE_BINARY_DIR}/generated"
				"-DBUILTINS=${CMAKE_SOURCE_DIR}/${SW_FILE_REFLECT_BUILTINS}"
				"-DTEMPLATES=${CMAKE_SOURCE_DIR}/${SW_DIR_TOOLS_REFLECTION_TEMPLATES}"
				"-DCONFIG=${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_DEFAULTS}"
				"-DGEN_CPP_SUFFIX=${SW_EXT_REFLECTION_GEN_CPP}"
				"-DTARGET_ARGS=${parser_test_target_args}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/ReflectionParser/RunParserToolInputs.cmake")
		set_tests_properties(ReflectionParser.ToolInputs.${parser_tool_mode} PROPERTIES LABELS nogpu TIMEOUT 120)
	endforeach()
	# 틀린 명령줄은 파서를 멈춘다 — 모르는 인자 · 필수 인자 빠짐. 입력을 읽기 전이라 사례 헤더가 필요 없다.
	add_test(NAME ReflectionParser.RejectsUnknownArgument COMMAND $<TARGET_FILE:ReflectionParser> --no-such-argument)
	add_test(NAME ReflectionParser.RejectsMissingOutput COMMAND $<TARGET_FILE:ReflectionParser> --input nothing.h)
	set_tests_properties(ReflectionParser.RejectsUnknownArgument ReflectionParser.RejectsMissingOutput PROPERTIES LABELS nogpu TIMEOUT 60 WILL_FAIL TRUE)
endif()

# 임포트 밖의 모든 테스트가 임포트한 산출물을 요구한다(맨 위 `Import.*`).
get_property(all_test DIRECTORY PROPERTY TESTS)
list(FILTER all_test EXCLUDE REGEX "^Import\.")
set_tests_properties(${all_test} PROPERTIES FIXTURES_REQUIRED ImportedAssets)
if(SW_CPU_HEAVY_TEST AND TARGET EditorModule)
	set_tests_properties(Bake.Portraits.DirectX11 Bake.Portraits.DirectX12 Bake.Portraits.Vulkan Bake.Portraits.OpenGL PROPERTIES DEPENDS ${SW_CPU_HEAVY_TEST})
endif()

# ctest 항목이 띄우는 시나리오(`Content/` 기준 경로)를 적는다 — 항목을 만드는 자리마다 부른다. Scenario/ 의 끝이 어느 항목도 띄우지 않는 시나리오를 찾아 구성을 세운다.
function(sw_register_scenarios)
	set_property(GLOBAL APPEND PROPERTY SW_REGISTERED_SCENARIOS ${ARGN})
endfunction()

add_subdirectory(Net)
add_subdirectory(Scenario)

# 퍼저(Linux-Fuzz 프리셋만) — 신뢰 경계 파서의 스모크 `Fuzz.<이름>.Smoke`. 임포트 산출물을 읽지 않아 위의 고정 단계를 요구하지 않는다(다른 폴더라 걸리지 않는다).
if(SW_ENABLE_FUZZING)
	add_subdirectory(Fuzz)
endif()
