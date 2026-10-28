# 로드맵 — 앞으로 할 일의 순서

> 질문: 남은 일을 어떤 순서로 하는가?
> 독자: 다음 일을 고르는 통합 담당과 작업자.
> 결론: 이 문서는 순서와 병렬 여부만 가진다. 설계 · 진행 · 상태는 링크한 계획 문서의 머리 줄이 원본이고, 끝난 일은 이 표에서 지운다.

- 일의 식별자는 이름이다(계획 문서 파일 이름 또는 `Candidates-<영역>.md` 의 `### 이름` 절). 옛 번호는 `py -3 -m Scripts plans --find <옛번호>` 로 푼다.
- 착수 전에 항목의 현재 완성도를 코드로 확인한다. 같은 파일을 건드리는 단위는 한 작업자에게 묶는다. GPU 확인이 필요한 단위는 구현만 하고 통합에서 캡처한다.
- 병렬 열: `병렬` 은 다른 작업과 같은 시간대에 병합해도 파일이 겹치지 않는다는 뜻이고, `단독` 은 혼자 하는 시간대가 필요하다는 뜻이다(이유를 적는다).

| # | 계획 | 한 줄 설명 | 병렬 | 선행 |
|---|------|-----------|------|------|
| 1 | [DataDriven](DataDriven.md) 7.5 | 데이터화 마무리 | 단독: 기본값 · 로더 형식 변경으로 여러 모듈 재빌드 | 없음 |
| 2 | [LeakDetection](LeakDetection.md) | 누수 탐지 | 단독: `Memory.h` 변경 재빌드, 측정은 Release 에서 혼자 | 1 |
| 3 | [ClusteredLighting](ClusteredLighting.md) | 클러스터드 라이팅 | 단독: 측정은 Release 에서 혼자 | 없음 |
| 4 | [EnvironmentRemainders](EnvironmentRemainders.md) | 환경(지형 · 식생 · 물)의 남은 것 | 단독: 측정은 Release 에서 혼자 | 없음 |
| 5 | [TerrainEditCost](TerrainEditCost.md), [DestructionNetSessionScene](Candidates-network.md#destructionnetsessionscene) → [EncryptedTickCost](Candidates-network.md#encryptedtickcost) | 지형 · 식생 비용, 서버 파괴 호스트 | 단독: 셰이더 재베이크, 측정은 Release 에서 혼자 | 4 |
| 6 | [OnlineKits](OnlineKits.md) | 온라인 키트 마감 | 병렬 | 없음 |
| 7 | [ShooterCharacterIntegration](ShooterCharacterIntegration.md) · [ServerDebug](ServerDebug.md) · [WildWestHorseMount](WildWestHorseMount.md) | Shooter3D 캐릭터, 서버 디버깅, 승마 키트 | 병렬 | 없음 |
| 8 | [MsdfGlyphs](MsdfGlyphs.md) | 런타임 UI 글리프 | 병렬 | 없음 |
| 9 | [PreloadAndLod](PreloadAndLod.md) | 프리로딩 · LOD | 단독: `.mesh` 판 올림 · 셰이더 재베이크 | 5 |
| 10 | [WaylandWindowLayer](WaylandWindowLayer.md) · [DedicatedServerNoRenderer](DedicatedServerNoRenderer.md) | Wayland 창 계층, 서버 타깃에서 렌더러 빼기 | 병렬 | 없음 |
| 11 | [LongFunctions](LongFunctions.md) | 긴 함수 정리 | 단독: 헤더 · include 변경 재빌드 | 1 ~ 2 |
| 12 | [DecisionMeasurementColumn](Candidates-project.md#decisionmeasurementcolumn) · [CarriedOverChecks](Candidates-project.md#carriedoverchecks) · [SilentFallbackAudit](SilentFallbackAudit.md) | 결정 측정 열, 이월 확인, 조용한 폴백 감사(정리 → 기능 순) | 병렬 | 없음 |
| 13 | [ModuleRenderPass](ModuleRenderPass.md) · [CrossFileObjectReference](CrossFileObjectReference.md) · [ThirdPartyGaps](ThirdPartyGaps.md) | 렌더 패스 등록 창구, 파일 넘는 오브젝트 참조, 서드파티 빈자리 | 병렬 | 없음 |
| 14 | [AutomationSplit](AutomationSplit.md) | Automation 분리 | 병렬: 셰이더 · 층 게이트와 파일이 다르다 | 없음 |
| 15 | [RhiAsyncUploads](RhiAsyncUploads.md) | RHI 비동기 업로드(U5 GPU 확인은 통합, U7 · U8 · U9 · U11 남은 몫, 이어 U10 · U12 ~ U14) | 병렬: 단위마다 소유 폴더가 다르다(계획 문서 표) | 없음 |
| 16 | [PackedTypes](PackedTypes.md) | 저장 전용 압축 타입 P4(G 버퍼 법선, GPU 확인 · 재쿠킹은 통합) → P5 → P6 | 단독: P4 는 셰이더 재베이크 | 없음 |
| 17 | [RuntimeQualityAdjust](RuntimeQualityAdjust.md) | 부하 검증의 남은 것과 자동 품질 조정 | 병렬 | 없음 |
| 18 | [BuildSpeed](BuildSpeed.md) | 빌드 속도 12 단계(리플렉션 헤더 팬아웃) | 단독: 다른 작업이 모두 끝난 뒤 혼자, 빌드 중 측정 무효 | 1 ~ 17 |

순서에 올리지 않는 조건부 · 보류 · 관찰 항목은 후보 문서의 `## 조건부` / `## 보류` / `## 관찰` 절에 있다. 조건이 오면 거기서 골라 온다: [engine](Candidates-engine.md) · [graphics](Candidates-graphics.md) · [gameplay](Candidates-gameplay.md) · [editor](Candidates-editor.md) · [network](Candidates-network.md) · [ui](Candidates-ui.md) · [quality](Candidates-quality.md) · [project](Candidates-project.md) · [audio-ai-localization](Candidates-audio-ai-localization.md).
