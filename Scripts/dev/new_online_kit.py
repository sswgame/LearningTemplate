"""
온라인 키트 뼈대를 만든다 — 새 키트 하나를 붙이는 자리(매니페스트 둘 · CMake 둘 · 프로토콜 서술자 · 클라이언트 · 서버 서비스 · 서버 조립 · 테스트)를 한 번에 놓는다.

  py -3 -m Scripts new-online-kit Inventory [--dry-run]

폴더 틀(서버 온라인 조사 P11): `Source/GameFramework/Kits/Feature/Online/<이름>/`
  Runtime/  공유 모듈 `GF_<이름>`(대상 Client · Server) — 메서드 · 요청/응답 형식(`<이름>Protocol`) · 클라이언트(`<이름>Client`)
  Server/   서버 모듈 `GF_Server_<이름>`(대상 Server, `GF_Server_OnlineCore` 위) — 서비스(`<이름>Service`) · 서버 조립 · 테스트
중앙 표 두 곳에 한 줄씩 더한다: 메서드 영역(`OnlineMethodRange::k<이름>`, 다음 빈 0x100 칸)과 서버 키트 이름(`OnlineServerKitName::k<이름>`).
뼈대의 메서드는 `Echo` 하나다(요청 글을 그대로 돌려준다) — 실제 메서드로 바꾸고 결과 열거형에 도메인 값을 더한다(공통 범주는 `OnlineError` 다).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from common.command import Command, ScriptContext
from common.exit_code import ExitCode

KITS_ROOT = "Source/GameFramework/Kits"
KIT_ROOT = f"{KITS_ROOT}/Feature/Online"
RANGE_HEADER = "Source/GameFramework/Runtime/Online/Service/OnlineProtocol.h"
KIT_NAME_HEADER = "Source/GameFramework/Runtime/Online/Service/OnlineServerKitName.h"


def make_snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def make_lower_camel(name: str) -> str:
    return name[0].lower() + name[1:]


def make_files(name: str) -> dict[str, str]:
    lower = make_lower_camel(name)
    files: dict[str, str] = {}
    files[f"Runtime/GF_{name}.module.json"] = f'''{{
    "_name": "GF_{name}",
    "_version": "1.0.0",
    "_kind": "Kit",
    "_description": "{name} 키트(공유) — 메서드 · 요청/응답 형식 · 클라이언트 (온라인 게임)",
    "_listDependency": [
        {{
            "_name": "GameFramework"
        }}
    ],
    "_listConfiguration": [
        "Dev",
        "Shipping"
    ],
    "_listTarget": [
        "Client",
        "Server"
    ],
    "_bEnabledByDefault": true
}}
'''
    files["Runtime/CMakeLists.txt"] = f'''# ==============================================================================
# @file {KIT_ROOT}/{name}/Runtime/CMakeLists.txt
# @brief {name} 키트(공유) — 메서드 · 요청/응답 형식 · 클라이언트
# ==============================================================================

sw_add_game_framework_kit(GF_{name})
'''
    files[f"Runtime/{name}Protocol.h"] = f'''/**
 * @file {name}Protocol.h
 * @brief {name} 키트의 메서드 표 · 요청/응답 형식 · 등록 항목입니다.
 * @details 결과는 두 층이다. 업무 결과(`{name}Result`)는 응답의 `_result` 이고, 로그인 없음 · 깨진 요청 · 일시 장애는 공통 범주(`OnlineError`)로 `OnlineResult::_errorCode` 에 온다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

namespace sw
{{
    /** @brief {name} 메서드 이름입니다. 열거자 순서가 와이어 번호이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class {name}MethodName : uint8
    {{
        Echo,
        Count,
    }};

    /** @brief {name} 알림 이름입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class {name}PushName : uint8
    {{
        Count,
    }};

    /** @brief {name} 키트의 업무 결과입니다. 공통 범주는 두지 않는다(`OnlineError`). */
    ENUM()
    enum class {name}Result : uint8
    {{
        Ok = 0,
    }};

    using {name}Outcome = OnlineServiceResult<{name}Result>;
}} // namespace sw

namespace sw
{{
    /** @brief `Echo` 의 요청입니다. */
    REFLECT()
    struct SW_KIT_API {name}EchoRequest
    {{
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _text{{}};
    }};

    /** @brief `Echo` 의 응답입니다. */
    REFLECT()
    struct SW_KIT_API {name}EchoReply
    {{
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _text{{}};
        PROPERTY()
        {name}Result _result{{ {name}Result::Ok }};
    }};
}} // namespace sw

namespace sw
{{
    /** @brief {name} 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝은 서술자가 정합니다. */
    struct {name}Method : OnlineMethodTable<{name}MethodName, OnlineMethodRange::k{name}, {name}PushName>
    {{
        using Echo = Method<MethodName::Echo, {name}EchoRequest, {name}EchoReply, OnlineMethodFlag::ReadOnly>; // 호출 종류를 꼭 하나 적는다(읽기 · 키 필수 · 본래 멱등)
    }};

    /** @brief {name} 키트의 프로토콜 표, 메시지 목록, 등록 항목입니다. */
    struct SW_KIT_API {name}Protocol
    {{
        using Table        = OnlineProtocolTable<{name}Method, OnlineMethodList<{name}Method::Echo>, OnlinePushList<>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = {name}Result;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
    }};
}} // namespace sw
'''
    files[f"Runtime/{name}Protocol.cpp"] = f'''#include "pch.h"

#include "GameFramework/Kits/Feature/Online/{name}/Runtime/{name}Protocol.h"

namespace sw
{{
    const OnlineProtocolEntry {name}Protocol::kOnlineEntry{{ "{name}", {name}Protocol::Table{{}} }};
}} // namespace sw
'''
    files[f"Runtime/{name}Client.h"] = f'''/**
 * @file {name}Client.h
 * @brief {name} 키트의 클라이언트입니다. 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다(`call<{name}Method::Echo>`).
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/{name}/Runtime/{name}Protocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{{
    /**
     * @class {name}Client
     * @brief {name} 클라이언트입니다. 캐시를 고치는 메서드만 `onCallSucceeded` 오버로드를 둔다.
     */
    class SW_KIT_API {name}Client final : public OnlineProtocolClient<{name}Client, {name}Protocol>
    {{
    public:
        using OnlineProtocolClient::onCallSucceeded;
    }};
}} // namespace sw
'''
    files[f"Server/GF_Server_{name}.module.json"] = f'''{{
    "_name": "GF_Server_{name}",
    "_version": "1.0.0",
    "_kind": "Kit",
    "_description": "{name} 키트(서버) — 서비스 · 서버 조립 (온라인 게임)",
    "_listDependency": [
        {{
            "_name": "GameFramework"
        }},
        {{
            "_name": "GF_Server_OnlineCore"
        }},
        {{
            "_name": "GF_{name}"
        }}
    ],
    "_listConfiguration": [
        "Dev",
        "Shipping"
    ],
    "_listTarget": [
        "Server"
    ],
    "_bEnabledByDefault": true
}}
'''
    files["Server/CMakeLists.txt"] = f'''# ==============================================================================
# @file {KIT_ROOT}/{name}/Server/CMakeLists.txt
# @brief {name} 키트(서버) — 서비스 · 서버 조립
# ==============================================================================

sw_add_game_framework_kit(GF_Server_{name})
'''
    files[f"Server/{name}Service.h"] = f'''/**
 * @file {name}Service.h
 * @brief {name} 요청 처리기입니다. 서비스 스레드에서 씁니다.
 * @details 저장소 왕복은 `_store.submit( 일 람다, 완료 람다 )` 로 맡기고, 완료에서 `finish( call, outcome, reply )` 로 끝낸다. 맡긴 일 수는 기반이 센다(`isIdle` · `getOpenJobCount`).
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Kits/Feature/Online/{name}/Runtime/{name}Protocol.h"

namespace sw
{{
    class ServiceStore;
    class MetricRegistry;

    /** @class {name}Service @brief {name} 요청 처리기입니다. */
    class SW_KIT_SERVER_API {name}Service final : public OnlineProtocolService<{name}Service, {name}Protocol>
    {{
    public:
        /** @brief 저장소를 잇고 지표를 등록합니다. 저장소가 없으면 false 입니다. */
        [[nodiscard]] bool initialize( ServiceStore* pStore, MetricRegistry* pMetricRegistry );
        /** @brief 맡긴 일이 끝날 때까지 기다린 뒤 남은 호출을 버립니다. 저장소를 종료하기 전에 부릅니다. */
        void shutdown();

        // 프로토콜 표의 메서드 — 로그인 · 본문 · 멱등 키는 기반이 먼저 걸렀다
        void handleCall( OnlineServerCall<{name}Method::Echo> call, const OnlineCallContext& context, const {name}EchoRequest& request );
    }};
}} // namespace sw
'''
    files[f"Server/{name}Service.cpp"] = f'''#include "pch.h"

#include "GameFramework/Kits/Feature/Online/{name}/Server/{name}Service.h"

namespace sw
{{
    bool {name}Service::initialize( ServiceStore* pStore, MetricRegistry* pMetricRegistry )
    {{
        if ( pStore == nullptr )
        {{
            SW_LOG_ERROR_ID( "{name}Service.NeedsStore", "{name}Service needs a store" );
            return false;
        }}
        _store.bindStore( pStore );
        initializeCalls( pMetricRegistry, "{make_snake(name)}" );
        return true;
    }}

    void {name}Service::shutdown()
    {{
        _store.flushUntilIdle( [this]()
        {{ return isIdle(); }} );
        _calls.abandonAll();
        _store.bindStore( nullptr );
    }}

    void {name}Service::handleCall( OnlineServerCall<{name}Method::Echo> call, const OnlineCallContext& context, const {name}EchoRequest& request )
    {{
        (void)context;
        {name}EchoReply reply;
        reply._text = request._text;
        (void)finish( call, {name}Outcome{{ {name}Result::Ok }}, reply );
    }}
}} // namespace sw
'''
    files[f"Server/{name}OnlineServerKit.cpp"] = f'''#include "pch.h"

#include "GameFramework/Kits/Feature/Online/{name}/Server/{name}Service.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// {name} 키트의 서버 조립 — 서비스를 호스트에 걸고 시작 때 저장소를 잇는다.

namespace sw
{{
    namespace
    {{
        /** @class {name}OnlineServerKit @brief {name} 키트의 서버 조립입니다. 다른 키트의 인터페이스를 받으면 형식 목록(`OnlineServerKit<OnlineKitRequired<…>, OnlineKitOptional<…>>`)에 적는다. */
        class {name}OnlineServerKit final : public OnlineServerKit<>
        {{
        public:
            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
            {{
                return registerServerService( context, entry, &_service );
            }}

            expected<void> start( OnlineServerKitContext& context, const Dependencies&, const UnorderedDependencies& ) override
            {{
                if ( _service.initialize( context._pStore, context._pMetricRegistry ) == false )
                    return unexpected( Error{{ ErrorKind::InvalidArgument, "the {lower} service needs a store" }} );
                return {{}};
            }}

            /** @brief 계정 id 를 두는 표가 없으면 nullptr 다. 표가 생기면 탈퇴 정리를 정한다. */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override {{ return nullptr; }}

            int32 getOpenJobCount() const override {{ return _service.getOpenJobCount(); }}

            void stop() override
            {{
                if ( isStarted() )
                    _service.shutdown();
            }}

        private:
            {name}Service _service;
        }};
    }} // namespace
}} // namespace sw

namespace sw
{{
    SW_REGISTER_ONLINE_SERVER_KIT( {name}OnlineServerKit, OnlineServerKitName::k{name} );
}} // namespace sw
'''
    files[f"Server/Tests/Test{name}Service.cpp"] = f'''#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/{name}/Runtime/{name}Protocol.h"

/**
 * @brief [{name}ProtocolTest] 메서드 번호가 키트 영역 안에 있다
 */
SW_TEST_CASE( {name}ProtocolTest, EchoIsInsideTheKitRange )
{{
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isInRange( sw::{name}Method::Echo::kID, sw::OnlineMethodRange::k{name} ) );
}}
'''
    return files


def add_method_range(text: str, name: str) -> str:
    """메서드 영역 표 글에 다음 빈 칸을 더한다. 이미 있으면 그대로 돌려준다."""
    if re.search(rf"\bk{name}\s*=", text):
        return text
    anchor = re.search(r"\n(\s*)static constexpr uint16 kGame\s*=", text)
    if anchor is None:
        raise RuntimeError("kGame anchor not found in " + RANGE_HEADER)
    list_base = [int(value, 16) for value in re.findall(r"static constexpr uint16 k\w+\s*=\s*0x([0-9A-Fa-f]{4});", text)]
    next_base = max(value for value in list_base if value < 0x8000) + 0x0100
    if next_base >= 0x8000:
        raise RuntimeError("no free online method range below kGame")
    return text[: anchor.start()] + f"\n{anchor.group(1)}static constexpr uint16 k{name} = 0x{next_base:04X};" + text[anchor.start():]


def add_kit_name(text: str, name: str) -> str:
    """서버 키트 이름 표 글에 한 줄을 더한다. 이미 있으면 그대로 돌려준다."""
    if re.search(rf"\bk{name}\s*=", text):
        return text
    anchor = re.search(r"\n(\s*)static constexpr const utf8\* kAdmin\s*=[^\n]*", text)
    if anchor is None:
        raise RuntimeError("kAdmin anchor not found in " + KIT_NAME_HEADER)
    return text[: anchor.end()] + f'\n{anchor.group(1)}static constexpr const utf8* k{name} = "{make_lower_camel(name)}";' + text[anchor.end():]


def describe_invalid_request(root: Path, name: str) -> str | None:
    """만들 수 없는 까닭을 돌려준다(만들 수 있으면 None)."""
    if re.fullmatch(r"[A-Z][A-Za-z0-9]+", name) is None:
        return f"'{name}' is not PascalCase"
    if (root / KIT_ROOT / name).exists():
        return f"{KIT_ROOT}/{name} already exists"
    list_manifest = [path for stem in (f"GF_{name}", f"GF_Server_{name}") for path in sorted((root / KITS_ROOT).rglob(f"{stem}.module.json"))]
    if list_manifest:
        return f"module name already used: {list_manifest[0].relative_to(root).as_posix()}"
    return None


class NewOnlineKitCommand(Command):
    name = "new-online-kit"
    help = "온라인 키트 뼈대(Runtime · Server 모듈, 서술자 · 클라이언트 · 서비스 · 조립 · 테스트)를 만들고 중앙 표(메서드 영역 · 서버 키트 이름)에 한 줄씩 더한다"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("name", help="키트 이름(PascalCase, 예: Inventory)")
        parser.add_argument("--dry-run", action="store_true", help="만들 파일만 보인다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        name: str = args.name
        root = context.root
        reason = describe_invalid_request(root, name)
        if reason is not None:
            print(f"[new-online-kit] {reason}", file=sys.stderr)
            return ExitCode.CHECK_INVALID
        # 파일을 하나라도 쓰기 전에 중앙 표 두 곳의 자리를 확인한다 — 표가 옮겨졌으면 반쯤 만든 키트를 남기지 않는다.
        list_table: list[tuple[str, str, str]] = []
        for relative, add_row in ((RANGE_HEADER, add_method_range), (KIT_NAME_HEADER, add_kit_name)):
            text = (root / relative).read_bytes().decode("utf-8")
            list_table.append((relative, text, add_row(text, name)))
        for relative, content in make_files(name).items():
            path = root / KIT_ROOT / name / relative
            print(f"[new-online-kit] {'would write' if args.dry_run else 'write'} {path.relative_to(root).as_posix()}")
            if args.dry_run is False:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="")
        for relative, text, new_text in list_table:
            if new_text == text:
                continue
            print(f"[new-online-kit] {'would add a row to' if args.dry_run else 'added a row to'} {relative}")
            if args.dry_run is False:
                (root / relative).write_bytes(new_text.encode("utf-8"))
        if args.dry_run is False:
            print("[new-online-kit] next: py -3 -m Scripts format, then add the kit to the server config `_listKit` (and Programs/OnlineLoadBot to run its tests)")
        return ExitCode.PASSED
