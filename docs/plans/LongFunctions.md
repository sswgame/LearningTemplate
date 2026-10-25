# 긴 함수 정리 기준 (2026-10-18)

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: C-18 · 진행: 해당없음

100 줄이 넘는 함수를 언제 나누고 언제 그대로 두는지 정하고, 목록을 뽑는 도구와 작업 단위를 둡니다.
나누기 자체는 코드 총량을 줄이지 않습니다. 그래서 줄 수는 후보를 고르는 기준일 뿐이고, 나누는 이유는 아래 1절의 구조 문제여야 합니다.
목록은 `Scripts/lint/report/RunFunctionSize.py` 가 뽑습니다(보고 전용, clang-tidy `readability-function-size` 와 `Source/Engine/Text` 에서 함수 11개의 줄 수가 같음).

## 1. 나누는 기준

순서대로 봅니다. 앞 기준에서 해결되면 뒤로 가지 않습니다.

1. **중복부터 없앱니다.** `RunDuplicateCode.py` 로 긴 함수 안의 반복 블록을 찾고, 공용 함수나 테이블로 합칩니다. 총량이 줄어드는 유일한 경우입니다.
2. **다시 읽기와 두 번 검사를 없앱니다.** 검사 단계가 읽은 값을 실행 단계가 다시 읽는 함수는 한 번 읽은 타입 있는 값을 넘기게 바꿉니다. 자동화 실행기가 대표 예이고, 데이터 주도 계획 7.3 의 "자동화 단계는 한 번만 읽습니다" 가 이 작업입니다.
3. **단계 경계에서 나눕니다.** 파싱, 검사, 적용처럼 단계가 나뉘고, 앞 단계의 지역 변수가 다음 단계로 넘어가지 않는 함수는 단계마다 함수로 나눕니다. 넘겨야 할 값이 많으면 단계 결과 구조체를 둡니다.
4. **분기 사다리는 테이블로 바꿉니다.** 중첩 깊이가 4 이상이거나 같은 형태의 오류 반환이 반복되면, 경우를 데이터 테이블이나 `switch` 하나로 바꿉니다.

그대로 두는 경우는 다음과 같습니다. 이유를 함수 헤더 주석에 한 줄로 적습니다.
- 순서가 곧 의미인 선형 목록입니다. 초기화 순서, 등록 목록, 프레임 단계 본문이 해당합니다. 나누면 순서를 여러 곳에서 읽어야 합니다.
- 경우마다 짧은 `switch` 디스패처입니다.
- 생성 코드, X-macro 테이블, 테스트 본문, 셰이더 코드입니다.

게이트는 두지 않습니다. 줄 수 상한을 게이트로 두면 이유 없는 기계적 분할이 늘어나고, 그것은 총량을 늘립니다. 결정 기록에 한 줄로 남깁니다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| L3 이후 | 영역(폴더)마다 기준 1 ~ 4 를 적용합니다. 영역 하나가 단위 하나이고, 그 폴더를 다른 작업이 건드리는 중이면 미룹니다 | 영역마다 다름 | 그 영역의 다른 계획 단위(예: 자동화는 데이터 주도 D8) | 그 영역의 테스트, 동작 변경 없음 | 핫 패스 함수를 나눌 때만 Release 전후 측정 |

L3 의 단순한 중복 합치기는 `mech-worker` 에 맡길 수 있습니다. 끝난 영역: `Source/Editor/Panels`(2026-10-21 — 본문 100 줄 이상 29건을 의미 단위로 나눴다: 계층 행 · 슬롯 칸 · 카탈로그 XML 읽기 · 속성 표 행과 열거형 콤보 · 레이어 편집기 · 입력 맵 · 콘솔 · 단축키 · 스프라이트 클립 · 시퀀서 · 애니메이션 그래프 · 프로파일러 · 히스토리 · 전역 변수 · 패키징 · 타일맵 · 콘텐츠 브라우저 · 인스펙터 컴포넌트 메뉴 · 데이터 표. `MaterialPanel::drawTypedProperty` 는 경우마다 짧은 `switch` 디스패처라 유지하고 함수 주석에 이유를 적었다. 빌드와 에디터 시나리오 확인은 병합 뒤에 한다), `Source/Editor/Common`(2026-10-21 — 12 개 모두 100 줄 아래. 텍스처 임포트의 밉 · 압축을 HDR 경로와 한 함수로, 원문 테이블 대응 만들기를 한 함수로 합치고 나머지는 단계 함수로), `Source/Engine/Input` · `Destruction` · `Character` · `Audio` · `Localization` · `Physics` · `Source/Games/Empty`(2026-10-21 — XML 읽기 · 평가 · 쪼개기 · 핏 · 믹서 검사 · 이벤트 재생 · 벤치 씬 생성을 단계 함수로 나눴다. 남긴 것은 유지 판정이다: `InputKeyTable` 세 표(케이스마다 한 줄), `InputManager::beginFrame`(프레임 단계 순서가 곧 의미), `InputManager::dispatchRawEvent` · `InputManager::processNativeEvent` Win32 · `InputMap::evaluateBindingDown` · `AudioEngine::applyCommand`(긴 케이스는 빼고 남은 짧은 switch 디스패처). 빌드와 테스트는 아직 하지 않았다), `Source/Editor/Viewport`(2026-10-18 — 페이스 버튼 표, 기즈모 설정 · 단일 조작, 바 손잡이를 단계 함수로. 격자 `drawAdaptiveGrid` 는 람다를 옮기면 숫자 색 리터럴이 새 줄로 잡혀, 테마 토큰 작업(editorux) 때 함께 나눈다), `Source/Core/Network`(2026-10-21 — 패킷 읽기 · 쓰기와 제어 패킷 처리, 요청 프레임을 단계 함수로), `Source/Core/File`(2026-10-21 — 감시 워커의 이벤트 처리를 따로), `Source/GameFramework/Kits`(2026-10-21, `wt/kitclean` — 상태 읽기는 원소 하나 읽기 함수로, 상태 디스패처의 긴 경우는 `update*` 로, 낙하 세기 · 격자 다시 칠하기 중복은 하나로. 유지: `FightingMatch::updateFighter` · `RespEphemeralStore::sendOperation`(짧은 switch 디스패처, 헤더에 이유). `Feature/Online` 5 개는 2026-10-22 `wt/onlineclean` 에서 나눴다: 매칭 서버와 계정 서버는 본문 읽기와 디스패치로, 거래 저장은 닫기 커밋 · 정산 붙이기 · 닫힌 거래 붙이기로, 길드는 초대 · 떠나기 · 창고 목록으로, 경제 구매는 요청 짜기와 계정별 한도로. 빌드는 미확인), `Source/GameFramework/Base`(2026-10-21 — 14 개 중 12 개를 단계 함수로 나눴다. 카메라 평가, 외형 해석, 외형 부품 내보내기, 기믹 회로 만들기, 행동 트리 합성 노드, 플랫포머 이동, HTTP 파서 단계, 기믹 액추에이터, 절단, 상호작용 틱, 원소 격자 스텝, 외형 프리셋 펼치기다. `MemoryEphemeralDatabase::executeValue` 와 나눈 뒤 남은 `BehaviorTreeRunner::tickNode` · `CameraModeInternal::placeByMode` 는 짧은 분기의 switch 디스패처라 유지하고 이유를 주석에 적었다). `ScheduleCatalog::loadElement` 는 원소별 읽기 함수와 `readRoutineChildren` 으로, `BehaviorTreeReader::readNode` 는 종류별 읽기 함수로, `BehaviorTreeEditModel::parseNode` 는 속성 읽기 함수로 나눴다(2026-10-25, `wt/longai`, 빌드 미확인; 속성 기본값 리터럴 규칙은 D9 에서 지워져 막힘이 없다). `Source/Engine/Graphics` · `Renderer`(2026-10-22, `wt/cleanup347`, 빌드 미확인 — 본문 100 줄 초과 54 개 중 24 개를 단계 함수로 나눴다: `FrameRenderer::executePass`(483 → 131 줄, 패스별 `execute*Pass` 와 공통 입력 `PassExecuteInput`), SPIR-V 리플렉션, `ShaderCompiler::compileHlsl`, `GPUSceneBuilder` 수집 · 갱신, `RenderPipelineAsset::validate`, `collectAllRequests`, `ShaderBindingValidator::validate`, `GPUScene::upload`, `RenderThread` 미리보기 · 스크린샷, `ShaderParameterBinder::bindGraphics`, `RenderGraph::compile` · `executeParallel`, `MeshAssetFormat::readFromBytes`, `drawGPUBatches`, 머티리얼 속성 패킹, `ShaderCookDriver::cookAllShaders`, `Material::syncPropertiesFromReflection`, `ShaderCache::getOrCompile`, 정점 · 모프 풀 갱신, `FrameRenderer::submitGraph` · `prepareExtraViews` · `createPSOForPassType` · `ensureMaterialPSOs`, `RenderPacketBuilder::populateScene`, `Render2DSettings::loadFromXMLText`, `ShaderBindingLayout::make`. 남은 30 개는 유지 판정이고 함수 헤더에 이유를 적었다: RHI 백엔드의 초기화 · 종료 · 생성 · 렌더 패스 시작 24 개(백엔드 API 호출 순서가 곧 의미), `makeInfo` · `reservedBindings`(표), `executePass`(남은 것은 짧은 디스패처), `compileDxc` · `RenderThread::executeFrameBody` · `GPUSceneBuilder::populateMeshesFromScene`(순서 · 결정 순서가 곧 의미)). 2026-10-24 `wt/plangfxq`(빌드 미확인, 3절 목록을 `RunFunctionSize.py` 로 다시 뽑아 고름): `TileMapXMLData::loadFromXML` · `toXML`(읽기 · 쓰기 단계), `UserSettingsSchema::loadRoot` · `validate`(요소별 읽기 · 설정 / 품질 번들 검사), `DDSLoader::loadFromMemory`(FourCC 표), `InputMap::loadUserBindings` · `saveUserBindings` · `writeDefinitionBinding`(바인딩 종류별), `ModuleCatalog::parseManifest` · `resolve`(단계), `TextLayoutEngine::layout`(줄 채우기 · 제한 · 줄 구성 · 글리프 내기 · 정렬), `RecastNavMesh::bakeTile`(구성 · 복셀화 · 부피 · 타일 바이트, 링크 판정 중복 합침), `AnimationGraphAsset::parseRoot` · `parseLayers`(노드 · 링크 · 마스크 · 레이어), `Scene::instantiateNodes`(오브젝트 만들기 · 상태 얹기 · 중첩 프리팹), `FarmAutoFarmerAIComponent::think`(씨앗 · 수확물 · 칸 할 일, 출하 중복 합침), `TileSetAsset::loadFromXMLText`(브러시 하나 읽기), `SceneAudio::update`(리스너 · 에미터 · 가림 · 리버브 존), `TickRegistry::applyStageDelta`(스테이지 찾기 · 끼우기 · 표시). 같은 날 `wt/plangfxq` 둘째 묶음(빌드 미확인): 직렬화(`BinarySerializer::deserializeTagged` 는 태그 하나 읽기 `readTaggedField` 로, `readXMLIntoInstance` 는 컨테이너 · 중첩 · 스칼라 필드별로, `JSONSerializer::readObject`, `SerializeContext::makeDefault` 는 핸들 · 비트 목록 핸들러 등록을 뺌), `DirectionalLightComponent::computeShadowProjectionForView`, `BVHTree3D` 둘, `Process::launch` 둘(Windows 파이프 · 속성 / Posix 파이프), `PoseRetargeter::initialize`(사슬 본 · 이름 짝 모으기와 짝 만들기), `LipSyncAnalyzer::analyze`(터짐 오름 찾기 · 비즘 점수), `RigSpringChain::simulate`(충돌체 밀어내기 · 고정 스텝 꺼내기), `FoliageComponent::applyLayerInstances`(메시 얻기 · 셀 묶기 · 셀 만들기 · 셀 놓기), `SceneTransformHierarchy::applyBatch`(세터 길 · 연속 구간 병렬 · 버킷 병렬), `CultureTable::loadFromXMLText`(요소 모으기 · 부모 순서 풀기), `SceneDocument::loadXMLSource`(월드 설정 · 엔티티 읽기), `CharacterMeshUtil::createBakedMesh`(오프셋 · 정점 굽기 · 구간 · 모프 옮기기), `AsyncLogSink::writeLogInternal`(시각 · 줄 포맷 · 리스너 방송 · 큐 넣기), `parseFormatSpec`(플래그 · 너비와 정밀도 · 길이 수식어 · 변환 문자), `Win32Window::wndProc`(WM_SIZE · 최소 크기), `ModuleCompiler::runBuildThread`(실패 끝 경로 중복 합침), `LiveReloadManager::reloadCascade`(준비 · 교체 · 버리기 중복 합침), `UIStyleTransition::onStyleChanged`, `Scene::serializeToDocument`(저장 입력 · 엔티티 쓰기), `JoltPhysicsScene::createSoftBody`, `SceneCooker::partitionStreamingCells`, `AudioGraphInstance::renderBlock`(노드 종류별), `AnimationSystem::evaluate`, `GPUScene::uploadMaterialGroups`, `ReceiptProviderSet::readSettings`(종류별), `FrameRenderer::drawGPUBatches`(`BatchDrawState` · 한 바퀴 · 묶음 세기), `PackPatchBuilder::makePatch`, `AutomationScenario::parse`(루트 속성 · 자식 · 시점 — D8 의 "한 번만 읽기" 없이도 단계가 나뉘어 선행 조건이 아니었다), `ScenarioRecorder::appendFrameSteps`, `ProfilerTraceExport::parseTraceJSON` · `mergeTraceJSON`(익명 네임스페이스 둘을 `ProfilerTraceExportInternal` 한 구조체로 모음), `Engine::update`(서비스 · 씬 틱 · 런타임 UI · 렌더 패킷 제출), `HeadlessSubsystem::initializeSubsystem`(작업마다 함수로, 우선순위는 한 식), `FarmAutoFarmerAIComponent::think`(할 일 고르기). 에디터는 `VrmMaterialImporter::readVrm0Material`, `ModelImporter`(`readSkinnedModel` · `readModelAsset` — `GltfData` 소유자가 `cgltf_free` 를 맡음 · `importModel`), `ModelImportUtil::appendPrimitive` · `promoteSceneRootsToTopAncestor`, `VrmAvatarImporter::readVrm1Springs`, `ImGuiEditor::updateUI`, `QuickLauncherPopup::drawContent`, `TestRunnerPanel::drawUnitTestTab`, `RigDebugPanel::drawContent`, `EditorDataTableCommands::saveLocalizationProject`, `MeshModifiersPanel::drawNumberListParam` 을 나눴다. 이번 묶음에서 유지로 판정해 함수 주석에 이유를 적은 것: `MeshModifiersPanel::drawParam`(경우마다 짧은 switch 디스패처), `Engine::initialize`(기동 순서가 곧 의미인 열). 남은 긴 함수와 그 판정은 3절 표가 정본이고, 이 절의 영역별 기록은 그때그때의 이력이다(여기 적힌 "남은 N 개 모두 유지" 같은 셈은 다시 뽑으면 어긋났다). 영역을 맡기 전에 3절 표에서 그 영역 행만 봅니다.

## 3. 목록

본문 100 줄을 넘는 함수는 지금 71 개이고, 아래 표가 전부다(2026-10-25, `RunFunctionSize.py` 를 `Source` 전체에서 다시 돌림). 이미 나뉜 영역은 이 표에 없다: `Source/Core/Network` 의 `NetRequestServer::handleFrame` · `NetConnection::writePacket` 도 지금은 60 줄 아래라 끝났다.

**표에는 줄 수를 적지 않습니다.** 손으로 적은 줄 수는 코드가 바뀌면 어긋나고, 그래서 이미 나뉜 함수를 작업자가 다시 맡았다. 줄 수 · 중첩은 `py -3 Scripts/lint/report/RunFunctionSize.py --filter <경로>` 로 그때 잰다.
표가 맡는 것은 사람이 정한 판정뿐이고, `py -3 -m Scripts plans --check` 가 표의 행(파일 + 함수 이름)과 코드의 100 줄 초과 함수가 같은지 본다. 새로 길어진 함수는 판정 행을 더하게 하고, 나뉜 함수의 행은 지우게 한다(틀리면 종료 1). 함수를 나누면 그 행을 지우고, 유지로 정하면 판정 칸에 이유를 적고 함수 헤더 주석에도 같은 이유를 적는다.

판정 칸은 `나눔`(기준 번호와 단계) 또는 `유지`(이유)로 시작한다. `나눔 (예비)` 는 본문을 다 읽지 않고 단계 주석 · 케이스 모양으로 정한 것이라, 맡은 작업자가 읽고 확정한다. 기준 1(중복)은 어느 함수든 먼저 본다.

| 파일 | 함수 | 판정 |
|---|---|---|
| `Source/GameFramework/Base/Actor/AI/BehaviorTree.cpp` | `BehaviorTreeRunner::tickNode` | 유지 — 12 종류 `switch` 의 케이스가 평균 9 줄이고 합성 노드(`tickSequence` · `tickSelector`)는 이미 뺐다. 가장 긴 케이스(`Repeat` · `Cooldown`)도 13 줄 안쪽이라 더 가르면 같은 모양의 함수만 늘어난다. 함수 주석에 이유가 있다 |
| `Source/GameFramework/Base/Online/Cache/MemoryEphemeralStore.cpp` | `MemoryEphemeralDatabase::executeValue` | 유지 — 연산 7 개의 `switch` 이고 케이스가 10 ~ 24 줄이다(`Increment` 가 가장 길다). 연산 하나의 규칙(RESP 와 같은 동작)을 한곳에서 읽는 값이 있어 유지하되, `Increment` 가 더 길어지면 그 케이스를 뺀다. 함수 주석에 이유가 있다 |
| `Source/Engine/Resource/Pack/ResourcePakWriter.cpp` | `ResourcePakWriter::makePackBytes` | 나눔 — 기준 3: FAT 정렬 · 코덱 고르기 · 항목별 병렬 압축 · 직렬 배치(오프셋) · 페이로드 복사가 주석으로 이미 나뉜 단계다 |
| `Source/Engine/Object/SceneAnimation/AnimationSystem.cpp` | `AnimationSystem::rebuildComponents` | 나눔 — 기준 3: 간선 모으기(CSR) · Kahn 위상 정렬 · 고리 보고 · union-find 성분 묶기 · 성분 구간 놓기. 단계 사이로 넘기는 값은 `_listScratch*` 멤버라 구조체가 필요 없다 |
| `Source/Engine/Character/Fit/CharacterMeshUtil.cpp` | `CharacterMeshUtil::mergeMeshes` | 나눔 — 기준 3: 부품 검사 · 그룹 만들기 · 정점 이어 붙이기 · 모프 타깃 합치기 |
| `Source/Editor/Common/Asset/OutfitAtlasBaker.cpp` | `OutfitAtlasBaker::bake` | 나눔 — 기준 3: 부품 메시 읽기 · 맵 이미지 읽기와 배치 · 메시 병합 · 합성과 쓰기(쓰기 전에 메모리에서 모두 끝낸다는 순서는 유지) |
| `Source/Engine/Renderer/Vfx/VfxGPUSimulation.cpp` | `VfxGPUSimulation::prepareFrame` | 나눔 (예비) — 기준 3: 파티클 배치 · 파티클 라이트 · 메시 방출기 · 지난 재생 놓기 |
| `Source/Engine/Renderer/Light/GPULightBuffer.cpp` | `GPULightBuffer::update` | 나눔 — 기준 3: 프레임 상한 나누기 · 업로드 목록 짜기 · 버퍼 확보 · 프로브 SH 올리기 |
| `Source/Editor/Panels/Catalog/CatalogTableEditor.cpp` | `CatalogTableEditor::draw` | 나눔 (예비) — 기준 3: 그룹마다 표를 그리는 `for` 본문(약 100 줄)을 그룹 하나 그리기로 뺀다 |
| `Source/Editor/Panels/Inspector/InspectorPropertyManager.cpp` | `drawValueWidget` | 나눔 (예비) — 기준 4: 값 타입별 분기(`return` 24 개)를 타입별 위젯 함수 또는 오버로드로 |
| `Source/Editor/Panels/Inspector/EditorPropertyGrid.cpp` | `EditorPropertyGrid::drawTypeMethods` | 나눔 (예비) — 기준 3: 메서드 하나 그리기 · 인자 입력란 · 호출 버튼 |
| `Source/Engine/Graphics/Texture/TextureCache.cpp` | `TextureCache::flushRegionUpdates` | 나눔 (예비) — 기준 3: 요청 하나 내기 · `Busy` / `Rejected` 규칙 · 미룬 것 합치기 |
| `Source/Engine/Renderer/Canvas/CanvasRenderer.cpp` | `CanvasRenderer::uploadAtlasPage` | 나눔 (예비) — 기준 3: 첫 전체 업로드 · 등록 · 구간 업로드 · 실패 규칙 |
| `Source/Engine/Destruction/FractureComponentBase.cpp` | `FractureComponentBase::updateGroups` | 나눔 (예비) — 기준 3: 잠든 덩어리 정리 · 예산 초과 제거 순서 정하기 |
| `Source/Engine/Graphics/Shader/Compile/ShaderCookStamp.cpp` | `verifyDomain` | 나눔 (예비) — 기준 3: 우선순위 폴더 판정 · 헤더 스탬프 판정 · 키 누락 판정 |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp` | `VulkanRHISwapChain::createSurface` | 나눔 (예비) — 기준 4: 창 시스템 길(Win32 · Xlib · Xcb · Wayland)마다 표면 만들기 함수로 |
| `Source/Engine/Graphics/Lighting/SceneVoxelGrid.cpp` | `SceneVoxelGrid::traceRay` | 유지 — 슬랩 판정 · 시작 칸 · 칸 걷기가 지역 변수(`tNear` · 칸 · 축 · 경계 거리)를 모두 공유하는 한 알고리즘이고 103 줄이라 경계다. 단계 결과 구조체를 둘 만큼의 이득이 없다. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/EngineSubsystemSet.cpp` | `addStartupSubsystems` | 유지 — 기동 서브시스템을 순서대로 단계 객체에 등록하는 열이고, 순서와 범위(호스트 · 엔진 · Server 제외)가 곧 의미다. `Engine::initialize` 와 같은 이유. 헤더 주석에 이유를 적어야 한다 |
| `Source/App/App.cpp` | `App::initialize` | 유지 — 호스트 범위 → 엔진 → 헤드리스 작업 → 자동화 → 설정 감시 순의 시동 열이고 순서가 곧 의미다(`Engine::initialize` 와 같은 이유). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Engine.cpp` | `Engine::initialize` | 유지 — 기동 순서가 곧 의미인 열(함수 주석에 이유) |
| `Source/Engine/Renderer/RenderThread.cpp` | `RenderThread::executeFrameBody` | 유지 — 프레임 단계 순서가 곧 의미이고 단계별 측정 범위 주석이 함께 읽혀야 한다 |
| `Source/Engine/Renderer/Frame/FrameRendererPassExecute.cpp` | `FrameRenderer::executePass` | 유지 — 패스별 `execute*Pass` 로 뺀 뒤 남은 짧은 `switch` 디스패처(함수 주석에 이유) |
| `Source/Engine/Renderer/Scene/GPUSceneBuilder.cpp` | `GPUSceneBuilder::populateMeshesFromScene` | 유지 — 단계는 이미 나뉘었고 남은 본문은 단계를 고르는 결정 순서(함수 주석에 이유) |
| `Source/Engine/Graphics/Shader/Compile/ShaderCompiler.cpp` | `compileDxc` | 유지 — 생성 · 소스 읽기 · 인자 · 컴파일 · 결과 읽기의 순서가 곧 의미이고 인자 글의 수명이 이 함수 안이다 |
| `Source/Engine/Renderer/Pipeline/RenderPassTypeInfo.cpp` | `makeInfo` | 유지 — 경우마다 표 한 줄(함수 주석에 이유) |
| `Source/Engine/Graphics/Shader/Binding/ShaderBindingValidator.cpp` | `buildReservedBindings` | 유지 — 줄마다 리소스 하나인 등록 목록(함수 주석에 이유) |
| `Source/Engine/Input/Windows/InputKeyTableWin32.cpp` | `InputKeyTable::mapWin32VirtualKey` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Linux/InputKeyTableX11.cpp` | `InputKeyTable::mapX11KeySym` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Map/InputKeyTable.cpp` | `InputKeyTable::mapScanCodeToKey` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Windows/InputManagerWin32.cpp` | `InputManager::processNativeEvent` | 유지 — 긴 케이스는 뺀 뒤 남은 메시지별 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Input/Linux/InputManagerX11.cpp` | `InputManager::processNativeEvent` | 유지 — Win32 짝과 같은 이벤트 종류별 `switch` 디스패처. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Input/InputManager.cpp` | `InputManager::dispatchRawEvent` | 유지 — 이벤트 종류마다 장치 한 곳에 적용하는 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Input/InputManager.cpp` | `InputManager::beginFrame` | 유지 — 프레임 단계 순서가 곧 의미인 선형 본문(함수 주석에 이유) |
| `Source/Engine/Input/Map/InputMapEvaluate.cpp` | `InputMap::evaluateBindingDown` | 유지 — 긴 케이스는 뺀 뒤 남은 종류별 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Audio/AudioEngine.cpp` | `AudioEngine::applyCommand` | 유지 — 긴 케이스는 뺀 뒤 남은 명령별 짧은 `switch`(함수 주석에 이유) |
| `Source/Editor/Panels/Material/MaterialPanel.cpp` | `drawTypedProperty` | 유지 — 경우마다 짧은 `switch` 디스패처(함수 주석에 이유) |
| `Source/GameFramework/PlugIn/Editor/MeshModifiers/MeshModifiersPanel.cpp` | `MeshModifiersPanel::drawParam` | 유지 — 매개변수 종류마다 짧은 경우를 늘어놓은 `switch`(함수 주석에 이유) |
| `Source/GameFramework/Kits/Genre/Action/Fighting/FightingMatch.cpp` | `FightingMatch::updateFighter` | 유지 — 상태별 짧은 `switch` 디스패처(헤더에 이유) |
| `Source/GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.cpp` | `RespEphemeralStore::sendOperation` | 유지 — 연산별 짧은 `switch` 디스패처(헤더에 이유) |
| `Source/GameFramework/Base/Actor/Camera/CameraMode.cpp` | `CameraModeInternal::placeByMode` | 유지 — 카메라 모드별 짧은 분기 `switch`(함수 주석에 이유). `OrthoTopDown` 케이스가 가장 길다 |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIDeviceInit.cpp` | `OpenGLRHIDevice::initializeInternal` | 유지 — 컨텍스트 · 확장 확인 · 기본 객체 생성의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIDeviceInit.cpp` | `OpenGLRHIDevice::shutdownInternal` | 유지 — 생성의 역순으로 놓는 해제 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHICommandContext.cpp` | `OpenGLRHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.cpp` | `OpenGLRHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.cpp` | `OpenGLRHIResourceFactory::submitTextureUpload` | 유지 — 검사 · 링 복사 · 컨텍스트 빌려 올리기 · 펜스의 순서가 곧 의미(전송 계약 1 · 2 · 4). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/GL/Platform/GlxPlatformContext.cpp` | `GlxPlatformContext::initialize` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/Platform/WglPlatformContext.cpp` | `WglPlatformContext::initialize` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX11/D3D11RHIDeviceInit.cpp` | `D3D11RHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.cpp` | `D3D11RHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceInit.cpp` | `D3D12RHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceDescriptor.cpp` | `D3D12RHIDevice::createGlobalResources` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceSubmission.cpp` | `D3D12RHIDevice::endFrame` | 유지 — 닫기 · 타임스탬프 해석 · 제출 · 펜스 · Present 의 프레임 끝 순서가 곧 의미이고 업로드 리스트를 프레임 리스트 앞에 끼우는 규칙이 한곳에 있어야 한다. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.cpp` | `D3D12RHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.cpp` | `D3D12RHIResourceFactory::submitTextureUpload` | 유지 — 서브리소스 · 풋프린트 · 스테이징 복사 · 상태 전이의 순서가 곧 의미. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHICommandContext.cpp` | `D3D12RHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.cpp` | `VulkanRHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.cpp` | `VulkanRHIDevice::shutdownInternal` | 유지 — 생성의 역순으로 놓는 해제 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInit.cpp` | `VulkanRHIDevice::createInstance` | 유지 — 확장 · 레이어 확인 후 생성하는 백엔드 API 호출 순서가 곧 의미(창 시스템 길 분기는 짧다). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInit.cpp` | `VulkanRHIDevice::createLogicalDevice` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceDescriptor.cpp` | `VulkanRHIDevice::createDescriptorResources` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceSubmission.cpp` | `VulkanRHIDevice::beginFrame` | 유지 — 프레임 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::updateStructuredBufferRegions` | 유지 — 스테이징 확보 · 복사 · 장벽 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::submitTextureUpload` | 유지 — 검사 · 링 복사 · 기록 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::submitTextureReadback` | 유지 — 검사 · 링 할당 · 기록 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactoryPipeline.cpp` | `VulkanRHIResourceFactory::createPipelineState` | 유지 — 서술을 단계 순서로 채운 뒤 만드는 선형 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp` | `VulkanRHISwapChain::create` | 유지 — 표면 능력 조회 · 형식 고르기 · 스왑체인 · 이미지 뷰 생성의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.cpp` | `VulkanRHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |

## 4. 사용자 결정

없습니다.
