# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 진행: 해당없음

GameFramework 키트 · 게임 · 캐릭터 · 카메라 · 애니메이션에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 큰 기능 대기열은 [FeatureQueue](FeatureQueue.md)에 있다.
아래는 모두 코드와 테스트가 끝나고 데이터 · 탐침 · 시나리오를 새로 만들어야 남은 절이다. 착수 조건은 모두 "바로 착수 가능" 이다.

## 한눈에 보기

| 절 | 한 줄 설명 | 만들 것 |
|---|---|---|
| CharacterAppearanceScreenChecks | 병합 부품이 든 외형 차림 | 데이터 · 프리팹 |
| CameraRendererVisualChecks | 가려져 쉰 뷰 수 탐침 | 탐침 · 시나리오 |
| PairedAnimationScreenChecks | 페어드 애니메이션 에디터 창 | 에디터 시나리오 |
| RigEditingScreenChecks | 리그 디버그 창 저장 · 기즈모 | 에디터 시나리오 |

## 데이터

### CharacterAppearanceScreenChecks
제목: 캐릭터 외형의 병합 부품 데이터
왜 필요한가: 외형 조립 코드와 테스트는 끝났고 합칠 스킨드 부품이 든 차림 데이터와 `Merge Parts` 를 켠 프리팹이 없다(Shooter3D 의 스킨드 부품은 자기 리그가 있는 망토뿐).
착수 조건: 없음.
남은 첫 일: 그 차림 데이터와 프리팹을 만든다.
함정: 떼어 낸 부품 오브젝트의 수명(잔해 정리)을 정하는 곳이 없다. 칸 점유(양손 무기)는 외형만 본다. 나중에: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로 쓰게 한다.

## 탐침 · 시나리오

### CameraRendererVisualChecks
제목: 가려진 뷰의 가림 판정 탐침과 시나리오
왜 필요한가: 카메라별 후처리 · 피사계 심도 · 컷 신호 · 자동 노출 · 다중 뷰 가림 판정은 코드와 테스트가 통과했지만 가려져 쉰 뷰 수를 읽는 탐침과 시나리오가 없다.
착수 조건: 없음.
탐침 `RenderView.HiddenCount`(마지막 수집에서 보임 판정으로 쉰 추가 뷰 수)는 넣었다.
남은 첫 일: `-gv_occlusionCulling=1` 로 Shooter3D 의 CCTV 모니터를 벽 뒤로 보내는 카메라 위치를 정해 시나리오로 만든다.

### PairedAnimationScreenChecks
제목: 페어드 애니메이션의 에디터 창 시나리오
왜 필요한가: 세션 · 정렬 · 복제 · 에디터 모델은 테스트가 통과했고, 두 프로세스 복제는 씬 `pairedanimnet.scene.xml` 과 `paired-net-pair` 명령으로 구성했다. 에디터 창 시나리오가 없다.
착수 조건: 없음.
남은 첫 일: 미리보기 창(`PairedAnimationPanel`)에서 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 시나리오(`-EnableEditor`, `CaptureEditorUI`)를 만든다. 활 · 화살 같은 외형 부품 프롭의 역할 복제 확인은 그 게임이 생길 때 그 게임 계획에서 만든다.
함정: 재생 속도를 매 프레임 쓰는 컴포넌트가 새로 생기면 세션 쥠(`setPlayRateHeld`)을 받아야 한다. 복원은 `warp` 창을 다시 열지 않아 창 도중에 이어 받은 클라이언트는 서버 보정을 받는다. 같은 에셋 · 같은 역할의 프롭이 둘 이상이면 역할 묶기가 거절한다.

### RigEditingScreenChecks
제목: 리그 디버그 창 저장과 대상 기즈모 시나리오
왜 필요한가: 창에서 고친 값을 리그 에셋에 되쓰는 길(`RigAsset::rewriteSettings`)과 대상 본 기즈모는 테스트가 통과했고 에디터 흐름의 시나리오가 없다.
착수 조건: 없음.
남은 첫 일: `-EnableEditor` 로 Shooter3D `arena.scene.xml` 을 플레이해 리그 디버그 창에서 가중치 · 켜짐을 바꾸고 "Save to rig" 후 파일 감시가 다시 읽어도 값이 남는지, "Gizmo" 로 대상(본 · 소켓)을 끌면 `translation` · `rotation` 이 바뀌고 Ctrl+Z 가 파일을 되돌리는지 시나리오로 만든다.
