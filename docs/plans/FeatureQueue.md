# 남은 기능 대기열

> 상태: 참고 · 선행: 없음 · 영역: gameplay · 옛번호: D4-9 · 진행: 해당없음

2026-10-04 사용자 지시로 빠른 순으로 둔 기능 대기열이다. 진행 방식은 [작업 방식](../11_Workflow.md) — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번.
착수하는 것부터 이름 있는 계획 문서로 떼어 내고 여기서 지운다. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시).
다른 문서에 있는 것은 여기 다시 적지 않는다: 메시 LOD · PSO 미리 만들기 · 밉 스트리밍은 [PreloadAndLod](PreloadAndLod.md), 카메라는 [결정 기록 5-5](../decisions/5-5_GraphicsRHI.md), 리플레이 · 킬캠은 [결정 기록 5-8](../decisions/5-8_GameFramework.md), 하늘 · 높이 안개 옛 D2-5, 천 · 머리카락은 [ClothFollowups](ClothFollowups.md), DPI 실물 확인 옛 B-8.

## 1. 대기열

- 중간(M): 대역폭 프로파일러 · 게임플레이 디버거 · 모듈 패키지 관리(옛 D3-8) · HZB 가림 컬링의 밉 사슬 · 두 단계 컬링(한 단 가림 타일은 들어갔다 — `FrameRendererOcclusion.cpp`) · 에셋 DCC 내보내기 · SSR · 시간 누적 업스케일러(공간 업스케일 FSR 1 은 들어갔다 — ThirdPartyGaps F2) · HDR 출력 · 데칼 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
- 들어간 것(큐에서 뗐다):
  - 로딩 흐름의 남은 것: 진행률 · 프리로드 세트 연결 · 팁 목록 데이터. 쿠킹이 구운 `<씬>.preload.json` 을 씬 로드가 `PreloadSet` 으로 열고(`SceneManager::tryGetLoadProgress`), 로딩 화면이 `Progress` 슬라이더와 gamesettings `loadingTips` 파일(`*.loadingtips.xml`, 글 수집은 현지화 프로젝트의 `assetRules`)을 받는다([PreloadAndLod](PreloadAndLod.md) P8).
  - 비주얼 로거: 엔진 서브시스템 `VisualLogger`(`Engine/Graphics/Debug/VisualLogger.h`) — 글 · 선 · 구 · 상자 · 화살표를 프레임 번호와 함께 고리 버퍼에 적고(끈 채 시작), 오브젝트 · 분류 · 프레임으로 질의하고, 한 프레임을 디버그 드로우로 다시 보고(`replayFrame`), `*.vlog.json` 으로 저장한다. 개발 명령 `vlog.on` · `vlog.off` · `vlog.status` · `vlog.clear` · `vlog.save` · `vlog.replay`. 확인 대기: 매니페스트 id `visual-logger-tests`. 에디터에서 프레임 슬라이더로 다시 보는 패널은 남았다(에디터 UI).
- 큼(L): 제약 · 파티클/VFX · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 · 패치/DLC · 모드/UGC.
- 아주 큼(XL): 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 옛 D7-5).
