# 구조 리팩토링 설계 — 경로 · 신원 · 경계를 한 규칙으로

> 질문: Source 아래에서 일관성이 깨진 곳(한 층에 두 방식, 입력 종류를 추측하는 함수, 폴백, 같은 일을 하는 장치 둘)을 어떤 구조로, 어떤 순서로 고치는가?
> 독자: 이 리팩토링을 이어서 하는 세션(에이전트와 사람).
> 결론: 근본 원인은 하나다 — **경로 · 리소스 신원 · 명령 인자를 형식 없는 `string` 으로 넘기고, 답을 아는 쪽(기동 · CMake · 데이터 계층)이 값을 넘기지 않아 받는 쪽마다 글자로 다시 추측한다.** 형식과 경계를 먼저 세우고(0~3 단계), 추측에 기대는 곳을 걷어 낸 뒤(4~6), 규칙을 오류로 켜고(7), 리소스 경로 · 읽기/쓰기 · 나머지 주제를 옮긴다(8~). 근거는 2026-10-31 Opus(high) 설계 검토 6 건(Core·호스트 / Engine 데이터 / Engine 런타임 / Editor / GameFramework·Games / ReflectionParser·OnlineLoadBot)이다.

## 1. 규칙

| 규칙 | 뜻 | 상용 엔진 |
|---|---|---|
| R1 OS 경계 하나 | 파일 · 폴더 · 라이브러리 · 환경 변수 · 실행 파일 경로 같은 OS 호출은 `PlatformFileUtil`(과 그 짝) 한 곳만 한다. Win32 · POSIX 네이티브로 통일하고 `std::filesystem` 은 걷어 낸다 — 한 층에 오류 모델 · UTF-16 변환이 둘이면 결함이 난다 | 언리얼 `IPlatformFile`, Godot `FileAccess`·`DirAccess` |
| R2 디스크 I/O 는 절대 경로만 | `FileUtil` · `PlatformFileUtil` 의 I/O 는 절대 경로만 받는다. 상대 · `C:foo` · `\foo` 는 오류. 경로 글 연산(`joinPath` 등)은 대상이 아니다 | |
| R3 리소스는 리소스 경로만 | `AssetPaths` · `AssetIO` 는 `ResourcePath{도메인(선택), 상대 경로}` 만 받는다. 도메인을 비우면 우선순위로 찾고, 넣으면 그 도메인만. 문자열에서 도메인을 추측하지 않는다 | 언리얼 `/Game/…`, 덮어쓰기는 pak 우선순위 |
| R4 쓰기는 FileUtil 이 절대 경로로 | `AssetPaths` · `AssetIO` 는 읽기와 경로 풀기만. 있는 파일은 리소스 경로를 디스크 위치로 풀어 쓰고, 새 파일은 도메인 루트 아래에 잇는다 | |
| R5 상대 → 절대는 경계에서 한 번, 기동 때 정한 기준으로 | 명령줄 · 설정 · 콘솔 · 시나리오 값은 들어오는 자리에서 **기동 때 고정한 루트** 기준으로 한 번 바꾼다. 실행 중 작업 폴더는 읽지 않는다 | 언리얼 `FPaths::ProjectSavedDir` |
| R6 폴백 없음 | 필요한 값이 없으면 오류로 멈춘다. "못 찾으면 받은 글", "실패하면 기본값", "실패하면 다른 구현" 은 없앤다 | |
| R7 확인과 실행은 같은 해석 | 있는지 · 쓸 수 있는지를 묻는 쪽과 실제로 읽고 쓰는 쪽이 같은 함수로 푼다 | |
| R8 같은 일은 하나 | 같은 판정 · 정규화 · 표 · 백오프 · 파서는 하나만 둔다. 답을 아는 쪽이 값을 넘긴다(재추측 금지) | |
| R9 구성 차이는 크기대로 | 값뿐이면 `build::kShipping` 상수(두 갈래 모두 컴파일), 갈래 하나가 다른 구성에 없는 헤더를 쓰면 그 자리 `#if`, 기능 전체가 없으면 빌드 단위(`<이름>Shipping.cpp` · 폴더째 빼기). 몇 줄짜리 차이로 파일 짝을 만들지 않는다([BuildConfigSwitch](agents/BuildConfigSwitch.md)) | 언리얼 Runtime · Developer · Editor 모듈과 `#if !UE_BUILD_SHIPPING` |

## 2. 기반 형식 (0 단계 — 나머지가 모두 기댄다)

| 형식 · 장치 | 내용 | 없어지는 것 |
|---|---|---|
| `AbsolutePath` + 엄격한 판정 | 검증하는 생성 함수로만 만든다. Windows 는 `X:/` · `X:\` · UNC, POSIX 는 `/`. **지금 `FileUtil::isAbsolutePath`(FileUtil.cpp:450)는 `C:foo` · `\foo` 를 절대로 본다** | 느슨한 판정 |
| `ProcessRoots` | 기동 때 한 번(실행 파일 위치 · `-project=`): binary · project · content · saved · logs · crash · userData · packs · DDC. 못 정하면 기동 오류. 지금 Saved 기준이 **일곱**(작업 폴더 · 실행 파일 폴더 · 프로젝트 · `UserDataPath` 상대 폴백 · `TestRunner` · RenderDoc · 크래시 폴더 "로그 부모" 세 곳)이다 | `getSavedFolderPath` 의 작업 폴더 의존, `AssetPaths::initialize` 의 작업 폴더에서 위로 찾기, 팩 폴더 후보 체인(AssetManager.cpp:155), `AutomationSession` 의 작업 폴더 복구, `"Saved"` 리터럴, `UserDataPath::makeFallback` |
| `ResourcePath{DomainID, RelPath}` | 만들 때 정규화(소문자 · `/` · `./` 제거 · `..` 거절). 문자열에서 암시적으로 못 만든다. 캐시 · GUID 표 · 쿠킹 입력의 키는 **해석이 끝난** 값 | `AssetID` 의 무정규화, 캐시마다 다른 키 정규화 다섯(SharedAssetTable · PrefabCache · AssetPaths 캐시 · AssetDatabase · CookJob)과 Texture · Material · Mesh · UIText · PhysicsMeshShape 의 `normalizePath` 키 |
| `DomainTable` | 도메인 이름표(engine · common · editor · `game/<팩>` · `dlc/<이름>` · scratch · 붙인 디스크 도메인) · 우선순위 · 쿠킹 대상 · 레지스트리를 한 표로. 데이터의 한 줄 참조(`common/x`)를 두 칸으로 가르는 일은 이 표의 최장 일치 한 곳에서만 | 도메인 판정 셋(`mapGlobalIDToRoot` 의 디스크 폴더 존재 · `PackManager::trySplitDomainPrefix` 의 글자 · 팩 파일 이름 `_` 변환 세 곳), 도메인 나열 넷(`buildResourceFolders` · `PackCooker::collectDomains` · `AssetDatabase::writeRegistryFiles`(scratch 까지 굽는다) · `loadAssetRegistries`), 우선순위 표 둘 |
| 입력 경계 형식 | `CommandLineManager` 의 값 종류 `DiskPath`(parse 때 R5 로 절대화, 오류는 모아 기동 실패), `gv_` 보류 값도 같은 변환기, 개발 명령 인자 읽기 `readDiskPathArgument` · `readResourcePathArgument`, 시나리오 단계 등록이 속성마다 경로 종류를 선언(`{out}` 은 디스크 칸에서만) | `setValue` 의 "실패하면 0 넣고 읽음 표시", `"--service"` 글자 비교, 토큰 나누기 둘, `prefab.spawn` 류 추측, 녹화 경로의 글자 치환(`makeReplayID`), `bindOutputDirectory` 의 모든 속성 치환 |

## 3. 주제별 설계

### T1. OS 경계 하나 (R1 · R2 · R6)
- `PlatformFileUtil` 원시 연산: 상태 조회(있음 · 종류 · 링크 · 읽기 전용 · 크기 · 시각) · 쓰기 가능 · 시각 설정 · 지금 파일 시각 · 폴더 하나 만들기 · 경로 하나 지우기(POSIX 의미, 읽기 전용 무시 — MSVC STL 과 같게) · 복사 · 한 단계 나열 · 절대화 · canonical · 작업 폴더 · 임시 폴더 · 범위 읽기 · 할당 없는 쓰기(크래시 경로). `FileUtil` 은 재귀 생성 · 삭제 · 나열 · 고유 경로 · 상대 경로(글 연산, Windows 대소문자 무시)만.
- 함께 들어올 우회 지점: `FileUtilStdFileSystem.cpp` 25 함수, `FileUtil::readRange` 의 `CreateFileW`, `getFileStamp`(FileUtil.cpp:635), `writeAtomically` 의 pid `#if`, 크래시 쓰기 두 벌(CrashContext.cpp:181 · WindowsCrashHandler.cpp:138), `WindowsFileWatcher` 폴더 핸들, `WindowsServiceHost` 의 `GetModuleFileNameW` · `MAX_PATH` 사본, Vulkan 의 `SetEnvironmentVariableW`, DXC `LoadFile` · 기본 include 처리.
- 동적 라이브러리 입구 둘: `loadLibraryAt(AbsolutePath)`(검색 없음, 한 번, 오류 코드) · `loadSystemLibrary(name)`. `SetDllDirectoryW` 를 걷고 검색 폴더는 기동 때 `AddDllDirectory`. 직접 로드하던 RenderDoc · 크래시 처리 · XInput · XAudio2 · Tracy · dxcompiler · ALSA 를 옮긴다. `ProcessCommand` 도 `at(AbsolutePath)` · `fromSearchPath(name)`.
- 오류 모델: `getFileSize` · `getFileTimestamp` 처럼 오류를 0 으로 돌려주는 함수는 `[[nodiscard]] bool` + 출력 인자로.

### T2. 리소스 경로와 팩 (R3 · R7 · R8)
- `AssetPaths` 는 넷만: 리소스 경로 → 절대 경로(도메인을 넣으면 루트 + 상대 경로 — 없는 파일도, 비우면 우선순위로 있는 파일), 절대 → 리소스 경로, 나열(도메인 하나 또는 우선순위로 이긴 것), 우선순위 · 디스크 도메인 설정. Linux 대소문자 재시도는 도메인 루트 조회 한 곳으로.
- `PackManager` 는 (도메인, 상대 경로) 색인 하나. 팩 머리에 마운트 도메인을 적고(언리얼 pak mount point) 패치 교체 · 제거도 같은 키. **지금 결함**: 도메인 없는 키로 읽으면 패치가 바꾸거나 지운 파일이 보이지 않는다(`visitPacksWithFile` 1 단계 해시가 전체 경로 기준), `dlc/<이름>` 은 팩에서 못 찾는다.
- 확인과 실행을 맞춘다: `hasAsset` 이 폴더에 true(읽기는 `isRegularFile`), `collectAssets` 가 소스 구성에선 한 도메인 · 팩 구성에선 합집합, `LocalizationManagerReload::isSamePath` 의 꼬리 글자 비교.
- 리소스 참조 형식: 장면 데이터의 `string _…Path` 속성 약 60 개를 리소스 참조 형식으로(`FileUtil` 이 받을 수 없게). `IntentTrackControllerComponent::_trackFile` 은 지금 `FileUtil` 로 작업 폴더 기준 읽기(런타임 결함).
- 캐시: `AudioClipCache` 를 `AssetCacheBase` + `AssetLoadBlock` 으로(지금은 `finishPendingAssetWork` 가 오디오 디코드를 기다리지 않는다, 리로드도 DevTools 별도 길). `TextureCache` 의 `rendertarget/` · `cpu/` 는 가상 도메인, 선언하지 않은 렌더 타깃은 256×256 으로 만들지 않고 오류. `AssetDatabase::toRelativePath` 는 `toAssetID` 로 합친다.

- **결정(2026-10-31)**: `TextureCache` 의 렌더 타깃 기본 크기(256)는 그대로 둔다 — 카메라가 나중에 크기를 알리면 `resizeRenderTarget` 이 다시 만들어 맞추므로 폴백이 아니라 자리 표시 값이다(언리얼 렌더 타깃 에셋의 기본 크기와 같다). 씬 데이터의 `string _…Path` 칸(컴포넌트 44 개)을 참조 형식으로 바꾸는 일은 하지 않는다 — 그 값이 디스크 입구(`FileUtil`)로 가는 곳은 0 이고(조사), 7 단계부터 상대 경로가 OS 경계에 닿으면 오류라 남은 위험도 드러난다. 리플렉션 · 세 직렬화기 · 에디터 위젯을 바꾸는 비용에 견줘 얻는 것이 작다(새 결함이 이 모양으로 나오면 그때 형식으로).
- `AudioClipCache` 의 디코드는 에셋 로드 단계(`AssetManager::addLoadTask`)에 넣는다(끝, 2026-10-31) — 프레임 경계 대기가 오디오도 기다린다.
- **결정(2026-10-31)**: `collectAssets` 는 소스 · 팩 구성 모두 합집합이다(같은 이름은 앞선 도메인 · 높은 팩) — 언리얼 `IterateDirectory` 가 pak 과 낱개를 합쳐 훑는 것과 같다. 팩의 도메인은 팩 안(스트링 풀 맨 앞, 형식 2)에 적고 파일 이름으로 추측하지 않는다(언리얼 pak mount point).
- **결정(2026-10-31)**: 문서 패널의 "읽을 문서 정하기" 여섯 줄 11 벌은 `EditorDocumentPanel::acceptDocumentToLoad` 하나로, 읽기 · 쓰기 자리는 `getDocumentDiskPath` 하나로 모았다. 기반 밖 자체 저장 패널 8 개(Catalog · InputMap · AudioEvents · DataTable · ProjectSettings · GlobalVariables · UIPreview · AudioGraph)는 단일 문서 기반으로 옮기지 않는다 — 이미 `IEditorPanel` 의 저장 규약(`saveDocument` · `revertDocument` · `canSaveDocument` - Ctrl+S · 닫기 확인)을 함께 쓰고, 여러 파일 · 페이지를 다루는 창에 "문서 하나" 정체성을 지어내야 한다. 언리얼도 에셋 편집기(`FAssetEditorToolkit`)와 설정 창(디테일 뷰)을 나눈다.
- **결정(2026-10-31)**: `AssetPaths` 의 공개 해석은 `toDiskPath`(리소스 id → 자리) · `findLooseFile`(있는 낱개 → 자리) · `toAssetID`(자리 → id) · `collectLooseFolders`(나열) 와 설정(`setSearchPriority` · `mountDiskDomain`). 개발 명령의 디스크 경로 인자(`readDiskPathArgument`)는 글로 둔다 — 받는 쪽 대부분이 글을 받고, 저장 함수로 갈 때만 `FileUtil::requireAbsolutePath` 로 형식에 올린다. 문서 · 직렬화기의 `saveFile`(XML · JSON · Archive)은 `FileUtil` 과 같은 층의 원시 연산이라 글을 받는다(OS 경계가 상대 경로를 거절한다).

### T3. 읽기 · 쓰기 분리 (R4 · R7)
- 엔진 저장 입구: 모든 `save*` 는 `save*(AbsolutePath)`. 지금 같은 이름 `saveToFile(string_view)` 가 형식마다 리소스 id(Material · MaterialInstance · Prefab · TileMap · `getWritePath` 경유)이거나 디스크 경로(Skeleton · AnimationClip · AnimationGraph · Heightfield · Sequence · Dialogue · Fracture · NavMesh — 상대 경로면 작업 폴더에 쓴다)다. `InputMap.h:356` 은 "id 또는 절대".
- 원본 위치 칸: `Scene::_sourcePath` · `SceneDocument` · `TileMapXMLData` 의 원본 경로는 지금 불러오면 id, 다른 이름 저장 뒤엔 절대 경로다 → `ResourcePath`. 다른 이름 저장 경계에서 절대 → 리소스 경로, 마운트 밖이면 거절. `SceneObjectReference` 비교가 이 칸에 기댄다.
- 없어지는 것: `toWriteID` · `getWritePath` · `resolveWritePath` · `DomainAssetID`, `toAssetIDFromPathOrID` · `isMountedDiskDomainID`, `makeSaveFolderPath` · `makeSavePath` · `makeUniqueSavePath`(빈 이름을 못 찾으면 있는 경로를 돌려줘 덮어쓴다 — 고유 이름은 `FileUtil::reserveUniquePath` 하나), `findLooseDomainFolder`, `LooseFilePath`.
- 에디터 편집 파일: `EditorSourceFile{ResourcePath, AbsolutePath}` 를 `open(ResourcePath)`(도메인이 비면 이긴 도메인을 채움, 없으면 오류) · `create(ResourcePath — 도메인 필수)` 두 입구로만 만든다. 읽기 · 쓰기는 같은 절대 경로에 `FileUtil`. `EditorDocumentPanel` 이 문서를 받을 때 이것을 정해 `loadDocument` · `saveDocument(const EditorSourceFile&)` 로 넘긴다.
  - 지금 편집 문서 읽기/쓰기가 넷이다: 도구 문서(findLooseFile + 디스크), AssetIO 읽기 + `getWritePath` 쓰기(Material · InputMap · AudioEvent · AudioGraph · Retarget — 팩 구성에서 읽은 것과 쓴 것이 어긋난다), findLooseFile 읽기 + `getWritePath` 쓰기(Catalog · Rig · 입력 재생 · UIPreview · Paired · CharacterEditor), 쓰기에 findLooseFile(Terrain).
  - 포커스 블록 복사 11 벌(Editor 7 · GameFramework 4)과 기반 밖 자체 저장 패널 8 개(Catalog · InputMap · AudioEvents · DataTable · ProjectSettings · GlobalVariables · UIPreview · AudioGraph)를 기반으로 옮긴다. `TileMapPanel` 의 `_pathBuffer` 두 번째 경로와 "두 종류를 받는 입력란"(2026-10-31 고친 `TileMapPanel.cpp:428` 도 이 범주 — 임시 조치)을 없앤다.
  - 존재 확인은 `hasAsset` 이 아니라 읽기와 같은 해석(R7). 입력 재생 파일도 편집 파일이다(에디터가 같은 파일을 쓴다).
- 런타임 내용 읽기는 `AssetIO`: 테마 토큰, SQL 마이그레이션 목록(`isDirectory` 추측 · 런타임 `findLooseFile` 제거), 외형 데이터 감시(→ DevTools `watchAssetFolder`), Localization 문서.
- 메시 모디파이어: `AssetPath` 인자를 세 가지로 푸는 것(TextureSampler `exists` 추측 · WorldWarp · PointCache `FileUtil` 직접 · 세션의 쓰기/읽기 도메인 불일치)을 `MeshModifierContext::readAsset` 하나로.

### T4. 쿠킹 입력 종류 (R3 · R6) — T2 · T3 보다 먼저
- `CookInput` 한 칸(`_assetID`)이 지금 리소스 id · 도메인 없는 키 · 루트 밖 절대 경로 · 목록 폴더 · `..` 원문 폴백 다섯을 담고, `computeCurrentHash` 가 모두 `findLooseFile` 로 다시 푼다. 절대 경로 분기를 지우면 디스크 · 목록 입력의 해시가 조용히 0 → **매번 재베이크(오류 없음)**.
- `CookInput{ Resource(ResourcePath) | DiskFile(AbsolutePath) | Listing(AbsolutePath, 확장자) }`, `computeCurrentHash` 종류별, `CookRecord` 형식 · 쿠커 버전 올림. `PackCooker::addFile` 은 `AssetIO::readBinaryFile` 위에 계측만, `DDSLoader::loadFromDiskFile`(사용 0) 삭제.

### T5. 셰이더 소스 계층 (R1 · R3 · R6 · R8) — 절대 경로 오류 전환보다 먼저
- 지금: `ShaderCompileDesc._filePath` 에 런타임은 id, 쿠커는 절대 경로를 넣고 `ShaderCompiler.cpp:779` · `ShaderCookDriver.cpp:188` 이 `FileUtil::exists` 로 먼저 추측한다(작업 폴더의 같은 이름 파일을 컴파일할 수 있다). 소스 읽기 넷(findLooseFile + FileUtil · 쿠킹본 AssetIO · DXC `LoadFile` + 기본 include · FXC include), include 루트 해석 셋(붙인 도메인 헤더가 스탬프 · 가림 검사에 안 보인다), "소스 → `shaders/bin/<rhi>`" 규칙 세 벌(글자 `shaders/` 검색), 디스크 캐시 둘, DXC → FXC 폴백(DXIL 포함 — 원인을 가린다), 바이트를 그대로 넓히는 UTF-16(`:708`, 한글 경로 깨짐), 쿠킹본 정책에서 메모리 캐시가 늘 빗나간다(`ShaderCache.cpp:266`).
- 목표: `ShaderSourceID`(리소스 경로) → 해석기 한 곳 → `ShaderCompileInput{절대 소스, 절대 include 루트}`. `ShaderIncludeResolver` 하나를 DXC · FXC include 처리 · 쿠킹 스탬프 · 가림 검사가 함께 쓴다(DXC 는 `DxcBuffer` + 사용자 include handler). `ShaderBinaryLocation::make(ShaderSourceID, rhi)` 순수 함수. `IShaderBytecodeProvider` 를 `Cooked` · `Live` 로 나눠 기동 때 하나만 건다(`areEngineServicesBound` · 정책 분기 다섯 제거). DDC 하나. 언리얼 `AddShaderSourceDirectoryMapping` 과 같은 자리.
- **끝(2026-10-31, 빌드 · 테스트 전 — 통합이 확인)**: `ShaderCompileDesc._source` 는 `ShaderSourceID`(쿠커도 리소스 id), `ShaderSourceResolver` → `ShaderCompileInput`, `ShaderIncludeResolver` 를 DXC · FXC · 스탬프 · 가림 검사가 함께 씀(`findSourceFile` · `findShadersDirectory` · `collectShaderIncludeDirs` · 쿠커/스탬프의 resourceRoot 인자 삭제), `ShaderBinaryLocation::make`(shaders 칸 판정), `IShaderBytecodeProvider` Cooked · Live(기동 때 하나, 쿠킹본 메모리 캐시 적중), DDC 는 `Saved/ShaderCompileCache` 하나(`Saved/ShaderCache` 결과 캐시 삭제). UTF-16 은 DXC→FXC 폴백 삭제(c633ba320c)와 함께 이미 `utf8ToUTF16` 만 남았다. 쿠킹 판정(`verifyCookedShaders`)도 `ShaderTargetFormat` 을 받아 `ShaderBinaryLocation::makeForDomain` 으로 자리를 정한다(소스 · 도메인 두 입구가 내부 함수 하나를 쓴다 — 글 `shaders/bin` 잇기 0). 남은 것: §5 의 셰이더 확인(캐시 폴더를 지운 ctest · 쿠킹본 메모리 캐시 적중 수 · `ShaderBinaryLocation` 표 테스트).

### T6. 저장 파이프라인 하나 (R4 · R5 · R6)
- 게임 상태 스냅샷(`GameInstance::saveStateToFile` — `DataPath` 를 디스크 경로로 · 작업 폴더 기준), `Autosave`(`saves` · `.info` 직접 쓰기), 에디터의 사용자 외형 프리셋 · 썸네일(콘텐츠 트리에 봉투 없이)을 모두 `ILocalStore` 슬롯으로. `UserDataPath::resolve` 의 추측은 `LocalStoreLocation{Memory, UnderUserData, Absolute}`.
- 온라인 서버 키트: 옵션 필드에 `Kind::FilePath`(readOptions 경계에서 절대화, 못 풀면 오류). `NetSecurity::makeProjectPath` 폴백 · `ensureDevCertificate` 작업 폴더, Chat `bannedWords` 작업 폴더 + 없으면 필터 없이 동작, 제공자 JSON 읽기 세 벌 → 하나. `ConfigManager.h:393` 의 세 후보 찾기(작업 폴더 → 프로젝트 → Bin)도 같은 모양.
- **진행(2026-10-31)**: 끝난 것 - 키트 옵션 파일 경로 칸(`std::optional<AbsolutePath>`, `readOptions` 경계에서 프로젝트 루트 기준으로 풀고 못 풀면 오류), 제공자 파일 읽기 하나(`OnlineProviderFile::read` - 계정 · 경제 · 라이브 운영, 키트는 `readEntry` 만), `NetSecurity::resolveProjectPath`(못 푼 경로는 오류 - 개발 인증서로 물러나지 않는다), `ensureDevCertificate` 는 이미 `ProcessRoots::_derivedData`, `ConfigManager` 는 프로젝트 루트 한 곳(남은 옛 주석 · 오류 글 정리), `LocalStoreLocation`(데이터는 사용자 데이터 폴더 아래 상대 경로만, 바닥은 열 수 없는 종류면 오류, SQLite `:memory:` 는 그 바닥만). 채팅 `bannedWords` 는 `none` 값이 있어 글 칸 그대로(경계에서 `tryResolve` - 앞 커밋).
- **끝(2026-10-31, 빌드 · 테스트 전 — 통합이 확인)**: 런타임 저장소의 주인은 게임 범위 서브시스템 `GameLocalStore`(`GameInstance` 가 소유, 파일 백엔드를 사용자 데이터 폴더 `saves` 아래에 열고 봉인 문맥은 게임 범위 `INetSecurityProvider` + 장치 키 `device.key`). 완료는 요청마다 단 콜백을 요청 id 로 나누는 한 곳이고, 요청은 구독을 돌려준다(주인이 먼저 사라지면 콜백이 불리지 않고, 끝난 요청의 구독은 꺼진다 - Core `SubscriptionSource::detachSubscription`). 종료 전 단계는 맡긴 일과 콜백이 잇는 일이 바닥날 때까지 완료를 넘긴 뒤 닫는다. `saveStateToFile` · `loadStateFromFile`(경로) → `saveGameToSlot` · `loadGameFromSlot`(슬롯 이름, 완료 뒤 `update` 가 `SaveGameSavedEvent` · `SaveGameLoadedEvent` 를 낸다 - 칸 `_slot`), `GameSettings::_defaultSavePath` → `_defaultSaveSlot`. `AutosaveManager` 는 상태 슬롯 `autosave/slot<n>` 과 기록 슬롯 `.info` 를 맡기고(델리게이트는 바이트를 만들고 되살린다, 기록 읽기 동안 저장은 기다린다, 상태 쓰기가 실패하면 그 칸 기록을 지운다), XML `directory` → `group`. **결정**: 언리얼은 플랫폼 `ISaveGameSystem` 위에 `AsyncSaveGameToSlot`(완료 델리게이트), 유니티는 앱 수명 `persistentDataPath` 다 - 봉투 · 백엔드가 GameFramework 에 있어 엔진 범위에 둘 수 없고 슬롯 폴더가 게임마다 달라 게임 범위에 둔다. 백엔드는 코드가 `file` 로 고른다(SQLite 키트를 gamesettings 로 고르는 칸은 쓰는 게임이 생길 때). 완료 배분은 기반 `LocalStoreChannel`(GameFramework `Online/Local`)로 뺐고 `GameLocalStore` 는 그것을 든 게임 범위 서브시스템이다.
- **에디터 쪽 끝(2026-10-31)**: 에디터 사용자 외형 프리셋 · 썸네일의 주인은 캐릭터 편집 확장의 에디터 서브시스템 `CharacterEditorLocalStore`(`SW_EDITOR_SUBSYSTEM`, 같은 기반 `LocalStoreChannel`, 활성 게임의 사용자 데이터 폴더 아래 `editor`, 봉인 없음 - 완료는 에디터 프레임 단계가 넘긴다). 프리셋 목록은 슬롯 `charactereditor/presets`(쓰기가 끝나야 목록 · 저장 상태가 바뀐다), 썸네일은 슬롯 `charactereditor/thumb.<이름>`(PNG, 프리셋 판 4 가 경로 대신 슬롯 이름을 든다). 콘텐츠 트리의 `editor/characterpresets.txt` · 옆 PNG 쓰기와 경로 칸은 없앴다. **결정**: 에디터는 게임 인스턴스 밖이라 게임 범위 `GameLocalStore` 를 쓸 수 없고, 사용자 자료라 콘텐츠가 아니다 - 언리얼은 에디터 사용자 설정을 `Saved/`, 유니티는 `EditorPrefs` 에 둔다. 쓰는 확장이 하나라 그 확장이 주인이고, 둘째가 생기면 공용 에디터 플러그인으로 옮긴다. 스크린샷 창구가 파일로만 내놓아 캡처는 사용자 데이터 폴더의 임시 파일을 거친다(창구가 바이트를 내면 바로 받는다). `AccountDeviceSecret` 은 부르는 곳이 0 이라 지웠다(게스트 장치 비밀은 게임이 만들어 `guestLogin` 에 넘긴다).

### T7. 같은 일 하나로 (R8)
- 헤드리스 작업 표 둘(Engine `enterEditorToolTask` · App 표 · 크래시 리포터 인자 재읽기) → `HeadlessTaskRegistration{인자[], 실행}` 하나.
- 게임 카탈로그 읽기 둘 → `GameInstance::loadRequiredCatalog` 하나(디렉터 컴포넌트의 자체 사본 · WARNING 폴백 제거 — HarvestValley · NileCity · StarSkirmish · ThemePark · WildWest).
- 지수 백오프 셋 → Core `ExponentialBackoff`. 토큰 나누기 둘 → `Core/String` 하나. 루프백 판정 · 포트 7100 상수 하나.
- 에디터: 게임 팩 나열 셋, 현지화 프로젝트 모으기 둘, 저장소 루트 셋, 파일 글 되돌리기 둘, `.meta` 쓰기 둘 → 각각 하나.
- `isInInactiveGamePack` 복제(VAT 쿠커 ↔ SceneCooker) → 함수 하나.

- **끝(2026-10-31)**: 쿠커의 `isInInactiveGamePack` 셋 → `AssetCookPath`, 백오프 둘 → `Core/Time/ExponentialBackoff`, 따옴표 낱말 나누기 둘 → `StringUtil::splitQuotedTokens`, 루프백 판정 다섯 → `NetAddress::isLoopback`, 포트 7100 셋 → `OnlineServiceHostConfig::kDefaultPort`, 게임 팩 나열 일곱 → `AssetPaths::collectGamePackDomains`, 현지화 프로젝트 모으기 둘 → `LocalizationTools::collectProjectPaths`, 저장소 루트 사본 → `ProcessRoots::_project`, 헤드리스 작업 표의 에디터 도구 인자 → `kArrEditorToolArgument`. **결정**: 디렉터 컴포넌트의 카탈로그 읽기는 컴포넌트 칸의 경로로 씬마다 다른 데이터라 게임 범위 `loadRequiredCatalog` 로 옮기지 않고 실패 등급만 ERROR 로. 파일 글 되돌리기 둘(프리팹은 캐시를 바로, 리그는 파일 감시가 다시 읽는다) · `.meta` 쓰기 둘(만들기 · 옮길 때 고쳐 쓰기)은 뒤따르는 일이 달라 합치지 않는다.

### T8. `#if` → 빌드 단위 (R9 · R6)
- `App.cpp` 의 `#if !defined(SW_SHIPPING)` 11 곳 → 개발 도구 함수 선언 하나(Dev 구현 파일 · Shipping 빈 구현 파일을 CMake 가 고름 — 빌드마다 고정이라 인터페이스 · 가상 함수를 두지 않는다, `docs/agents/CppStyle.md` 의 상속 규칙). `AppAutomationHost` 두 구현 → 두 파일, 자동화 없는 빌드에서 `-scenario` 를 받으면 기동 오류(지금은 로그만 남기고 계속).
- 키트의 개발 대체물(`LoginTicketKey` 개발 키 폴백, LiveOps 가짜 푸시, Account 가벼운 해시 · 테스트 계정, Economy 개발 검증기, NetSecurity 개발 인증서) → DevOnline 빌드 단위가 키트 등록표에 확장점으로 올린다. 키트 코드의 `#if SW_SHIPPING` 0, 값이 없으면 오류.
- 오디오 · 접근성의 `#if` 팩토리 → RHI 와 같은 등록표. `FileWatchDispatcher` 의 플랫폼 `#if` + mtime 폴백 정리.
- **코드 끝(2026-10-31, 빌드 · 검증은 통합에서)**: `AppDevTools.h`(구현 `AppDevTools.cpp` · `AppDevToolsShipping.cpp`) · `AppAutomationHost.cpp` · `AppAutomationHostOff.cpp` 를 `sw_pickAppVariant` 가 고른다. 개발 대체물은 `*DevOnline.cpp`(`SW_DEV_ONLY_SOURCE_PATTERNS`)가 `DevOnlineExtension` 에 올린다. 오디오 · 접근성은 `AudioBackendRegistrar` · `UIAccessibilityBridgeRegistrar`, 파일 감시자는 `IFileWatcher::createNative`(플랫폼 폴더). **결정**: 플랫폼 백엔드 파일(오디오 · 접근성) 안의 파일 전체 `#if` 는 남긴다 — Engine 은 아직 `sw_filterPlatformSources` 를 쓰지 않고 서버 타깃(클라이언트 코드 없음)도 같은 매크로로 가른다. Engine 에 폴더 규칙을 켤 때 함께 지운다.
- **호스트 · 모듈 관리 끝(2026-10-31, 빌드 · 검증은 통합에서)**: 구성별 구현 고르기는 공용 `sw_pickVariantSource`(`cmake/Engine/UnbuiltSources.cmake`)다. 짝 — `ModuleReloadDriver`(매니페스트 해석 · `addOnBeforeReloadBatch` 도 구동기 API) · `GameModuleManagerLinkage`(동적 · 정적 모듈 잇기) · `HostDevTools`(App · Server 공용 개발 도구 연결, `AppDevTools` 는 App 전용 둘만) · `EditorModuleManagerDevTools` · `EditorModuleManagerHeadless` · `ServerDebugControl`, 모두 `<이름>Shipping.cpp` 가 배포 쪽. App 도 배포에서 `SW_DEV_ONLY_SOURCE_PATTERNS` 를 쓴다(`RHIBackendSwitcherDevCommands.cpp`). 배포에서 `-EnableEditor` 는 기동 오류. 배포에서 짓지 않는 단위(Editor · DevTools) 안의 `SW_SHIPPING` 분기는 지웠다.
- **Engine · 모듈 해제 끝(2026-11-01, 빌드 · 검증은 통합에서)**: 값만 다른 구성 차이는 `build::kShipping`(`Core/Foundation/BuildInfo.h`)으로 읽는다 — GPU 진단 장치(브레드크럼 · DRED · 장치 결함 확장 · GPU 단언 · NaN · bindless 검사 · GL 디버그 컨텍스트, 진단 코드는 모든 구성에서 컴파일된다 — 언리얼 `!UE_BUILD_SHIPPING` 의 DRED 와 같은 자리) · `GameSelection::select`(배포는 베이크한 게임 하나) · `ConfigManager::initializeSubsystem`(배포는 베이크한 설정 JSON) · `AppDevTools`. 처음에는 `<이름>Shipping.cpp` 짝으로 갈랐으나 두 파일이 값 하나만 달라 되돌렸다([BuildConfigSwitch](agents/BuildConfigSwitch.md)). `Engine.cpp` 의 `SW_WITH_CLIENT_CODE` 셋은 `Engine.cpp` 끝의 `#if defined( SW_WITH_CLIENT_CODE )` 구역(렌더러 몫 일곱 함수)으로 — 처음에는 `Renderer/EngineRendering.cpp` 로 뗐다가 같은 상태를 나눠 쓰는 클래스 반쪽이라 합쳤다. 모듈 해제(`unregisterModuleTypes` · `destroyComponentsOfModule` · `drainUntilEmpty`)는 레지스트리 연산이라 구성과 무관하게 둔다. `ModuleCompiler` 는 `LiveReload/` 로 옮겨 배포가 짓지 않고, 개발 잇기만 만든다(주인 칸은 `unique_ptr<IModuleCompiler>` — 에디터가 `IModuleCompiler` 로 찾던 것이 구현 타입으로 걸려 못 찾던 결함도 함께). 시험 전역 변수의 기본값이 배포 값과 같은 곳(라이트 컬링 · VFX 시뮬레이션 · 디버그 드로우)과 기본 에셋 · 팩 설정의 파일 있음 검사는 분기를 지웠다(배포도 검사한다). 자동화 단계 되돌리기 등록(`AutomationStageReset`)은 기능이 등록하는 자리라 `Automation/Scenario/` 로 — `Ninja-Debug-NoAutomation` 프리셋(개발 + 자동화 끔)이 에디터째 지어진다.
- **배포 종료 경고 둘**(`Asset cache 'ElementRuleTable' / 'InteractionCatalog' is still registered`): 근본 원인은 엔진이 기동 범위의 컬렉션만 종료(`getCollection().shutdownAll()`)해 매단 자식 범위(모듈 서브시스템 — 배포는 정적 링크라 리플렉션 단계가 `Engine` 이름으로 세운 것이 끝까지 남는다)가 에셋 매니저보다 늦게 내려간 것. `SubsystemScope::shutdownAll` 이 소멸처럼 자식을 먼저 걷고, 엔진은 범위째 내린다.
- **남긴 구성 분기와 이유**:
  - Reflection 메타데이터(`ReflectionMetadata.h` · `Reflection.cpp` · `ReflectionTypes.h` · `ReflectionDocWriter.cpp`, 24) — 구조체 배치가 구성마다 다른 에디터 전용 데이터라 빌드 단위로 가를 수 없다(언리얼 `WITH_METADATA` · `WITH_EDITORONLY_DATA` 도 `#if`). 옮기려면 메타데이터를 옆 표로 떼고 생성기(ReflectionParser 템플릿)가 개발 전용 `.gen` 단위를 따로 내야 한다 — 생성기 · 에디터 읽는 쪽 전부를 바꾸는 일이라 따로 한다.
  - `GameObjectManager::resolveComponent` 의 타입 확인 — 뜨거운 길의 검사(언리얼 `DO_CHECK` 자리)라 함수 호출로 바꾸면 비용이 는다.
  - `LocalizationManager` 의 가짜 언어 숨김 둘 · `UIBindingSet` 의 배포 전용 경고 · `SplashWindow`(에디터 도메인 에셋) · `DiagnosticsSubsystem`(Core 할당 헤더 스위치를 따른다) — 모두 `build::kShipping` 으로 옮겼다(가짜 언어 · 스플래시 · 진단 · 모듈 구성 · UI 폴 경고 · 배포 평문 포트).
  - `GameNetSession` 의 배포 평문 공개 포트 거절 — 개발 허용은 `DevOnlineExtension` 에 올릴 개발 대체물이다(키트 등록표 차례에 함께).
  - 스위치를 정의하는 자리(`ContentSource.h` · `AnimationRewind.h` · `ProfilerBackend.h` · `ModuleCatalog::getCurrentConfiguration`)와 플랫폼 백엔드 파일의 `SW_WITH_CLIENT_CODE`(Engine 이 `sw_filterPlatformSources` 를 쓰지 않는다 — 위 결정)는 대상이 아니다. `SW_PROFILER_TRACE_ENABLED`(ServerApp 1)는 그대로다.
- **끝(2026-11-01)**: Engine 의 `SW_SHIPPING` 은 32 곳 — 리플렉션 메타데이터(툴팁 · 문서 필드)를 배포에서 빼는 자리와 배포 전용 정의 몇 곳뿐이다. 언리얼 `WITH_METADATA` · `WITH_EDITORONLY_DATA` 와 같은 자리라 유지한다(데이터 배치가 구성마다 다르다 — 빌드 단위로 가를 것이 아니다). 모듈 컴파일러는 `IModuleCompiler` 뒤(배포는 nullptr), 자동화 실행기는 엔진 밖에서 include 하지 않는다. 배포의 게임 모듈 타입은 종료가 뗀다(`GameModuleManager::shutdown`).

### T9. 생성 계약 · 도구 (R8 · R6)
- **타입 → 모듈 판정 넷**(파서의 경로 글자 `module_rules` — 런타임이 덮어써 버려지는 데이터, 런타임 `_activeModuleName`, `ContentModules.py` 정규식 — 주석 · 템플릿의 REFLECT 도 셈, CMake 대상)을 하나로: CMake 가 `--module <이름>` 을 넘기고 파서가 대상마다 `ReflectedTypes.list`(FQN · 모듈)를 낸다. `ContentModules` 검사는 정규식을 버리고 이 목록을 읽는 빌드 단계로.
- 생성 계약 글자 네 곳(JSON `emit` · `ParserConfig` 기본값 · CMake 리터럴 · 템플릿) → `ConfigConstants` 표 하나. 자리 표시자 = "스탬프 없음" 하나(생성 글 재읽기 추측 제거), 입력별 스탬프에 구조화된 사실.
- 파서 입력 엄격화: 설정 파일을 작업 폴더에서 위로 찾고 없으면 기본값으로 진행하는 것(`ParserConfig.cpp:24`) → 절대 경로 인자 필수. 템플릿 이름 enum(없으면 시작 실패), 철자 표 · 컨테이너 종류 · 파일 읽기 실패의 "경고 후 건너뜀" → 오류. 공유 스키마 헤더를 `ReflectionSchema` INTERFACE 대상으로(지금 Engine 을 링크하지 않으면서 Engine 헤더를 include).
- OnlineLoadBot: `sw_addExecutable` 로 UTF-8 매니페스트(지금 봇만 빠져 한국어 Windows 에서 인자가 CP949), 서버 조립은 `LoadBot.online.json` + 기존 `OnlineServerConfig` 로더, 부하 값은 Automation `<Load>` 하나(봇 JSON 의 칸 하나 여러 뜻 제거), 명령줄은 `CommandLineManager`, 보고서 쓰기 실패는 종료 코드로, `--tls` 값 없음 = 개발 인증서 폴백 제거, `--local-server` 짧은 시나리오를 CTest 로.

- **끝(2026-10-31, 빌드 · 테스트는 통합에서)**: 모듈은 `sw_addReflectionStep( MODULE )`(필수) → 파서 `--module` → 생성 코드가 찍은 이름 그대로 등록(런타임 덮어쓰기 · `kDefaultModuleName` 제거). 스탬프가 입력의 사실(타입 FQN · Flags · 원본 경로)을 적고 FlagOps 우산 · 이름 충돌 · `ReflectedTypes.list` 가 그것만 읽는다. `ContentModuleCheck` 빌드 단계가 그 목록과 CMake 가 적은 주인(`CONTENT_OWNER`, 게임은 `game:<폴더>`)으로 콘텐츠를 본다. 생성 계약 글자는 `ProjectConstants.py`(→ `SW_*` · `sw::config` · 템플릿 `$Banner`), 자리 표시자 = 스탬프 없음(CMake 가 심을 때 지운다). 파서는 `--config <절대 경로>` · `--builtins` 필수, 설정 필수 키 · 템플릿(`EmitTemplate` 열거형) · 철자 표 줄 · 컨테이너 종류가 틀리면 멈춘다. `ReflectionSchema` INTERFACE. 실행 파일은 `sw_addExecutable` 하나(매니페스트 포함). 봇: `CommandLineManager`, `--load`(`<Load>`), `--online-config`(`LoadBot.online.json`), 보고서 실패 = 4, `--tls` 값 필수, CTest `OnlineLoadBot.localServer`.
- 고정 배치형 진단 목록(`FixedSchemas.gen.txt`)도 스탬프의 `schema` 줄만 읽는다(`.gen.cpp` 표식 줄 삭제).
- **등록자 모듈 이름 끝(2026-10-31)**: CMake 가 대상마다 `SW_REGISTRAR_MODULE`(모듈 · 키트 · 게임 = `MODULE_NAME`, 키트는 강제 포함 헤더 `sw_setTargetDefsHeader( REGISTRAR_MODULE )`, Engine · Core = `Engine`, 호스트 실행 파일 · DevTools · ModuleManager = 대상 이름)을 주고, `SW_GLOBAL_VARIABLE*` · `SW_RESOURCE_DATA_KIND` 가 등록자에 찍는다. `registerPendingVariables` · `registerModuleKinds` 는 이름 인자 없이 등록자의 이름으로 올린다. 동적 모듈을 올리는 길(`registerModuleTypes( 이름 )` · 리로드의 새 이미지 확인 단계)은 `verifyModuleRegistrars` 로 대조해 다르면 오류로 멈추고(경고 삭제), 호스트 이미지는 `registerHostImageTypes`(여러 대상이 섞여 대조하지 않는다). 아래는 그 설계 기록이다.
- **설계(기록)**: 전역 변수 · 데이터 종류 등록자는 자기 모듈을 모른다. 정적 초기화가 Engine 의 전역 머리 하나에 매달고, 올리는 쪽(`registerModuleTypes( moduleName )`)이 그때 걷은 머리 전부를 **자기가 올린 이름**으로 등록한다 — 지연 로드로 늦게 올라온 공용 모듈의 등록자가 남의 이름으로 들어가 그 모듈을 내릴 때 해제되지 않는다(지금은 WARNING 하나). `GlobalVariableRegistrar::_pModuleName` 은 늘 빈 글로 채우는 죽은 칸이다. 목표: 답을 아는 CMake 가 대상마다 `SW_REGISTRAR_MODULE="<모듈 이름>"`(타입 생성기의 `--module` 과 같은 값 — `sw_addModuleEngineStamp` 의 `MODULE_NAME`, Engine · Core 는 `Engine`)을 컴파일 정의로 주고, `SW_GLOBAL_VARIABLE*` · 데이터 종류 등록 매크로가 그 값을 등록자에 찍는다(정의가 없으면 컴파일 오류 — 폴백 없음). `registerPendingVariables` · `registerModuleKinds` 는 등록자의 이름으로 올리고, 올리는 쪽 이름과 다르면 오류로 멈춘다(경고 삭제). 배포(정적 링크 하나)에서도 이름이 맞는다 — 지금은 모두 `Engine` 으로 들어간다. 언리얼은 `UE_MODULE_NAME` 을 UBT 가 대상마다 정의한다. 손댈 곳: 정의를 주는 CMake 함수(모듈 · 키트 · 게임 · 테스트 실행 파일 · Server), 매크로 둘, 등록 두 함수, 테스트의 직접 `registerVariable` 호출.
- **결정**: `ReflectionSchema` 는 include 경로로 막지 못한다(Core 가 `Source` 루트를 준다) — 목록 밖 Engine 헤더 include 를 막으려면 루트를 층별로 나눠야 한다.

### T10. 폴백 목록 (R6 — 각 주제와 함께 지운다)
`AutomationScratch::allowWrite` fail-open · `PackManager` 우선순위 기본값 · RHI 백엔드를 OS 기본으로 조용히 바꿈 · 스크린샷 대상 폴백 사슬(골든이 다른 그림을 볼 수 있다) · 머티리얼 폴백 버퍼를 `ensure` 네 곳이 지키는 불변식 · 미선언 렌더 타깃 · DXC→FXC · `ModuleCompiler::findBuildDirectory`(build.ninja 없으면 Bin) · `getBinaryDirectory`(TestBin 그대로) · `getExecutablePath` 빈 글 · `EditorToolAssetCommands::resolveExistingOrRelativePath` · `LocalizationTools::resolveAbsolutePath` · BehaviorTree `diskPath = path` · `EditorBakeOutputPath` · `EditorPaths::getProjectRootPath` · `resolveProjectRelativePath` · 빈 도메인 루트 이어 붙이기(`EditorSelfTestPanelCases.cpp:76` 은 그 경로를 `removeDirectory` 한다 — 작업 폴더 폴더를 지울 위험) · Chat 금칙어 · `ConfigManager` 세 후보 · 파서 설정 기본값 · 봇 `--tls`.

- **끝(2026-10-31)**: `resolveProjectRelativePath` 삭제(디스크 경로 gv 는 받을 때 이미 절대, 상수는 `AbsolutePath::tryResolve` + `ProcessRoots::_project`, 프로젝트 설정 목록은 설정 테이블이 읽은 절대 자리를 넘긴다), `EditorBakeOutputPath` 는 (씬 id, 이름) → 출력 id 순수 함수(`.scene.xml` 이 아니면 오류), 같은 모양 둘(`EditorTracyLauncher::findViewerPath` 의 빈 루트 · `BugReportModel::makeAbsolutePath`) · 팩 `<Engine>` 페이지의 빈 루트 이어 붙이기. `EditorBakeOutputPath` 다시 봄(부르는 곳 둘 — 라이트 · 반사 프로브): 빈 결과는 두 곳 다 오류로 멈추고 디스크 자리는 `toDiskPath`(절대 · 없는 상대 키는 오류)라 폴백 · 추측이 남지 않았다.
- **끝(2026-11-01)**: `ShaderCooker::getSubfolderForFormat` 의 "모르는 형식 → 기본 백엔드 폴더" 와 `getExtensionForFormat` 의 `".bin"` — 표가 모든 형식을 정확히 한 번씩 담는지 컴파일 때 보고, 못 찾는 값(`Count` · 범위 밖)은 `SW_FATAL`. 남은 같은 모양: `RHI::getShaderTargetFormat` 이 모르는 백엔드에 DXIL 을 돌려준다.

## 4. 순서

의존 순이다. 각 단계는 코드 → 린트 → 커밋, 단계 묶음 끝에 빌드 · ctest 한 번(CLAUDE.md 원칙 2). 7 단계 전까지는 동작이 바뀌지 않아야 한다.

| 단계 | 할 일 | 먼저여야 하는 이유 |
|---|---|---|
| 0 | **끝(2026-10-31)** `AbsolutePath`(`Core/Path`) + 엄격한 판정, `ProcessRoots`(`Core/Process`), `ResourcePath` · `DomainTable` 형식만(아직 안 씀) | 나머지 모두의 기준 |
| 1 | **끝(2026-10-31, Linux 는 문법 검사만)** T1 OS 경계 하나(네이티브 · 우회 지점 · 라이브러리 입구 · 오류 모델) | R2 검사를 한 곳에 둘 자리 |
| 2 | **끝(2026-10-31)** 입력 경계 형식(명령줄 `CommandLineDiskPath` + 틀린 인자는 기동 오류 · 개발 명령 `readDiskPathArgument` · `readSourceFileArgument` · `gv_` 는 `GlobalDiskPath` — 셋 모두 `AbsolutePath::tryResolve` 하나) + Saved 생산자를 `ProcessRoots`(`_saved` · `_derivedData`) 로. **결정**: 시나리오 단계 속성마다 경로 종류를 선언하지 않는다 — `{out}` 은 명령 글 안에서도 쓰여 속성 형식으로 막을 수 없고, 경로를 받는 명령이 형식을 검사한다(언리얼 자동화 스펙도 속성에 형식이 없다) | 상대 경로가 들어오는 문을 닫는다 |
| 3 | **끝(2026-10-31)** 조사 로그: OS 경계에서 상대 경로 입력을 기록 → 전체 ctest + **작업 폴더를 바꾸고 상대 경로 인자로 띄우는 CTest**(러너는 절대 경로만 넘겨 이 경우를 못 모은다) | 위반 지점 목록 |
| 4 | **끝(2026-10-31)** T4 쿠킹 입력 종류 | `findLooseFile` 절대 분기를 지우면 조용히 늘 재베이크 |
| 5 | **끝(2026-10-31)** T5 의 추측 제거(컴파일 입력 형식 · `exists` 추측 두 곳) | 7 단계에서 셰이더 컴파일마다 위반 → 기동 정지 |
| 6 | **끝(2026-10-31)** 작업 폴더 상대 경로 사용처 정리: GameInstance · Autosave · ControlIntentHistory · Chat · NetSecurity · WorldWarp · PointCache · ShooterDirector · 녹화 쓰기 · `EditorScenarioRecordCommands` · `resolveScenePath` 작업 폴더 갈래 · `PortraitBatchUtil` · 콘솔 `screenshot` | 7 단계에서 바로 깨진다 |
| 7 | **끝(2026-10-31)** R2 를 오류로(OS 경계 한 곳) | |
| 8 | **끝(2026-10-31)** T2 리소스 경로 · 팩(팩 형식 2 - 마운트 도메인, 쿠커 설정 해시에 형식 버전) | |
| 9 | **끝(2026-10-31)** T3 읽기 · 쓰기 분리 — 저장 입구 `save*(AbsolutePath)` 44 개, `AssetPaths` 해석 `toDiskPath` · `findLooseFile`(`optional<AbsolutePath>`), 씬 소스는 늘 리소스 id, 문서 패널의 읽을 문서 · 디스크 자리 하나(`acceptDocumentToLoad` · `getDocumentDiskPath`), 런타임 읽기 `AssetIO`. 남긴 것(이유는 아래 결정): `isMountedDiskDomainID`(붙인 디스크 도메인은 팩을 보지 않는다), 자체 저장 패널 8 개 | 8 의 형식 위에서 |
| 10 | T5 나머지 · T6 · T7 · T8 · T9 · T10 | 서로 독립 — 병렬 작업자 가능(커밋하고 멈춤, 통합이 검증) |

## 5. 검증

- 단계마다: `App --test`, ctest(단위 · 에디터 · 시나리오), Release 빌드. 7 단계 뒤에는 Shipping 구성 빌드와 `-cooked-content` 시나리오도.
- 배포 구성(통합에서 한 번): `py -3 -m Scripts shipping-check` — `Ninja-Shipping` · `Ninja-Shipping-Server` · `Ninja-Shipping-NoAutomation` 을 짓고(쿠킹 포함), 배포 App 을 몇 초 돌려 창 닫기로 0 종료, 서버를 정한 틱만 돌려 0 종료, 자동화 없는 App 의 `-scenario` 가 기동 오류인지 보고 로그의 [Error] · [Warning] 을 모아 찍는다. CTest 가 아닌 이유: 프리셋마다 빌드 폴더가 달라 한 ctest 트리에 담기지 않는다.
- 새로 만들 확인(원칙 4 — 손 확인 대신 자동화):
  - 같은 상대 경로가 common · game 두 도메인에 있을 때 domain=비움 / common / game 으로 각각 읽기 · 편집 왕복(연 도메인 파일만 바뀐다).
  - 패치 팩을 올린 뒤 도메인 없는 키 · 있는 키 모두 교체 · 제거가 보인다, dlc 팩 찾기.
  - 기존 콘텐츠 참조 2,007 개가 모두 같은 파일로 풀린다(스크립트).
  - 작업 폴더를 바꾸고 상대 경로 인자로 띄워도 산출물 위치가 같다 / 상대 경로 · `C:foo` · 잘못된 정수 인자는 종료 코드 ≠ 0.
  - 저장 왕복 시나리오가 없는 패널(Catalog · InputMap + 재생 · Material · Sequencer · SpriteClip · BehaviorTree · Rig · Terrain · AudioEvent/Graph)에 `scratch` 사본 저장 왕복 단계.
  - 쿠킹 입력 종류마다 기록 → 원본 수정 → 낡음 판정(`TestCookJob` 확장).
  - 셰이더: 캐시 두 폴더를 지운 ctest, 쿠킹본 정책의 메모리 캐시 적중 수, `--verify-shaders`, `ShaderBinaryLocation` 표 테스트(붙인 도메인 · `myshaders`).
  - 파서: 같은 입력 두 번 → 같은 바이트 · 시각 그대로, 설정 · 템플릿 · 철자 표를 망가뜨리면 종료 코드 ≠ 0.
  - 봇: 비ASCII 경로 실행, `--local-server` 짧은 시나리오.

## 6. 위험

- 쿠킹 기록 · DDC · 팩 형식 버전이 오른다 → 전체를 한 번 다시 굽는다(Debug 텍스처 압축은 장당 20 분 — Release 로). 패치 기준 빌드 기록은 전체 경로라 그대로 쓴다.
- 셰이더 include 기록 형식이 바뀌면 `Bin/Saved` 셰이더 캐시가 한 번 모두 무효(Debug 재컴파일이 길다). `ContentHash::computeFile` 은 디스크 경로 · 시각 캐시라 리소스 경로 지문용 캐시를 함께 설계한다.
- Linux 전용 대소문자 처리는 Windows 빌드가 짓지 않는다 — 도메인 루트 조회 한 곳으로 모은 뒤 WSL 빌드로 확인.
- `{out}` 을 쓰는 에디터 시나리오 약 15 개는 명령 · 칸 종류가 바뀌면 함께 고친다.

## 7. SOLID 점검 — Core · Engine (2026-11-01)

근거: `Scripts/dev/StructureAudit.py`, 정적 함수 이름 겹침 스크립트(Core · Engine `.cpp` 의 `static` 함수 이름이 두 파일 이상), 층 표(`CoreLayers.toml` · `EngineLayers.toml`)와 실제 include · 전방 선언 대조, 큰 파일 읽기. graphify 는 이 질문(중복 · 층)에 범위를 좁혀 주지 못해 쓰지 않았다. 이름이 같아도 하는 일이 다른 것(`appendFloat` 넷 중 셋, `isSpace` 둘, `isNameChar` 둘, `makePerpendicular` 둘, `isLive` 둘, `toTicks` 둘)은 표에서 뺐다.

| 순위 | 종류 | 문제 | 고친 모양 | 상태 |
|---|---|---|---|---|
| 1 | 층 역류(DIP) | `SceneEngineServices`(5 층)가 `PhysicsSystem*` · `IAudioService*`(6 층)를 칸으로 들었다 — 층 검사는 include 만 봐서 전방 선언으로 우회됐다 | 칸을 지우고 기능 씬 서비스가 묶음의 엔진 범위에서 타입으로 찾는다(`locateSubsystem<T>`, `WidgetComponent` 가 쓰던 방식 하나로 — 언리얼 `UWorld::GetSubsystem<T>`) | 끝 |
| 2 | 같은 일 둘(R8) | JSON 문자열 이스케이프 넷(`JSONWriter` · `JSONDocument::escapeString`(nlohmann, 잘못된 UTF-8 이면 예외) · 로그 싱크(잘못된 UTF-8 을 그대로 흘림) · 테스트 실행기(탭 · 제어 문자를 그대로 — 깨진 JSON)) | Core `JSONStringUtil` 하나(조각 함수로 받아 `JSONWriter` 고정 버퍼도 할당 없음). `JSONDocument::escapeString` 삭제 | 끝 |
| 3 | 같은 일 둘(R8) | XML 이스케이프 둘(`XMLWriter` · `XMLDocument::escapeString` — `'` 는 바꾸고 탭 · 줄바꿈 · 잘못된 UTF-8 은 그대로) | `XMLWriter::appendEscaped`(정적) 하나, `unescapeString` 이 짝인 문자 참조(`&#9;`)를 읽는다(전엔 에디터 카탈로그 왕복이 깨질 모양) | 끝 |
| 4 | 같은 일 둘(R8) | 16진 글자표 넷(`HexUtil` · `HexDigitUtil` · `StringUtil::escapeInvalidUTF8` · `JSONWriter`), `CookRecord` 의 `Fmt` 손 hex | 글자 ↔ 니블은 `HexDigitUtil`(티어 0) 하나, `HexUtil` 은 그 위 편의만. `HexUtil::tryReadDigit` 삭제 | 끝 |
| 5 | 같은 일 둘(R8) | `to_chars` 감싸기 다섯 · `to_string` 몸체 둘(실패하면 `formatstring` 폴백) | `StringUtil::appendNumber` 하나 | 끝 |
| 6 | 같은 일 둘(R8) | 소켓 주소 변환 셋(`PlatformSocketUtil` · IOCP · epoll), 초 → 나노초 식 아홉 | `NativeSocketAddress`(소켓 구현 파일 전용 헤더), `MonotonicClock::secondsToNanoseconds` | 끝(Linux 는 문법만 — Windows 빌드가 epoll 을 짓지 않는다) |
| 7 | 같은 일 둘(R8) | 내비 `toFloat3` 둘(Core `float3( const float32* )` 있음), Rig `computeAngleBetween`(= `quaternion::getAngleBetween`), Telemetry `countLines` 둘 | Core 함수로, `StringUtil::countChar` | 끝 |
| 8 | 층 역류 | `AssetCacheBase` 리로드 함수가 `IRHIDevice*`(2 층 → 4 층)를 받았다 | **끝(2026-11-01)** — 리로드 함수 · 캐시 여덟의 `reload` 는 에셋 id 만 받고, GPU 자원을 가진 캐시(텍스처 · 머티리얼 · 메시)는 `SceneRHISubsystem` 이 붙이고 떼는 디바이스(`bindDevice`)를 쓴다(백엔드 교체 때 떼고 다시 붙여 죽은 포인터가 없다) | 끝 |
| 9 | 층 역류 | `ReflectionTypes::_addComponent`(1 층이 `Component*` · `GameObject*` 를 안다), `FixedLayout` 의 `friend class NetMessageChannel` | **끝(2026-11-01)** — 생성 칸은 불투명(`void* (*)( void* )`)이고 코드젠이 Object 층의 연결 함수(`GameObject::addComponentForReflection<T>`)를 채운다. 부르는 두 곳(이름으로 만들기 · 에디터 기본 객체)이 형을 되돌린다. `friend class NetMessageChannel` 은 전방 선언 이름 하나라 둔다 — 패스키로 바꿔도 같은 이름을 알아야 하고, 스키마 머리 없는 읽기 · 쓰기를 그 채널만 부르게 하는 것이 목적이다 | 끝 |
| 10 | 한 층 두 모델 | 바이트 스트림 둘: Engine `BinaryStreamWriter/Reader` · Core `ByteWriter/Reader`, `RawAnimationCodec` 도 호스트 순서 | **끝** — Core `ByteWriter/Reader` 하나(가변 정수 · 길이 붙은 글/바이트 · POD · 자리 덮어쓰기를 더하고, POD 복사는 `__BYTE_ORDER__` static_assert 로 LE 호스트만), Engine 쪽 삭제 | 끝 |
| 11 | 게이트 없음 | Core 티어(`CoreLayers.toml`)는 낮은/높은 둘로만 지어 티어 사이 역류를 빌드가 막지 않았다 | **끝(2026-11-01)** — Log 아래 · 위는 빌드(낮은 부분의 `#error` 껍데기), 높은 티어끼리는 커밋 린트(`Scripts/lint/CoreTiers.py`)가 직접 include 를 본다. 높은 티어 여섯마다 빌드 부분을 지으면 부분 · pch 가 늘어 빌드가 느려진다 — 형식으로 싸게 막을 수 없는 틈이라 게이트를 둔다. 지금 위반 0 건 | 끝 |
| 12 | ISP | `INavMesh`(순수 가상 ~23 — 굽기 · 링크 · 장애물 · 질의 · 군중 · 디버그), `IProfilerBackend`(12 — CPU · 메모리 · GPU) | 남김: 구현이 하나씩이라 지금 비용은 없다. 두 번째 구현(내비 백엔드)이 올 때 Physics 처럼 질의 · 편집 · 디버그로 가른다 | 남김 |
| 13 | OCP | `BindingKind`(10 종) switch 가 3 파일 · case 66(평가 · 리맵 직렬화 · 글리프), 자동화 · UI · 녹화에 몇 개 | 남김: switch 는 모두 `-Werror=switch`(+`covered-switch-default`) 아래라 종류를 늘리면 빠진 자리가 컴파일 오류다 — 함수 표로 바꿔도 얻는 검사가 없다. 도형 삼각화는 `PhysicsShapeMesh` 하나로 | 남김 |
| 14 | SRP(파일) | `TaskManager.cpp` 1540 · `NetEndpoint.cpp` 1337 · `StringUtil.cpp` 1297 · `MemoryProfiler.cpp` 1009 줄 — 절 머리글로 이미 나뉘어 있다 | 남김: 같은 클래스의 파일 나누기라 동작 이득이 없다(병렬 그룹 · 대기 · 워커, 핸드셰이크 · 수신, 인코딩 · 파싱, 누수 창 순으로 나눌 수 있다) | 남김 |
| 15 | 같은 일 둘 | Wayland xdg 리스너 둘, MemoryTag 이름표가 `Diagnostics`(enum 은 `Memory`) | 이름표는 끝(2026-11-02 — 열거형과 이름 표가 `SW_MEMORY_TAG_LIST` 하나에서 나온다). Wayland 리스너는 Linux 빌드에서 | 다음 | float3 성분별 곱 12 곳은 `float3::multiply`, 나눗셈 둘은 뜻대로 `Pose::computeScaleRatio` · `RigPoseBuffer::removeParentScale`, CRC32 는 `HashUtil::computeCRC32`(2026-11-01).
| 16 | 이름 | 호스트 창구 헤더와 서비스 순서 표의 겹침, `Engine/FileSystem` ↔ `Core/File`, 관측 넷 | **끝(2026-11-01)** — 호스트 창구는 `Engine/Module/HostInterfaces.h`, 콘텐츠 주소 · 읽기 폴더는 `Engine/Content`. 관측 넷은 두기로 했다 — 역할이 다르다(Core `Diagnostics` 크래시 · 호출 스택 · 교착, `Profiling` 프레임 프로파일러, `Observability` 지표 · 디버그 채널, `Telemetry` 바깥으로 보내기 — 언리얼 HAL 크래시 · Stats · Analytics 와 같은 갈래) | 끝 |

- **근본 원인(한 줄)**: 바닥 도우미가 찾기 어려운 자리(Foundation · Container · TU 전용 Internal)에 있어 호출부가 사본을 만들었고, 층 검사는 include 글만 봐서 전방 선언 칸 · 함수 포인터로 역류가 숨었다. 같은 모양이 남았을 곳은 표의 8 · 9 · 15 다.

## 8. SOLID 점검 — GameFramework · Editor · App

2026-11-01. 범위: `GameFramework`(Runtime · Kits · Plugin/Editor) · `Editor` · `App` · `ModuleManager` · `DevTools` · `Games`(Core · Engine 내부는 다른 작업자). 방법: `Scripts/dev/StructureAudit.py`(폴더 크기 · 모호한 이름 · 정적 전용 타입), 영역별 정독 셋(키트 · 게임 / 편집기 패널 / 호스트 · 모듈 관리). graphify 그래프는 작업 트리에 없어 쓰지 않았다.

### 8.1 우선순위 표

장기 이득(같은 결함이 다시 나올 자리 수 × 결함의 무게) 순이다. "끝" 은 이번에 고쳤다.

| # | 문제 | 근거 | 조치 · 상태 | 상용 엔진 |
|---|---|---|---|---|
| 1 | 문서를 고치는 창이 문서 기반을 우회 — 편집 유실 | `AudioGraphPanel` 이 `IEditorPanel` 을 바로 상속해 Ctrl+S · 닫기 확인 · 되돌리기가 편집을 못 봤다 | **끝** — `EditorDocumentPanel`(`.audiograph.xml` 만 받음), 모델은 `takeEdit` 만 넘긴다 | 언리얼 FAssetEditorToolkit 저장 · 트랜잭션 |
| 2 | App · Server 기동이 복사된 줄 — 이미 갈라짐 | Server 만 `IModuleStateStore` 없음, 모르는 `-gv_*` 를 조용히 버림 | **끝** — 맡김 칸은 `ModuleLoaderSubsystem::initializeHostScope` 가 소유, 서비스 세우기 · 로그 · 경고는 `HostServiceUtil::startAll` | 언리얼 FEngineLoop 하나가 게임 · 서버 대상 |
| 3 | 이름이 내용과 다름 | `GameFramework/Base` 는 기반 층이 아니라 DLL 의 런타임 본체, `Plugin/Editor/Base`(`GF_EditorPlugin_Base`)는 아무도 기대지 않는 잎 확장 | **끝** — `GameFramework/Runtime`, `Plugin/Editor/GameplayTools`(`GF_EditorPlugin_GameplayTools`). 대상 · 내보내기 매크로 · 매니페스트 `GameFramework` 는 그대로 | 언리얼 모듈 폴더 Runtime · Editor |
| 4 | 디렉터 넷의 자동 플레이 빙의 되돌리기 복사 | Shooter3D · VoxelCraft · AbilityArena · HarvestValley, 둘은 `findOrCreatePlayerController` 까지 손으로 | **끝** — `ControlSystem::returnPawnToPlayer` · `isPawnControlledBy` | 언리얼 GameMode `SwapPlayerControllers` |
| 5 | `IEditorPanel` 이 패널 UI 와 문서 의미를 한 인터페이스에 | 가상 16 개 중 문서 6 개가 기본 구현을 가져 반쪽 문서(미저장 표시만 하고 저장을 빠뜨림)가 컴파일됐다 | **끝(2026-11-01)** — 문서는 `IEditorDocumentPanel`(미저장 비트 · 닫기 확인 · `saveDocument` / `revertDocument` 순수 가상)에만 있고, `IEditorPanel` 은 `asDocumentPanel()` 로만 문서를 묻는다(관리자의 저장 · 종료 확인 · 감시 값). 문서 없는 패널은 미저장을 표시할 수 없다. 자체 저장 패널 여덟은 이 기반을 바로 쓴다 | 언리얼 SDockTab ↔ FAssetEditorToolkit 분리 |
| 6 | 모듈 인스턴스 수명이 두 벌 | `EditorModuleManager`(App)가 `GameModuleManager` 의 before/after reload · 이미지 판정 · 실패 · API 묶기 · 인스턴스 만들기/부수기를 다시 적었다 | **끝(2026-11-01)** — 같은 절차는 `ModuleInstanceUtil` 하나(ABI 대조 · API 표 받기 · 리로드 전 이미지 판정 `isLibraryUsable` · 만들기 · 부수기). 남은 앞뒤 · 실패 처리는 뜻이 다르다 — 게임은 상태 스냅샷을 저장 · 복원하고 실패 때 씬 저장을 막으며 정적 링크(배포)를 다룬다, 에디터는 그런 상태가 없다 | 언리얼 FModuleManager 하나 |
| 7 | App · Server 의 종료 순서 · 프레임 본문 두 벌 | 프레임(beginFrame → fixed/update → 리로드 갱신 → 상태 해시)이 두 파일에 | **끝(2026-11-01)** — 겹치던 게임 걸음(고정 스텝 반복 → 갱신, 프로파일 구간)은 `GameModuleManager::updateGameFrame` 하나. 나머지는 실행 파일마다 다른 일이다(App: 에디터 UI · 페이싱 · 사용자 설정 · 백엔드 교체, Server: 서비스 재시작 · 틱 지표 · 드레인). 종료의 `shutdownDependentsOf` 는 모듈 이미지 호스트가 있는 App 만의 일이다 | FEngineLoop::Tick |
| 8 | 파일 감시 넷이 나란히 | `LiveReloadManager` 의 날 감시기, `FileWatchDispatcher` 셋(설정 · 콘텐츠 폴더 · 편집기 애셋), 셰이더는 손 단축키 | **닫음(2026-11-01, 같은 구현 하나다)** — 넷 모두 OS 감시기 추상 하나(`IFileWatcher::createNative` — ReadDirectoryChangesW · inotify)를 쓰고, 접두사 · 확장자 거르기는 `FileWatchDispatcher` 하나다. 뿌리 폴더(설정 · 콘텐츠 · 모듈 폴더)마다 인스턴스가 따로일 뿐이다(언리얼 DirectoryWatcher 도 폴더마다 등록한다). 셰이더는 일부러 수동 재컴파일(Ctrl+F8 — 언리얼 `recompileshaders changed` 와 같다) | 언리얼 DirectoryWatcher 모듈 하나 |
| 9 | 디렉터 상태 바이트 감싸기 복사 | 5 디렉터가 readSection → 표 · 버전 확인을 손으로(FrontierCity 는 버전을 안 봤다) | **끝** — `StateArchiveUtil::readExpectedSection`(표로 갈라 읽는 `SessionStateComponent` 만 `readSection`) | |
| 10 | 자동화 탐침 보일러플레이트 | `findProbeDirector` + `read*` 감싸기 4 게임 | **끝** — `SW_AUTOMATION_COMPONENT_WATCH`(`AutomationComponentWatch::findFirst<T>` · `read<T, 읽기>`), 읽기 함수는 디렉터 참조만 받는다 | |
| 11 | 온라인 키트 서비스 골격 복사 | LiveOps · ServerDirectory 의 다시 읽기 · 감사 쓰기 · 주기 리로드 약 150 줄씩, `makeActor` 셋 | 일부 **끝(2026-11-01)** — 감사 행위자 이름은 형식이 만든다(`ServiceAuditLog::makeAccountActor` · `makeOperatorActor` · `kSystemActor`, 손으로 만들던 다섯 곳). 다시 읽기 일정(요청 · 주기 · 읽는 중)은 `ServiceReloadSchedule` 하나. 쓰기 작업(감사 줄을 함께 넣는 트랜잭션)은 표마다 줄 형식 · 결과 형식이 달라 서비스에 둔다 — 공통 기반은 템플릿 매개변수만 늘린다 | 끝 |
| 12 | 온라인 소도우미 복사 | JSON `readText` 넷, HTTP `isTransientStatus` 둘, `encodeID` 둘 | **닫음(2026-11-01, 같은 일이 아니다)** — `encodeID` 는 저장 형식이 다르고(16 진 글 · 버전 + varint, 합치면 저장된 값이 바뀐다), `isTransientStatus` 는 제공자마다 뜻이 다르다(애플 401 · 스팀 401 · 403). `readText` 는 `JSONValue::findPathText` 의 한 줄 포장이고, 기본값을 받는 것(로그인 제공자 설정)은 종류마다 정한 문서화된 기본이며 필수 칸은 `isValidSettings` 가 따로 본다 | 끝 |
| 13 | 게임 안의 범용 기능 | `FarmSunComponent`(시각 → 해), `ArenaCameraComponent`(궤도 추적), `WildWestHorseComponent`(키트의 `WesternHorse` 소유 · 저장), WildWest 탈것 감시 값 | **끝** — Runtime `DaylightComponent`(디렉터 기반 `readDaylight`) · `FollowCameraComponent`(`readCameraFocus`), 키트 `WesternHorseComponent` + 추상 기반 `WesternDirectorComponent`(카탈로그를 답한다), 감시 값 `Mount.Gait` · `Mount.Speed`(VehicleWatches) · `Western.Stamina`(키트) | 언리얼 SkyAtmosphere 구동은 엔진 쪽 |
| 14 | Runtime 에 장르 기믹 | `Runtime/Gameplay/Gimmick/Genre/*` 약 2.2k 줄 | **닫음(2026-11-01, 두기로)** — 콘텐츠를 보니 이 컴포넌트들은 장르 키트 하나가 아니라 공용 프리팹 라이브러리(`common/prefabs/gimmicks/{shooter,platformer,racing,horror,…}`)가 쓰고, 쇼케이스와 여러 게임이 장르를 가리지 않고 가져다 쓴다. 장르 키트로 쪼개면 공용 프리팹을 쓰는 게임마다 그 키트를 매니페스트에 넣어야 해 결합이 는다 — 언리얼 GameplayFramework · 스타터 콘텐츠처럼 공용 런타임 라이브러리로 둔다 | 끝 |
| 15 | 런타임 키트에 편집기 기능 | `NetSimulation` 의 `NetSimPreviewSceneSource` · `NetSimGameRegistry` | **끝(2026-11-01)** — 미리보기 씬 빌려주기(`NetSimPreviewSceneSource`)는 키트 `Editor/` 로(쓰는 곳이 한 프로세스 재생 탭뿐). `NetSimGameRegistry` 는 게임(Shooter3D)이 런타임에 등록하는 자리라 런타임에 둔다 | 끝 |
| 16 | 큰 패널 · 정적 모음(SRP) | CharacterEditorPanel 1389 줄 · ContentBrowser 1174 · UIPreview 1169 · ProfilerPanel 1120 · InputMapPanel 1015, 정적 모음 몇 | **닫음(2026-11-01, 쪼개지 않는다)** — 패널의 상태는 이미 Model 클래스(ImGui 없는 층)가 들고 패널은 그리기만 한다. 탭마다 클래스 · 파일로 더 가르면 파일 수만 늘고(사용자 판단: 분리가 과하다) 얻는 검사가 없다. 탭이 독립된 상태 · 수명을 갖게 될 때만 그 탭을 뗀다. 사용처가 하나인 도우미는 반대로 그 .cpp 안으로 합친다(`docs/agents/Helpers.md`) | 끝 |
| 17 | 큰 인터페이스 | ModuleAPI `EditorAPI` 함수 칸 22, `IEditor` 순수 가상 16 | **닫음(2026-11-01, 두기로)** — 호스트 ↔ 에디터 모듈 경계의 C ABI 표 하나다(수명 · 프레임 · 입력 · 뷰포트 · 플레이). 역할별 하위 표로 가르면 간접 참조와 ABI 판 변경만 늘고 얻는 검사가 없다 — 호출하는 쪽은 호스트 하나(`EditorModuleManager`)뿐이다. 언리얼 모듈 · 플러그인 경계도 모듈마다 인터페이스 하나다 | 끝 |
| 18 | 층 역류 | ModuleAPI 의 `Export/GameFrameworkAPI.h`(`SW_GF_API` · `SW_KIT_API`) — 계약 층이 위층 이름을 알았다 | **끝(2026-11-01)** — 매크로 정의는 `GameFramework/GameFrameworkExports.h` 로. 계약 층에서 쓰던 곳은 게임 서비스 선언 하나뿐이라 그 매크로(`SW_GAMESERVICE_API`)는 자기 빌드 정의(`SW_GAMESERVICE_EXPORTS` / `IMPORTS`, GF 줄이 준다)로 선다. 키트 · 게임은 원래 `GameFrameworkExports.h` 를 거쳐 받아 include 가 따라가지 않았다 | 끝 |
| 19 | 위젯 우회 | 검색 칸 `InputTextWithHint` 직접, 애셋 경로 글 입력 | **끝(2026-11-01)** — 검색 칸 직접 그리기는 남지 않았다. 캐릭터 편집기의 경로 칸 넷(소켓 · 골격 · 기준 포즈 · 외형 항목)은 `EditorWidgets::drawAssetPathField`(글 입력 + 콘텐츠 브라우저 끌어 놓기, 소켓은 `.sockets.xml` 만), 라벨이 붙은 경로 칸(스프라이트 클립 아틀라스 · 타일맵 경로 `.tilemap.xml`)은 라벨 열(`drawPropertyRowLabel`) + 같은 위젯 | 끝 |
| 20 | Core · Engine 쪽(다른 작업자) | `normalizeOr` 넷(키트) → Core 수학, `SceneNetLink` 찾기 여섯 → Engine 정적 함수, Server 명령 해석 셋 | **끝(2026-11-02)** — `float2` · `float3::normalizeOr( fallback, minLength )`(키트 셋 · 메시 수정자 · 모델 임포트, 문턱은 자리마다 그대로), `SceneNetLink::findFor( component )`, 서버 명령은 이미 개발 콘솔 한 길(`DevConsole::execute`)이다 | 끝 |

키트 사이 장르 로직 중복은 찾지 못했다(라운드 · 차례 · 카운트다운 · 게이지 · 땅 · 격자는 이미 Runtime 하나). 게임 코드가 다른 게임 · 키트가 게임을 include 하는 역류도 없다.

### 8.2 결정

- `GameFramework/Base` 를 `Runtime` 으로: 폴더만 바꾸고 대상 이름(`GameFramework`) · 내보내기 매크로(`SW_GF_API`) · 매니페스트는 그대로다. 언리얼 플러그인의 `Source/Runtime` · `Source/Editor` 와 같은 뜻이고 형제 `Kits/` · `Plugin/` 와 겹치지 않는다. 층을 하나 걷어 Runtime 내용을 `GameFramework/` 바로 아래 두는 안은 키트 · 플러그인과 섞여 버렸다.
- `Plugin/Editor/Base` 를 `GameplayTools` 로: 내용이 행동 트리 · 어빌리티 · 기믹 · 일정 도구이고 다른 확장이 기대지 않는다(기반이 아니다). 형제 폴더(CharacterEditor · MeshModifiers …)처럼 내용 이름을 쓴다.
- 자동 플레이 되돌리기는 디렉터 기반(`GameDirectorComponent`)이 아니라 조종 영역(`ControlSystem`)에 둔다 — 빙의 넘기기는 디렉터가 아니어도 쓰는 일이다. AI 조종자를 찾거나 만드는 쪽은 게임마다 달라(프리팹 · 코드) 그대로 둔다.
- 소리 그래프는 그래프 문서 템플릿(`EditorGraphDocumentPanel<T>`)이 아니라 문서 기반을 쓴다 — 링크 표(`_listLink`)가 없고 연결이 받는 노드의 입력 목록이라 템플릿의 요구를 맞추지 못한다.

### 8.3 큰 클래스의 상태 묶음 (2026-11-01)

크기만 보고 cpp 를 나눈 것은 모두 합쳤고(같은 클래스는 한 파일), 제 상태와 규칙이 있는 묶음만 클래스로 뗐다.

- RTSWorld → `RTSVision`(팀 시야 · 다시 칠하기 타이머) · `RTSUnitBuckets`(이웃 버킷) · 내비 `FlowFieldCache`(목적지별 흐름장 공유 — 다른 키트도 쓴다). 유닛 슬롯은 그대로다: Core `SlotHandleTable` 은 잠금 · 원자값 동시 표이고, 여기는 슬롯 순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 들어간다. 옮기기 전후 같은 동작은 `RTSWorldTest`(공개 조회 요약 고정값)가 묶는다.
- InputMap → `InputTriggerRules`(바인딩 상태 · 문턱 · 홀드 · 탭 · 연타 · 끌기 · 반복 · 펄스 판정 — 언리얼 `UInputTrigger` 자리, 장치 · 레이어를 모른다) · `InputBinding.h`(스키마 형식). 맵은 장치 읽기와 액션 합산만 한다.
- AudioEngine 명령 큐 → Core `LockedSwapQueue`(잠금과 목록을 한 형식에 — 목록을 잠금 없이 만지는 모양을 쓸 수 없다). 재생 id 는 큐에 넣는 잠금 안에서(`pushStamped`). 같은 큐로 DeferredDelegateQueue · ContentBrowserImportQueue · Fracture 맞은 사건도. 손 잠금이 남은 곳(GameObjectStore 시작 대기열 — 잠금 순서 `SW_ACQUIRED_AFTER`, Loopback · FractureNetworkSync · RenderExtensionRegistry · TextureCache — 잠금 안에서 다른 상태도 만진다)은 순수 큐가 아니다.
- AudioEngine 보이스 슬롯 · 잡기 · 반환 · 실제/가상 고르기 → `AudioVoicePool`(언리얼 FMixerSourceManager 자리). 버스 · 감쇠 · 인스턴스 번호 풀이는 엔진.
- ScheduleSystem 빌린 서비스 → `Services` 묶음 하나. 경로 · 자리 제공자는 생성자부터 기본 제공자를 가리켜 null 이 없다.
- 에디터 콘솔 명령 모음(`EditorDevCommands.cpp`) → 기능의 명령 파일 옆(언리얼 Exec 명령이 그 시스템에 있는 것과 같다). 남은 진단 도구 폴더는 `Commands/Diagnostics/` — `/Dev/` 는 빌드 규칙에서 "배포가 짓지 않는 단위" 라는 뜻이다.
- cpp · 헤더 짝: 짝 없는 cpp 는 스스로 등록(탭 · 서버 서비스 · 데이터 종류 · 자동화 단계 · 명령), 플랫폼 · 라이브러리 · 형식별 구현, 모듈 진입점만 남았다. 하나만 include 하던 pimpl 헤더(`MaterialCacheImpl.h` · `TextureCacheImpl.h`)는 cpp 안으로, 빈 cpp(`TypedHandleTestComponent.cpp`)는 지웠다.

## 9. 점검 방법의 빈틈 — 동작 기준 (2026-11-01)

- **놓친 것**: `Material` 을 두 스레드가 고치는 모양(렌더 스레드 초기화 · 인스턴스 `updateRHI`), 게임 스레드의 직접 그리기(`FrameRenderer::execute( scene )`)가 패킷 경로 · 미리보기와
  같은 일을 세 번 하던 것, 테스트 없는 초상화 굽기의 결함 셋(메시 도착 · 등장 디더 · 창 백버퍼 읽기).
- **근본 원인(한 줄)**: 7 · 8 절 점검은 글자(함수 이름 겹침 · include 층 · 파일 크기)만 봤다 — 스레드 소유는 형식에 없어 보이지 않았고, 같은 일은 이름이 달라 겹침 검사에 걸리지 않았고, 실행되지 않는 길은 결함이 드러나지 않았다.
- **바꾼 형식**: 머티리얼은 게임 스레드 객체 + 렌더 프록시(`MaterialRenderProxy`), 다른 스레드의 렌더 일은 `RenderThread::executeAndWait`, 디바이스 프레임(`beginFrame` · `endFrame`) ·
  `GPUScene` · 프록시 진입은 렌더 스레드 역할을 확인한다(Debug). 화면 밖 그리기는 `FrameRenderer::executeOffscreenPacket` 한 길.
- **소유 지도(진행 중, 2026-11-01)**:

  | 객체 | 고치는 스레드 | 모양 | 판단 |
  |---|---|---|---|
  | `Material` | 게임 스레드(객체) · 렌더 스레드(`MaterialRenderProxy`) | 둘로 나뉨(형식) | 끝 |
  | `Texture2D` | 디바이스 스레드가 만들고 렌더 스레드가 스트리밍으로 바꿔 끼움 | 다른 스레드가 읽는 핸들 · SRV 는 원자값, 역할은 함수 머리에 | 유지 |
  | `Mesh` | 게임 스레드 · 워커(`GPUUploadQueue::flush`) 또는 렌더 스레드(GL · 큐 끔 · 큐 없는 캡처 · 동적 메시) | 만드는 쪽은 스냅샷마다 하나(`GPUSceneSnapshot::_bMeshesCreatedBeforeHandoff`) | 끝 |
  | 캡처 · 미리보기 하위 렌더러 | 렌더 스레드만(`executeOffscreenPacket`) | 렌더 스레드 명령 | 끝 |
  | 씬 → 패킷 채우기 | 게임 스레드, 씬마다 `RenderPacketSceneCollector` 하나 | 한 벌 | 끝 |
  | 컴포넌트 훅(`onRegister` · `onPostLoad`) | 게임 스레드만 — 비동기 로드 워커는 생성 · 직렬화만, 씬을 짓는 배치가 훅을 배치 끝으로 미루고 비동기 로드는 `adoptScene` 까지 쥔다 | 형식(`GameObjectManager::beginDeferredRegistration` · `Component::dispatchRegister`) | 끝 |
- **로드 스레드 판단 기준**(언리얼 비동기 로딩: 로딩 스레드 Serialize, 게임 스레드 PostLoad · OnRegister / 유니티 Awake 도 메인 스레드): 워커가 짓는 동안 부르는 코드는 엔진 장부(트랜스폼 계층 ·
  타입 버킷)뿐이고, 컴포넌트가 쓰는 훅은 게임 스레드에서 돈다 — 훅을 쓰는 쪽(게임 모듈 포함)이 스레드를 생각하지 않게 한다. 예전 모양(워커가 훅까지)은 컴포넌트가 데이터뿐이던
  2026-08 의 비동기 로드에 2026-09 등록제 훅이 더해지며 다시 보지 않은 것이다(의도된 근거 없음). 받는 프레임의 등록 비용은 `endDeferredRegistration` 한 곳에서 나눌 수 있다.
- **화면 밖 씬 렌더러 판단 기준**(언리얼 `FScene` · `FSceneRenderer` · RDG): 씬 데이터의 수명은 씬의 수명이고(미리보기 씬이 사라질 때 · 캡처가 끝날 때 내린다 — `OffscreenSceneRenderers`),
  주 렌더러만 하는 일(GPU 시간 보고 · 모듈 확장 · 렌더 타깃 목록 · 고정 PSO 미리 만들기)은 표시가 아니라 주입(`FrameRendererHost`)으로 가른다. 씬마다 렌더러 하나(패스 상수 링 · 그래프 커맨드
  리스트를 씬마다 따로)를 **유지한다** — 한 디바이스 프레임에 여러 씬을 그리므로 링 · 리스트를 나눠 쓰면 뒤 실행이 앞 실행의 상수를 GPU 가 읽기 전에 덮는다(언리얼도 뷰 패밀리마다
  `FSceneRenderer` 를 만들고 RDG 가 실행마다 할당한다). 함께 쓰는 것은 PSO 캐시(같은 파이프라인)이고, 그래프 컴파일은 씬 수명에 한 번이다.
- **GPU 자원 판단 기준**(언리얼 · 유니티 · Godot 비교): GPU 자원은 렌더 쪽이 소유하고 만들고 지우는 길은 하나다. 게임 스레드가 쥐는 모양은 데이터가 바뀌는지로 고른다.
  게임 스레드가 GPU 자원이 생긴 뒤에도 값을 바꾼다 → **렌더 프록시**(렌더 쪽 사본, `Material` / `MaterialRenderProxy` = 언리얼 `FMaterialRenderProxy`).
  바꾸지 않는다(바뀌면 새 객체) → **한 번 만들기**(만드는 쪽 하나, `Mesh` = 언리얼 `FStaticMeshRenderData` + `BeginInitResource`).
  렌더 쪽이 핸들을 바꿔 끼운다(스트리밍) → **게시**(원자값 핸들 · SRV, 게임 스레드는 읽기만, `Texture2D` = 언리얼 `FTextureReference`). 잠금으로 두 생성자를 덮지 않는다.
- **같은 일 지도(끝낸 것)**: 캡처 준비 판정(`CaptureReadiness`), 씬 하나 채우기(`fillSceneView`), 읽은 픽셀 풀기(`RenderTargetImageFile::unpackRGBA8` — 초상화의 `makeRGBA8` 삭제),
  로그 줄 모양 쓰기 · 읽기(`LogLineFormat`), 로그 사건 세기(모든 줄에 id — id 없는 매크로 삭제).
- **점검에 더할 축**: 소유 지도(객체 → 스레드, 렌더러 인스턴스 → 소유자, 디바이스 호출자), 일 지도(하는 일로 묶은 함수들), 명령줄 도구마다 ctest. 진행은 NEXT 0-6.

## 10. 메시 인덱스 버퍼 (2026-11-02 — 사용자 지시 "인덱스 버퍼 추가")

까닭: 메시가 색인 없는 정점 흐름(삼각형마다 정점 셋)이라 스키닝 · 정점 셰이더 · 업로드가 고유 정점의 약 3 배를 돈다. 농장 측정에서 캐릭터 하나가 GPU 0.6 ms(스키닝 컴퓨트가 대부분)였다.
언리얼 · 유니티 · Godot 은 모두 고유 정점 + 인덱스(16/32 비트)이고 구간 · LOD 는 인덱스 범위다. 이중 모드(색인 있음 · 없음)는 두지 않는다 — 모든 메시가 색인을 가진다(내장 도형 · 스프라이트 · 절차 메시 포함).

| 층 | 바꿀 것 |
|---|---|
| 데이터 | `Mesh` · `MeshAssetData` 에 `_listIndex`(uint32), 구간 · LOD 는 `_firstIndex` · `_indexCount`, 모프 델타 · 스킨 · VAT 는 고유 정점 번호. 메시 형식 판 올림(옛 `.mesh` 는 재임포트) |
| 임포트 | `ModelImportGeometry::expandTriangles*` 를 고유 정점 + 인덱스로(이미 용접한 `MergedMesh` 가 있다), 파쇄 조각 · LOD 단순화(meshopt 는 원래 인덱스 입력) |
| CPU 소비자 | 삼각형을 `i*3` 정점으로 읽는 곳(약 18 파일 — Character/Fit · Dismemberment · Appearance · MeshModifiers · Fracture · Mesh 패널 · 충돌 · 내비 굽기)을 `Mesh::getTriangle` 한 길로 |
| GPU | `GPUMeshVertexPool` 옆에 인덱스 풀, 간접 인자를 `RHIDrawIndexedIndirectCommand`(baseVertex)로, GPU 컬링 · 멀티 드로우 병합 경로, 스키닝 컴퓨트는 고유 정점 수만큼 |
| RHI | `drawIndexedInstanced` · 여러 개 `drawIndexedIndirect`(DX12 커맨드 시그니처 · VK `vkCmdDrawIndexedIndirect` · GL `glMultiDrawElementsIndirect` · DX11 반복) |
| 확인 | 모든 골든(정점 순서가 같으면 그림이 같아야 한다 — 차이는 버그로 본다), 농장 · 군중 측정(캐릭터당 GPU 를 다시 잰다) |

순서: RHI(네 백엔드, 끝) → 데이터 · 형식(끝 — `Mesh::setGeometry` · `getIndices`, `.mesh` 5 판) → GPU(끝 — 정점 풀 옆 인덱스 풀, 배치는 baseVertex + startIndex 의 `drawIndexedIndirect`, 모프 · VAT 는 메시 안 번호) → CPU 소비자(끝 — 외형 굽기 · 병합 · 절단 · 파쇄 · 물리 · 내비 · 복셀은 인덱스로 읽고, 모디파이어는 읽을 때 삼각형 목록으로 푼다) → 생산자 용접(임포터 · 도형 · 병합) → 골든 · 측정. 단계마다 빌드가 서는 상태로 커밋한다.
옮기는 동안의 규칙: 용접 단계 전까지 모든 생산자가 삼각형 목록(인덱스 0..n-1, `Mesh::setVertices` · `makeTriangleListIndices`)이라 인덱스 범위가 정점 범위와 같다. 소비자는 모두 인덱스로 읽으므로 이제 생산자가 정점을 나눠 써도 된다. 용접할 때 볼 것: 외형 형상(`CharacterMeshUtil::makeWeldedGeometry`)의 정점 순서가 메시 정점 순서를 따르므로, 저장된 보정 조각(형상 정점 번호)은 용접한 메시로 다시 만든다.
