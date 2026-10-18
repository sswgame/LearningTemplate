# 백로그 안내

백로그는 여러 PC와 여러 세션이 함께 쓰는 할 일 목록입니다. 아직 열린 일과, 그 일을 시작하기 전에 알아야 할 함정만 둡니다.
항목은 영역별 파일에 있고, 항목마다 첫 줄에 상태 태그가 붙습니다. 전체 목록과 개수는 손으로 세지 않고 스크립트로 뽑습니다.

## 영역별 파일

| 파일 | 영역 |
|---|---|
| [engine.md](engine.md) | 엔진 구조, 오브젝트, 물리, 내비게이션, 모듈 |
| [graphics.md](graphics.md) | 렌더러, RHI, 셰이더, 환경 |
| [editor.md](editor.md) | 에디터 패널, 에디터 플러그인, 에디터 시나리오 |
| [gameplay.md](gameplay.md) | 키트와 게임, 캐릭터, 카메라, 애니메이션 |
| [audio-ai-localization.md](audio-ai-localization.md) | 오디오, 현지화, NPC AI, 게임 로직 자동화 |
| [ui.md](ui.md) | 런타임 UI, 글자 렌더링, 접근성, DPI |
| [network.md](network.md) | 네트워크, 전용 서버, 온라인 서비스, 운영 |
| [quality.md](quality.md) | 테스트, CI, 린트 스크립트, 부하 검증, 빌드 속도, 간헐 실패 |
| [project.md](project.md) | 코딩 규칙과 이름, 주석 문체, 서드파티, 게임 리소스 |

계획 문서가 따로 있는 일도 있습니다. 엔진 분할은 [EnginePartition](../plans/EnginePartition.md), 빌드 속도는 [BuildSpeed](../plans/BuildSpeed.md),
서버 디버깅은 [ServerDebug](../plans/ServerDebug.md)입니다. 단위를 끝내면 계획 문서에서 지웁니다.

## 상태 태그

| 태그 | 옛 분류 | 뜻 |
|---|---|---|
| `결함` | A | 재현됐거나 코드로 확인된 열린 결함 |
| `검증대기` | B | 코드는 들어갔고 돌려 보는 일이 남은 것 |
| `정리` | C | 구조 분할, 중복과 의존 정리, 성능 후보, 린트와 문서 정리 |
| `기능` | D | 새 기능이나 기능의 남은 조각 |
| `관찰` | E | 간헐 실패나 원인을 모르는 현상 |
| `결정대기` | F | 사용자 결정이 있어야 움직이는 질문 |
| `조건부` | G | 조건이 오면 하는 일과 이득이 작은 후보 |
| `보류` | H | 사용자가 "나중에"로 정한 일 |

`검증대기`에는 이 PC에서 돌릴 수 없는 환경(리눅스, 실기)과 사용자 눈이 필요한 확인도 들어갑니다.
`관찰`은 다시 보이면 그때 원인을 팝니다. CI 결과를 읽는 법은 [검증과 측정](../08_Verification.md) 4절에 있습니다.
`조건부`는 "쓰는 게임이 생기면", "측정해서 이기면"처럼 조건을 항목에 적습니다.

## 일을 고르는 순서

1. `결함`을 다른 어떤 일보다 먼저 봅니다.
2. 다음은 `검증대기`, 그다음은 `정리`, 그다음은 `기능`입니다. 한 파일 안에서는 위에 있는 항목을 먼저 봅니다.
3. `관찰`은 같은 현상이 다시 보일 때만 봅니다. `결정대기`는 사용자의 답이 오면 봅니다.
4. `조건부`는 적힌 조건이 오면, `보류`는 사용자가 풀면 봅니다.

## 목록 뽑기

```powershell
py -3 -m Scripts backlog                     # 상태별, 영역별 개수
py -3 -m Scripts backlog --status 결함       # 그 상태의 항목 목록(번호, 영역, 제목)
py -3 -m Scripts backlog --area editor       # 그 파일의 항목 목록
py -3 -m Scripts backlog --id C-28           # 그 항목의 본문만
```

에이전트는 파일을 통째로 읽지 않고 `--id`로 자기 항목만 읽습니다.

## 항목을 쓰고 지우는 법

항목 하나는 아래 모양입니다. 첫 줄은 번호, 상태 태그, 굵은 제목 순서이고 이어지는 줄은 두 칸 들여 씁니다. 항목 사이에는 빈 줄을 둡니다.

```markdown
- `D3-13` [기능] **제목 한 문장.** 무엇이 남았고 어디서 시작하는지.
  이어지는 줄은 두 칸 들여 씁니다.
```

- 새 항목은 그 영역의 파일에 넣습니다. 같은 상태의 항목들 끝에 붙입니다. 어느 영역에도 속하지 않으면 `project.md`에 넣습니다.
- 번호의 문자는 항목을 처음 적을 때의 상태에서 고릅니다(결함 A, 검증대기 B, 정리 C, 관찰 E, 결정대기 F, 조건부 G, 보류 H).
  기능은 `D` 뒤에 영역 숫자를 붙입니다(D1 오브젝트와 물리, D2 그래픽스, D3 에디터, D4 게임플레이, D5 오디오와 AI, D6 런타임 UI, D7 네트워크, D8 모듈).
  숫자는 같은 문자의 가장 큰 번호 다음입니다. `py -3 -m Scripts backlog --status` 출력이나 `grep`으로 찾습니다.
- 번호는 다시 매기지 않습니다. 코드 주석과 문서가 번호로 가리키고, 번호 뒤 따옴표 안에 제목의 첫 낱말을 적습니다.
  상태가 바뀌면 태그만 고치고 번호는 그대로 둡니다. 영역이 바뀌면 항목을 다른 파일로 옮기고 번호는 그대로 둡니다.
- 일을 끝내면 그 항목을 같은 커밋에서 지웁니다. "완료" 표시를 남기지 않습니다. 남길 교훈은 그 코드의 헤더 주석에, 정한 방향과 기각한 안은
  [결정 기록](../09_Decisions.md)에, 검증과 측정 방법은 [검증과 측정](../08_Verification.md)에 옮깁니다. 무엇을 고쳤는지는 커밋 메시지에 씁니다.
- 여러 PC에서 고치는 파일이라 작업 중에 pull과 충돌이 날 수 있습니다. 커밋하기 전에 받아서 합칩니다.
- 번호가 겹치지 않는지, 태그가 위 여덟 가지 가운데 하나인지는 린트 `CheckBacklog`가 봅니다.

## 지난 이력

지나간 일의 이력은 `git log`에 있습니다. 2026-10-03까지의 이력이 모두 남은 옛 백로그(약 19,400줄)는 `git show 7ce95fc8:docs/06_Backlog.md`로 봅니다.
2026-10-16에 한 파일(`docs/06_Backlog.md`)이던 백로그를 이 폴더의 영역별 파일로 나눴습니다. 나누기 직전 판은 `git show 8031c7f1d:docs/06_Backlog.md`로 봅니다.

## 옛 번호 대응표

2026-10-13 이전의 글(커밋 메시지, 주석, 문서)은 영역 번호(1-1 ~ 1-12)를 썼습니다. 그 번호가 지금 어느 항목과 파일로 갈렸는지 적습니다.

- **1** (머리 항목): [engine.md](engine.md) `C-1`, [quality.md](quality.md) `C-2`
- **1-1** 직렬화 · 리플렉션: [engine.md](engine.md) `B-11`
- **1-2** 오브젝트 · 씬 · 틱 · 물리: [engine.md](engine.md) `D1-2` `D1-3`
- **1-3** 그래픽스 · RHI · 셰이더: [graphics.md](graphics.md) `D2-1` `D2-2` `D2-5` `D2-6` `D2-7` `D2-9`
- **1-4** 에디터: [editor.md](editor.md) `B-3` `B-4` `D3-1` `D3-3` `D3-4` `D3-5`
- **1-5** 핫 리로드 · 모듈: [engine.md](engine.md) `G-1`
- **1-6** 게임프레임워크 · 킷 · 게임: [engine.md](engine.md) `C-4` `D1-4`, [editor.md](editor.md) `D3-7`, [gameplay.md](gameplay.md) `D4-2` `D4-3` `D4-4` `D4-5` `D4-6` `D4-7` `D4-8` `D4-9` `D4-10` `D4-11` `D4-12` `D4-13`, [audio-ai-localization.md](audio-ai-localization.md) `D5-1` `D5-2` `D5-3`, [ui.md](ui.md) `D6-1` `D6-2` `D6-4` `D6-5` `D6-6`, [network.md](network.md) `G-2`, [project.md](project.md) `H-1`
- **1-7** Core · 태스크 · 네트워크: [network.md](network.md) `B-9` `B-10` `D7-1` `D7-2` `D7-3` `D7-5` `D7-6`
- **1-8** 성능: [engine.md](engine.md) `C-7`, [ui.md](ui.md) `C-8` `C-9`
- **1-9** 빌드 · 린트 · CI · 테스트: [quality.md](quality.md) `B-5` `C-15` `E-2`, [project.md](project.md) `C-11` `C-12` `C-16` `C-17`, [engine.md](engine.md) `C-13`
- **1-10** 관찰 중: [quality.md](quality.md) `B-6` `B-7` `E-1` `E-4` `E-5` `E-6` `E-7` `E-8`, [graphics.md](graphics.md) `E-3`
- **1-11** 결정이 필요한 것: [engine.md](engine.md) `H-2`
- **1-12** 낮은 우선순위: [ui.md](ui.md) `B-8` `G-3` `G-9` `G-10`, [project.md](project.md) `C-18` `G-11`, [gameplay.md](gameplay.md) `G-4` `G-6` `G-7`, [engine.md](engine.md) `G-5`, [network.md](network.md) `G-8`, [graphics.md](graphics.md) `G-12`

---

[◀ 문서 지도](../02_DocumentMap.md) | [🏠 위키 홈으로 돌아가기](../../README.md)
