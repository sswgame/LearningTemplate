# Resource

엔진이 로드하는 에셋의 최상위 폴더입니다. `Resource/` 자체는 표시·감시용이며, 파일은 아래 팩 아래에만 둡니다.

| 팩 | 경로 | 내용 |
| --- | --- | --- |
| Engine | `engine/` | 파이프라인, 코어 셰이더, 폴백 머티리얼, 내장 텍스처, 셸 InputMap, `enginedata.xml` |
| Common | `common/` | 게임 팩이 공유하는 셰이더(아웃라인, 샘플 컴퓨트 등) |
| Game | `game/<pack>/` | 해당 게임 콘텐츠(맵, 프리팹, 텍스처, 오디오, `gamedata.xml`) |
| Editor | `editor/` | 에디터 부트스트랩(`editordata.xml`). 유저 레이아웃은 `Config/Editor` |

## 경로 규칙

- 코드에서 드라이브 절대경로는 쓰지 않습니다.
- 검색 시 경로는 소문자로 정규화됩니다. 파일명은 소문자·숫자·언더바를 권장합니다.
- 전역 ID: `engine/pipeline/forwardpipeline.xml`, `common/shaders/postoutline.hlsl`, `game/demo/maps/town01.xml`, `editor/data/editordata.xml`
- 팩 상대 키: `pipeline/forwardpipeline.xml` → `game/<pack>/` → `common/` → `engine/` → `editor/` 순으로 검색
- 셸 InputMap: `engine/input/emergency.input.xml` (폴백). 게임플레이: `game/<pack>/input/default.input.xml`
