"""
Scripts/common/ConfigCatalog.py

설정 파일 목록 — 파일마다 "어느 층 · 무엇이 읽나 · 언제 · 배포본에 들어가나 · 커밋하나" 를 적은 표 하나입니다.

칸 표(이름 · 타입 · 기본값 · 범위 · 설명)는 여기 적지 않습니다. 그것은 코드(설정 구조체의 PROPERTY 와 문서 주석, 손으로 읽는 설정의
`ConfigKeyDoc` 표)가 정본이고 `ConfigReference.py` 가 거기서 뽑습니다. 이 표가 들고 있는 것은 코드에서 뽑을 수 없는 것 — 경로와 수명 — 뿐입니다.

`CheckConfigReference.py` 가 이 표를 저장소와 양쪽으로 대조합니다: `Config/` 의 파일 · `Resource/` 의 설정 XML 이 모두 어느 줄에 맞아야 하고,
줄이 가리키는 타입 · 키 표 · 정본 문서가 있어야 합니다. 그래서 새 설정 파일은 이 표에 한 줄을 더해야 커밋됩니다.
"""

from __future__ import annotations

from dataclasses import dataclass

# ------------------------------------------------------------------------------
# 1) 층 — `docs/07_Configuration.md` 의 결정표와 같은 이름이다
# ------------------------------------------------------------------------------
kLayerEngineDefault = "엔진 기본값"
kLayerGamePreset = "게임 프리셋"
kLayerPackData = "팩 데이터"
kLayerUser = "사용자 설정"
kLayerEditor = "에디터 도구"
kLayerLocal = "개발 머신 로컬"
kLayerServer = "서버 운영"
kLayerBuildContract = "빌드 · 쿠킹 계약"
kLayerModule = "모듈 매니페스트"

#: 층의 표시 순서(색인 페이지).
kListLayerOrder = (kLayerEngineDefault, kLayerGamePreset, kLayerPackData, kLayerUser, kLayerEditor, kLayerServer, kLayerBuildContract,
                   kLayerModule, kLayerLocal)

# ------------------------------------------------------------------------------
# 2) 키 이름 규칙 — 파일의 키가 코드의 무엇과 같은가
# ------------------------------------------------------------------------------
#: JSON 키 = 멤버 이름 그대로(`_width`). 리플렉션 `JsonSerializer` 가 읽는다.
kKeyStyleJsonMember = "json-member"
#: XML 속성 · 자식 원소 = 멤버 이름 그대로(`_gravity="0,-9.81,0"`). 리플렉션 `XmlSerializer` 가 읽는다.
kKeyStyleXmlMember = "xml-member"
#: XML 자식 원소 = 멤버 이름에서 앞의 `_` 를 뗀 것(`<startMap>`). 손으로 읽는다(`GameSettings::loadRoot`).
kKeyStyleXmlElementBare = "xml-element-bare"
#: JSON 키 = `ConfigKeyDoc` 표의 키(snake_case). 손으로 읽는다.
kKeyStyleKeyTable = "key-table"


@dataclass(frozen=True)
class ConfigFileEntry:
    """설정 파일 한 종류입니다. `pathPattern` 하나가 여러 파일에 맞을 수 있습니다(게임마다 하나인 프리셋)."""

    page: str                     #: 생성 페이지 이름(`docs/Config/<page>.md`). 빈 글이면 색인에만 나온다
    pathPattern: str              #: 저장소 상대 경로 패턴(`fnmatch`, `*` 는 `/` 를 넘는다). 저장소 밖이면 설명 글
    layer: str                    #: 위 `kLayer*` 하나
    fileFormat: str               #: "json" · "xml" · "ini"
    reader: str                   #: 무엇이 읽나(코드 자리)
    readWhen: str                 #: 언제 읽나(기동 · 핫 리로드 · 쿠킹 · configure …)
    shipping: str                 #: 배포본(Shipping)에서 어떻게 되나
    bCommitted: bool              #: 저장소에 커밋하나(False = 로컬 생성 · git 무시)
    typeName: str = ""            #: 리플렉션 구조체 이름(헤더에서 찾는다)
    header: str = ""              #: `typeName` 이 선언된 헤더
    keyStyle: str = ""            #: 위 `kKeyStyle*` 하나
    keyTableSource: str = ""      #: `ConfigKeyDoc` 표가 있는 파일(손으로 읽는 설정)
    listKeyTable: tuple[str, ...] = ()  #: 그 파일의 표 이름들(`kArrTextureRuleKeyDoc` …) — 표마다 한 절
    ownerDoc: str = ""            #: 칸 표 대신 정본 문서(파이썬이 정본인 계약 · 따로 문서가 있는 형식)
    bOptional: bool = False       #: 맞는 파일이 하나도 없어도 된다(게임 팩의 덮어쓰기 · 아직 없는 서버 설정)
    bOutsideRepo: bool = False    #: 저장소 밖 파일(사용자 폴더) — 대조하지 않는다
    note: str = ""                #: 색인에 붙일 한 줄


#: 설정 파일 표입니다. 순서가 색인의 순서입니다(층 안에서).
kListConfigFile: tuple[ConfigFileEntry, ...] = (
    # --- 엔진 기본값 ---------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="EngineConfig", pathPattern="Config/Engine/EngineConfig.json", layer=kLayerEngineDefault, fileFormat="json",
        reader="`ConfigManager::ensureConfig<EngineConfig>` (`EngineLoop` Config 단계)",
        readWhen="기동 · 에디터 핫 리로드(창 크기 · 백엔드처럼 기동 때만 쓰는 값은 다음 실행부터)",
        shipping="configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`) — 디스크의 파일은 읽지 않는다", bCommitted=True,
        typeName="EngineConfig", header="Source/Engine/Config/EngineConfig.h", keyStyle=kKeyStyleJsonMember),
    ConfigFileEntry(
        page="MemoryBudget", pathPattern="Config/Engine/MemoryBudget.json", layer=kLayerEngineDefault, fileFormat="json",
        reader="`MemoryBudgetMonitor::loadBudgetFile` (`EngineLoop` Config 단계)", readWhen="기동",
        shipping="읽지 않음(배포본에는 메모리 프로파일러가 없다)", bCommitted=True, keyStyle=kKeyStyleKeyTable,
        keyTableSource="Source/Engine/Utility/Profiling/MemoryBudgetMonitor.cpp",
        listKeyTable=("kArrMemoryBudgetRootKeyDoc", "kArrMemoryBudgetEntryKeyDoc")),
    ConfigFileEntry(
        page="EngineDefaultAssets", pathPattern="Resource/engine/data/enginedefaultassets.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`EngineDefaultAssets::loadFromResource` (`EngineLoop` 엔진 셸 단계)", readWhen="기동",
        shipping="엔진 팩에 실림", bCommitted=True,
        typeName="EngineDefaultAssets", header="Source/Engine/Config/EngineDefaultAssets.h", keyStyle=kKeyStyleXmlMember),
    ConfigFileEntry(
        page="PhysicsSettings", pathPattern="Resource/engine/physics/physicssettings.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`PhysicsSettings::loadFromResource` (`PhysicsSystem`)", readWhen="기동(씬 물리를 처음 세울 때)",
        shipping="엔진 팩에 실림", bCommitted=True,
        typeName="PhysicsSettings", header="Source/Engine/Physics/PhysicsSettings.h", keyStyle=kKeyStyleXmlMember),
    ConfigFileEntry(
        page="NavMeshSettings", pathPattern="Resource/engine/navigation/navmeshsettings.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`NavMeshSettings` (`ResourceCatalog`)", readWhen="처음 쓸 때 · 내비메시 쿠킹",
        shipping="엔진 팩에 실림", bCommitted=True,
        typeName="NavMeshSettings", header="Source/Engine/Navigation/NavMeshSettings.h", keyStyle=kKeyStyleXmlMember),
    ConfigFileEntry(
        page="", pathPattern="Resource/engine/data/render2d.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`Render2DSettings::getActive` (손으로 읽음 — 모르는 요소 · 속성은 오류)", readWhen="처음 쓸 때 · `reloadActive`",
        shipping="엔진 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/Graphics/2D/Render2DSettings.h",
        note="활성 게임 팩의 `data/render2d.xml` 이 있으면 통째로 대신한다"),
    ConfigFileEntry(
        page="", pathPattern="Resource/engine/input/default.input.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`InputMap::loadFromResource` (셸 입력 맵 — `EngineDefaultAssets::_shellInputMap`)", readWhen="기동 · 에셋 핫 리로드",
        shipping="엔진 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/Input/README.md"),
    ConfigFileEntry(
        page="", pathPattern="Resource/engine/input/ui.input.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`InputMap::loadFromResource` (런타임 UI 행동 맵 — `EngineDefaultAssets::_uiInputMap`, `UiSystem::initialize`)", readWhen="기동(Ui 단계)",
        shipping="엔진 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/UI/README.md"),
    ConfigFileEntry(
        page="UserSettings", pathPattern="Resource/engine/settings/engine.settings.xml", layer=kLayerEngineDefault, fileFormat="xml",
        reader="`UserSettingsManager::loadSchema` (`EngineLoop` UserSettings 단계)", readWhen="기동",
        shipping="엔진 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/UserSettings/README.md",
        note="플레이어 옵션 메뉴의 스키마 — 설정 표는 생성 페이지 `UserSettings.md`"),
    # --- 게임 프리셋 --------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="GameConfig", pathPattern="Config/Game/*.json", layer=kLayerGamePreset, fileFormat="json",
        reader="`ConfigManager::ensureConfig<GameConfig>` (`EngineLoop` Config 단계)",
        readWhen="기동 · 에디터 핫 리로드 — `SW_ACTIVE_GAME` 이 파일 하나를 고른다",
        shipping="configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`)", bCommitted=True,
        typeName="GameConfig", header="Source/Engine/Config/GameConfig.h", keyStyle=kKeyStyleJsonMember,
        note="게임마다 하나 — `CheckGamePresets` 가 팩 · CMake 프리셋과 대조한다"),
    # --- 팩 데이터 ----------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="GameSettings", pathPattern="Resource/game/*/data/gamesettings.xml", layer=kLayerPackData, fileFormat="xml",
        reader="`GameSettings::loadFromResource` (`GameInstanceBase::initialize`)", readWhen="게임 인스턴스 초기화 · 게임 모듈 핫 리로드",
        shipping="게임 팩에 실림", bCommitted=True,
        typeName="GameSettings", header="Source/GameFramework/Base/Data/GameSettings.h", keyStyle=kKeyStyleXmlElementBare,
        note="시작 씬(`startMap` · `titleScene`)은 이 파일 하나다 — 모르는 원소는 로드 오류, 커스텀 값은 `<custom><prop key>`"),
    ConfigFileEntry(
        page="", pathPattern="Resource/game/*/data/render2d.xml", layer=kLayerPackData, fileFormat="xml",
        reader="`Render2DSettings::getActive`", readWhen="처음 쓸 때 · `reloadActive`", shipping="게임 팩에 실림", bCommitted=True,
        ownerDoc="Source/Engine/Graphics/2D/Render2DSettings.h", bOptional=True, note="엔진 기본 `render2d.xml` 을 통째로 대신한다"),
    ConfigFileEntry(
        page="UserSettings", pathPattern="Resource/game/*/data/*.settings.xml", layer=kLayerPackData, fileFormat="xml",
        reader="`UserSettingsManager::loadSchema` (게임 프리셋 `_userSettingsSchema`)", readWhen="기동",
        shipping="게임 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/UserSettings/README.md", bOptional=True,
        note="엔진 스키마에 덧붙는 게임 설정"),
    ConfigFileEntry(
        page="", pathPattern="Resource/game/*/data/*.input.xml", layer=kLayerPackData, fileFormat="xml",
        reader="`InputMap::loadFromResource` (`GameSettings::_inputMap`)", readWhen="게임 인스턴스 초기화",
        shipping="게임 팩에 실림", bCommitted=True, ownerDoc="Source/Engine/Input/README.md", bOptional=True),
    # --- 사용자 설정 --------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="UserSettings", pathPattern="%LOCALAPPDATA%/SWEngine/<팩>/usersettings.json", layer=kLayerUser, fileFormat="json",
        reader="`UserSettingsManager` (사용자 파일)", readWhen="기동 · 메뉴에서 적용할 때 씀",
        shipping="플레이어 PC 에 생긴다 — 배포된 플레이어 데이터(이름을 바꾸면 스키마 `<Upgrade>`)", bCommitted=False,
        ownerDoc="Source/Engine/UserSettings/README.md", bOutsideRepo=True,
        note="Linux 는 `$XDG_CONFIG_HOME/swengine/<팩>/` · 자동화는 `-gv_userSettingsFile=<경로>`"),
    # --- 에디터 도구 --------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="EditorToolDefaults", pathPattern="Config/Editor/editortooldefaults.json", layer=kLayerEditor, fileFormat="json",
        reader="`EditorToolDefaults::loadFromHostPath` (에디터 모듈 기동)", readWhen="에디터 기동 · 에디터 핫 리로드",
        shipping="없음(에디터 없음)", bCommitted=True,
        typeName="EditorToolDefaults", header="Source/Editor/Common/Config/EditorToolDefaults.h", keyStyle=kKeyStyleJsonMember, bOptional=True,
        note="기본값과 다른 값이 있을 때만 만든다 — 없으면 기본값"),
    ConfigFileEntry(
        page="EditorConfig", pathPattern="Saved/Editor/EditorConfig.json", layer=kLayerEditor, fileFormat="json",
        reader="`EditorConfig::loadFromHost` · `saveToHost`", readWhen="에디터 기동 · 테마 저장 때 **앱이 통째로 다시 쓴다**",
        shipping="없음(에디터 없음)", bCommitted=False,
        typeName="EditorConfig", header="Source/Editor/Common/Config/EditorConfig.h", keyStyle=kKeyStyleJsonMember, bOutsideRepo=True,
        note="앱이 쓰는 상태(`Saved/`, git 무시) — 사람이 고치지 않는다"),
    ConfigFileEntry(
        page="TextureImportConfig", pathPattern="Config/Editor/TextureImportConfig.json", layer=kLayerEditor, fileFormat="json",
        reader="`TextureImportConfig::loadFromFile` (손으로 읽음)", readWhen="텍스처 임포트(에디터 · `App --import-textures`)",
        shipping="없음(임포트는 Dev 만)", bCommitted=True, keyStyle=kKeyStyleKeyTable,
        keyTableSource="Source/Editor/Common/Asset/TextureImportConfig.cpp",
        listKeyTable=("kArrTextureImportRootKeyDoc", "kArrTextureImportRuleKeyDoc")),
    ConfigFileEntry(
        page="ModelImportConfig", pathPattern="Config/Editor/ModelImportConfig.json", layer=kLayerEditor, fileFormat="json",
        reader="`ModelImportConfig::loadFromFile` (손으로 읽음)", readWhen="모델 임포트(에디터 · `App --import-models`)",
        shipping="없음(임포트는 Dev 만)", bCommitted=True, keyStyle=kKeyStyleKeyTable,
        keyTableSource="Source/Editor/Common/Asset/ModelImportConfig.cpp",
        listKeyTable=("kArrModelImportRootKeyDoc", "kArrModelImportRuleKeyDoc", "kArrModelImportFractureKeyDoc")),
    ConfigFileEntry(
        page="", pathPattern="Config/Editor/AssetValidationRules.json", layer=kLayerEditor, fileFormat="json",
        reader="`Scripts/common/AssetValidation.py` (에디터 `EditorAssetValidation` 도 이것을 돌린다)",
        readWhen="저장 · 임포트 직후(에디터) · `CheckAssetRules` 게이트 · `py -3 -m Scripts validate-assets`",
        shipping="없음", bCommitted=True, ownerDoc="Scripts/common/AssetValidation.py"),
    # --- 서버 운영 ----------------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="ServerConfig", pathPattern="Config/Server/*.json", layer=kLayerServer, fileFormat="json",
        reader="`ConfigManager::getConfig<ServerConfig>` (전용 서버 기동, `-server-config=<경로>` 로 바꾼다)",
        readWhen="전용 서버 기동", shipping="**디스크에서 읽는다**(운영자가 고친다) — 없으면 Shipping 서버는 기동 실패", bCommitted=True,
        typeName="ServerConfig", header="Source/Engine/Config/ServerConfig.h", keyStyle=kKeyStyleJsonMember, bOptional=True,
        note="비밀(DB 비밀번호 · 캐시 AUTH · 키 암호)은 파일에 쓰지 않는다 — `_secretEnvironment` 필드가 환경 변수 이름을 가리킨다"),
    ConfigFileEntry(
        page="", pathPattern="Config/Server/chat_banned_words.txt", layer=kLayerServer, fileFormat="txt",
        reader="`ChatWordFilter::loadFile` (`GF_Server_Chat`)", readWhen="채팅 서비스 기동",
        shipping="디스크에서 읽는다(운영자가 고친다) — 저장소에는 시험 낱말만", bCommitted=True,
        ownerDoc="Source/GameFramework/Kits/Online/Server/Chat/ChatWordFilter.h"),
    # --- 빌드 · 쿠킹 계약 ---------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="", pathPattern="Config/Engine/CookContract.json", layer=kLayerBuildContract, fileFormat="json",
        reader="`GenerateCookContract.py`(→ `CookContract.gen.h`) · `Scripts/common/CookContract.py`",
        readWhen="configure · 쿠킹 · `CheckCookContract` 게이트", shipping="빌드에 굳음", bCommitted=True,
        ownerDoc="Scripts/common/CookContract.py"),
    ConfigFileEntry(
        page="", pathPattern="Config/Engine/PackFormat.json", layer=kLayerBuildContract, fileFormat="json",
        reader="`GeneratePackFormat.py`(→ `PackFormat.gen.h`) · `Scripts/common/PackFormat.py`", readWhen="configure · 쿠킹",
        shipping="빌드에 굳음", bCommitted=True, ownerDoc="Scripts/common/PackFormat.py"),
    ConfigFileEntry(
        page="", pathPattern="Config/Engine/PackConfig.json", layer=kLayerBuildContract, fileFormat="json",
        reader="`Scripts/generate/CookAssets.py`", readWhen="쿠킹(팩 만들기)", shipping="팩 모양을 정한다(파일은 안 실림)",
        bCommitted=True, ownerDoc="Scripts/generate/README.md"),
    # --- 모듈 매니페스트 ----------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="", pathPattern="Source/*.module.json", layer=kLayerModule, fileFormat="json",
        reader="CMake `ModuleManifest.cmake` · `ModuleCatalog::loadDirectory`", readWhen="configure · 기동(모듈 적재 순서)",
        shipping="빌드에 굳음(링크할 모듈 · 순서)", bCommitted=True, ownerDoc="Source/Engine/Module/ModuleCatalog.h",
        note="`ModuleCatalogTest.EveryRepositoryManifestParses` 가 모두 읽는다"),
    # --- 개발 머신 로컬 -----------------------------------------------------------------------------------------------------
    ConfigFileEntry(
        page="", pathPattern="Config/Environment/*.defaults.json", layer=kLayerLocal, fileFormat="json",
        reader="`Scripts/setup/SetupEnvironment.py` (로컬 `*.json` 의 시드)", readWhen="환경 준비 · configure",
        shipping="없음", bCommitted=True, ownerDoc="Scripts/setup/README.md"),
    ConfigFileEntry(
        page="", pathPattern="Config/Environment/toolchain_config.json", layer=kLayerLocal, fileFormat="json",
        reader="`GenerateToolchainCMake.py` · ReflectionParser", readWhen="configure · 리플렉션 코드젠",
        shipping="없음", bCommitted=False, ownerDoc="Scripts/setup/README.md", bOptional=True,
        note="SetupEnvironment 가 채운 절대 경로 캐시"),
    ConfigFileEntry(
        page="", pathPattern="Config/Environment/parser_config.json", layer=kLayerLocal, fileFormat="json",
        reader="ReflectionParser `ParserConfig::load`", readWhen="리플렉션 코드젠", shipping="없음", bCommitted=False,
        ownerDoc="Tools/ReflectionParser/README.md", bOptional=True),
    ConfigFileEntry(
        page="", pathPattern="Config/Environment/search_paths.json", layer=kLayerLocal, fileFormat="json",
        reader="`Scripts/setup/HostTools.py`", readWhen="환경 준비", shipping="없음", bCommitted=False,
        ownerDoc="Scripts/setup/README.md", bOptional=True),
    ConfigFileEntry(
        page="", pathPattern="Saved/Editor/*", layer=kLayerEditor, fileFormat="ini · json · xml",
        reader="ImGui · `EditorDockLayout` · `EditorLayoutStore` · 노드 캔버스 · 도구 문서 임시본 · gv 프리셋(`GlobalVariablePresets/<팩>/`)",
        readWhen="에디터가 쓰고 읽는다", shipping="없음", bCommitted=False, bOutsideRepo=True, ownerDoc="Source/Editor/README.md",
        note="앱이 쓰는 에디터 상태 — `EditorUtil::getEditorStateDirectory`(git 무시)"),
)

#: `Config/` 아래인데 설정이 아닌 파일 — 대조에서 뺀다.
kSetConfigFolderNonConfig = frozenset({"Config/README.md"})

#: `Resource/` 아래에서 "설정 파일" 로 보는 이름 패턴 — 이 패턴에 맞는데 표의 어느 줄에도 맞지 않으면 게이트가 진다.
kListResourceSettingPattern = ("Resource/*settings*.xml", "Resource/*.settings.xml", "Resource/*.input.xml",
                               "Resource/*/data/render2d.xml", "Resource/engine/data/enginedefaultassets.xml")
