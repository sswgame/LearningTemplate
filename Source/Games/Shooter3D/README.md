# Shooter3D — 슈터 테스트 게임(1인칭과 3인칭)

## 이 게임으로 무엇을 배우나

나무 상자 더미가 놓인 아레나에서 땅에서 일어나는 스켈레톤(KayKit)과 싸우는 슈터입니다. 플레이어는 KayKit 기사(투구, 망토, 방패, 오른손에 블래스터)이고,
3인칭, 궤도, CCTV 시점에서는 몸이 보입니다. 테스트 게임 중 엔진과 기반 기능을 가장 많이 엮은 게임입니다.

- 기반의 무기 규칙(`Base/Combat` 의 무기 카탈로그와 무기 상태)과 히트스캔(`RayMath`)
- 캐릭터 외형(`Base/Appearance`), 애니메이션 그래프와 상체 레이어, 소켓
- 페이싱 감독(`Base/AI/Director`)이 웨이브를 내는 방법
- 플레이어와 적이 같은 몸 이동 코드를 쓰고, 적은 내비메시 에이전트를 `SteerOnly` 로 쓰는 방법
- 카메라 프리셋과 CCTV 렌더 텍스처, HUD 뷰모델
- 자동화 시나리오 셋(무기 교체, 창 닫기, 자동 플레이 빙의)

총과 상자와 과녁은 Kenney Blaster Kit, 캐릭터는 KayKit(CC0, `Resource/game/shooter3d/credits.md`), 벽과 바닥은 내장 도형입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-Shooter3D
cmake --build --preset Ninja-Debug-Shooter3D
cd build/Ninja-Debug-Shooter3D/Bin
./App.exe -dx12
./App.exe -dx12 -gv_shooterAutoPlay=1                     # 조준과 사격과 이동도 AI
./App.exe -dx12 -gv_shooterAutoPlay=1 -gv_navDebugDraw=23  # 내비메시 걷는 면과 적의 경로를 화면에
./App.exe -dx12 -gv_cameraPreset=thirdperson               # 시작 카메라 프리셋(firstperson, thirdperson, orbit, cctv)
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/shooter3d/maps/arena.scene.xml"
```

`-gv_navDebugDraw` 는 비트 값입니다. 16 이 게임 화면의 걷는 면과 경로이고, 1, 2, 4 는 에디터 뷰포트의 선입니다.

움직임이 튀는 것을 진단할 때는 프레임마다 dt, 발과 몸의 위치와 요, 주요 본, 카메라, 적 하나를 CSV 로 남기고(끝날 때 씁니다) 연속 스크린샷과 같이 봅니다.

```powershell
./App.exe -dx11 -gv_shooterAutoPlay=1 -gv_cameraPreset=thirdperson -gv_profileFrames=2700 "-gv_shooterMotionTrace=trace.csv" `
          -gv_screenshotFrame=2500 -gv_screenshotCount=30 -gv_screenshotInterval=2 "-gv_screenshot=tp.ppm"
```

### 자동화 시나리오

시나리오는 `Resource/game/shooter3d/automation/` 에 있습니다. 고정 프레임 시간과 가상 입력으로 돌고, 결과는 종료 코드입니다.
`ctest --test-dir build/Ninja-Debug-Shooter3D -R AppTest_HostOnly` 가 셋을 백엔드마다 돌립니다.

```powershell
./App.exe -dx12 -scenario=game/shooter3d/automation/weaponswitch.scenario.xml   # E/Q 와 숫자 키가 무기를 정확히 한 단계씩
./App.exe -dx12 -scenario=game/shooter3d/automation/closewindow.scenario.xml    # 커서 잠금, Esc, 창 닫기
./App.exe -dx12 -scenario=game/shooter3d/automation/autoplay.scenario.xml       # 자동 플레이 = AI 빙의, 끄면 플레이어 키가 먹는다
```

- `weaponswitch` 는 E 와 Q 를 짧게, 길게, 같은 프레임에 눌러 무기 번호가 정확히 한 단계씩 바뀌는지 봅니다. 누르는 동안 매 프레임 바뀌는 회귀를 잡습니다.
- `closewindow` 는 진짜 커서 잠금(`ClipCursor`)을 보므로 OS 입력도 받는 `input="mixed"` 로 돕니다. 도는 동안 키보드와 마우스를 만지지 않습니다.
  전경 창을 얻지 못하는 세션(원격, 잠긴 화면)이나 Windows 밖에서는 13(건너뜀)으로 끝납니다.
- `autoplay` 는 자동 플레이를 켜면 AI 가 쏘고 맞히고, 끄면 플레이어 조종자가 폰을 되찾아 3 키로 권총을 고르는지 봅니다.

탐침은 `Shooter3D.WeaponIndex`, `PlayerAlive`, `ShotCount`, `EnemyHitCount`, `PlayerControllerKind`(0 플레이어, 1 AI, −1 없음)입니다.

## 조작 — 입력 맵 `data/shooter.input.xml`

게임 코드는 키를 묻지 않고 액션만 묻습니다. 키를 바꾸려면 이 파일을 고칩니다.

| 액션 | 기본 바인딩 | 하는 일 |
|------|-------------|---------|
| `Move` | WASD, 왼쪽 스틱 | 이동 |
| `Look` | 마우스 이동량 | 시점. 플레이어 조종자가 조종 회전에 더하고 1인칭 카메라가 따릅니다 |
| `ToggleMouseLock` | Esc | 마우스 잠금을 풀고 다시 겁니다 |
| `Fire` | 왼쪽 버튼, RB | 쏘기. 소총은 누르고 있으면 연사, 산탄총과 권총은 누를 때마다 |
| `Jump`, `Sprint` | Space, LeftShift | 점프, 달리기 |
| `Reload` | R | 재장전(빈 탄창은 저절로) |
| `Weapon1..3`, `SwitchWeapon` | 1, 2, 3, Q/E | 소총, 산탄총, 권총, 이전과 다음 |
| `CycleCamera` | C | 카메라 프리셋 돌리기: 1인칭, 3인칭(어깨 너머), 궤도, CCTV |
| `Camera.Look`, `Camera.LookHold`, `Camera.Zoom` | 마우스 이동, 오른쪽 버튼, 휠 | 궤도 프리셋의 시점, 누른 동안만 시점, 확대 |

스켈레톤은 다가와 칼이나 도끼를 휘두르고, 클립의 맞는 시각에 손이 닿으면 피해를 줍니다. 맞으면 움찔하고 HP 바가 뜨며, 쓰러지면 쓰러짐 클립 뒤 3초 있다가 정리됩니다.
플레이어 체력은 `Vitality` 이고 4초 동안 안 맞으면 다시 찹니다. 바닥나면 쓰러짐 클립(2.5초) 뒤 웨이브 1 부터 다시 시작합니다.

## 작동 원리

### 조종 — 폰, 조종자, 의도

플레이어와 스켈레톤은 **폰**(`PawnComponent`)이고, 몸은 폰의 의도만 읽습니다. 누가 의도를 내는지는 조종자가 정합니다.

| 폰 | 버튼 | 몸 이동 | 조종자 |
|----|------|---------|--------|
| 플레이어(씬의 `Player`) | `Jump`, `Sprint`, `Fire`, `Reload`, `SwitchWeapon`, `Weapon1..3` | `ShooterBodyMovementComponent` | 사람은 `PlayerControllerComponent`, 자동 플레이는 `ShooterAutoAimControllerComponent` |
| 스켈레톤(`prefabs/skeleton.prefab.xml`) | `Attack` | 같은 `ShooterBodyMovementComponent` 와 내비메시 에이전트(`SteerOnly`) | `ShooterEnemyAiControllerComponent`(`skeleton_ai.prefab.xml`) |

스켈레톤의 AI 조종자는 자동 빙의 `Ai` 가 만들고, 폰과 함께 지워집니다.

**자동 플레이는 AI 조종자의 빙의입니다.** 스위치(`-gv_shooterAutoPlay`, 씬의 `_bAutoPlay`, 에디터 툴바)가 바뀌면 디렉터가 틱 뒤에 플레이어 폰을 자동 조준 AI 에게 넘기고, 끄면 플레이어 0 의 조종자에게 돌려줍니다.
플레이어 몸(`ShooterPlayerComponent`)에는 자동 플레이 분기가 없습니다.
자동 조준 AI 는 가까운 적의 가슴에 초점을 두고 초당 3라디안으로 돕니다. 조준 오차가 3도 안이고 7m 안이면 `Fire`, 8m 안이면 산탄총, 밖이면 소총을 누르고, 아레나 가운데를 도는 나선을 따라 걷습니다.

**적의 판단은 조종자에 있습니다.** 쫓기(플레이어가 0.5m 넘게 움직이면 경로를 다시 구함), 손 닿는 거리의 0.85 안에서 서기, 닿으면 `Attack` 누르기는 조종자가 합니다.
휘두르기 간격, 맞는 시각, 움찔, 쓰러짐은 폰(`ShooterEnemyComponent`)의 규칙입니다.

**몸의 회전에는 상한이 있습니다.** 몸은 시점 요를 초당 10라디안(`ShooterAvatarComponent._turnRate`)까지만 따라가고, 자동 조준도 초당 3라디안으로 돕니다.
상한이 없으면 표적을 바꿀 때 한 프레임에 수십 도를 돌아 3인칭 몸과 카메라가 튑니다.

### 페이싱

감독(`data/arena.director.xml`, 스폰 예산 `data/arena.spawns.xml`)이 쌓기, 절정, 쉼을 돕니다. 쌓기에서는 스켈레톤이 예산만큼 조금씩 나오고, 단계 곡선에 따라 늘어납니다.
긴장도(맞은 피해, 쓰러뜨린 적, 7m 안 적 수, 탄 부족)가 0.7 을 넘거나 45초가 지나면 절정입니다. 절정에 들어서면 무리(`swarm`)가 한꺼번에 오고,
두 번째 순환부터는 가끔 체력이 두 배인 정예(`elite`, 전사 프리셋)가 섞입니다. 쉼에는 적을 내지 않고 탄을 채우며(`ammo`), 식으면 다시 쌓기입니다.
그때가 다음 웨이브이고(웨이브 = 순환 + 1), 웨이브마다 체력과 속도가 오릅니다. 씨앗은 디렉터의 `_pacingSeed` 입니다.
새 웨이브와 쓰러짐은 텔레메트리 진행 이벤트(`data/shooter3d.telemetry.xml`)로 남습니다.

### 캐릭터 — 외형, 애니메이션, 소켓

| 무엇 | 어디 |
|------|------|
| 외형 데이터 | `data/appearance/` 와 `data/items.xml`. 게임이 `AppearanceDatabase` 를 게임 서비스로 등록합니다 |
| 플레이어 모습 | 프리셋 `ShooterPlayer`. 기사 몸과 기사 세트(다 갖추면 망토가 기사 망토로), 주무기 블래스터, 푸른 염색 |
| 적 모습 | `SkeletonMinion`, `SkeletonRaider`(씨앗마다 몸과 두건과 무기), `SkeletonRogue`, 정예 `SkeletonWarrior`(투구가 두건을 감춥니다 — 규칙 `HelmetHidesHood`) |
| 몸 소켓 | `data/sockets/kaykit_humanoid.sockets.xml`. Helmet, Back, Gun, Blade, Shield 와 Eyes(1인칭 눈높이), Chest(히트박스 중심) |
| 무기 소켓 | `data/sockets/blaster_*.sockets.xml` 의 `Muzzle`(탄도선, 섬광)과 `SupportHand`. 외형에서는 `MainHand.Muzzle` |
| 애니메이션 그래프 | `data/anim/adventurer.animgraph.json`, `skeleton.animgraph.json` |
| 상체 레이어 | `ShooterAvatarComponent` 가 `spine` 아래에 겨눈 자세와 막 쏜 자세를 덮습니다. 맞음과 쓰러짐 동안은 내립니다 |
| 텍스처와 머티리얼 | 원본 GLB 의 아틀라스를 `textures_raw/kaykit_*.png` 로 꺼내 BC1 DDS 로 만들고, `materials/kaykit_*.material` 로 씁니다 |

플레이어 그래프의 `Move` 파라미터는 0 서기, 1 걷기, 2 달리기, 3 뒷걸음, 4 와 5 옆걸음, 6 공중이고, `Hit` 와 `Dead` 가 있습니다. 스켈레톤 그래프는 땅에서 일어나기로 시작합니다.

**총구.** 1인칭이면 손에 든 총(카메라의 자식)과 그 무기 외형의 `Muzzle` 소켓을, 아니면 몸 외형의 `MainHand.Muzzle`(지난 프레임 포즈)을 씁니다.
판정은 눈에서 쏘고, 탄도선은 총구에서 맞은 곳까지 잇습니다.

### 구조

아레나는 씬 하나(`Resource/game/shooter3d/maps/arena.scene.xml`)입니다.

| 무엇 | 어디 |
|------|------|
| 바닥, 벽, 엄폐물(나무 상자 더미는 엄폐물의 자식), 해, 적 스폰 위치 여덟, 플레이어, 디렉터 | 씬 |
| 벽과 엄폐물의 충돌 상자 | `ShooterBlockerComponent`. 디렉터가 플레이 시작에 모읍니다 |
| 스켈레톤, 탄착과 섬광, 탄도선 | 프리팹 `skeleton`, `skeleton_ai`, `effect`, `tracer`. 효과는 디렉터가 미리 만들어 숨겨 둔 풀에서 꺼내 씁니다 |
| 플레이어의 몸 | 프리팹 `player_body.prefab.xml`. 플레이어가 플레이 시작에 만듭니다. 장비는 `prefabs/kaykit/*`, `prefabs/blaster_*` |
| 페이싱, 쓰러뜨린 수, 효과 풀, 로그, 자동 플레이 빙의 | `ShooterDirectorComponent` |
| 무기 셋, 히트스캔(적은 캡슐), 체력, 탄도선 요청 | `ShooterPlayerComponent`. 카메라, 폰, 몸 이동, 손에 든 총, HUD 와 같은 오브젝트입니다 |
| HUD(조준선, 맞음 표시, 체력, 탄약, 무기 이름) | 플레이어 오브젝트의 `HUDControllerComponent` 가 `ui/hud.ui.xml` 을 열고, `ShooterPlayerComponent::updateHUD` 가 틱 뒤에 `HUDViewModel` 에 값을 넣습니다 |
| 걷기, 달리기, 점프, 중력, 상자에 미끄러지기 | `ShooterBodyMovementComponent`. 스스로 틱하지 않고 같은 오브젝트의 규칙 컴포넌트가 자기 틱에서 부릅니다 |
| 몸이 플레이어를 따르기, 애니메이터 파라미터, 상체 레이어 | `ShooterAvatarComponent`(몸 오브젝트) |
| 적 하나 | `ShooterEnemyComponent`. 일어나기, 쫓기, 휘두르기, 움찔, 쓰러짐 |
| 내비메시 | 씬의 `NavMeshSurfaceComponent`(보이는 메시로 Humanoid 베이크), 플레이어의 `NavMeshModifierComponent`(손에 든 총과 카메라를 뺍니다), 스켈레톤의 `NavMeshAgentComponent`(`SteerOnly`) |
| 1인칭 시점과 손에 든 총 위치 | GameFramework `FirstPersonCameraComponent` |
| 화면에 나가는 시점 | `ViewCamera` 오브젝트(우선순위 10)의 `CameraDirectorComponent`. 프리셋은 `data/shooter.cameras.xml` 입니다 |
| 감시 카메라와 모니터 | `CctvCamera` 가 렌더 텍스처 `rendertarget/shooter_cctv` 에 `cctv_sweep` 프리셋으로 그리고, 북쪽 벽 `CctvMonitor` 가 그 텍스처를 읽습니다 |

HUD 의 위젯 값은 문서의 `{bind:필드}` 가 연결합니다. 무기 이름은 현지화 키 그대로 넣고, 글 위젯이 문화권에 맞게 바꿉니다. 적 HP 바는 스켈레톤 프리팹의 화면 마커와 `HealthBarComponent` 입니다.
1인칭 프리셋이면 플레이어가 몸을 숨기고 손에 든 총과 조준선을 보이며, 다른 프리셋에서는 반대입니다.
내비메시는 쿠킹이 `maps/arena.navmesh` 로 쓰고, Dev 는 플레이 첫 프레임에 베이크합니다.

**틱.** 디렉터는 `PrePhysics` 에서 쓰러진 적을 세고 시체를 정리하고 이번 프레임의 적과 플레이어 위치를 적습니다.
1인칭 카메라도 `PrePhysics` 에서 폰의 조종 회전으로 시점을 둡니다. 플레이어와 적은 `DuringPhysics` 에서 그것을 읽기만 하고 자기 오브젝트에만 씁니다.
몸(`ShooterAvatarComponent`)은 `PostPhysics` 에서 플레이어를 읽고 자기 위치와 애니메이터를 씁니다.
다른 오브젝트에 쓰는 일(적과 플레이어에 피해, 효과와 탄도선 꺼내기, 효과음, 무기 바꾸기, 몸 보이기)은 쌓아 두고 틱 뒤에 합니다.
애니메이션은 틱 뒤에 평가되고, 외형 컴포넌트가 몸의 포즈가 끝난 프레임에 무기와 투구 위치를 고칩니다.

**소리.** `Shooter3DGame` 이 사운드 이벤트(`audio/shooter3d.audioevents.xml`)를 로드하고, 소리는 이벤트 이름으로 냅니다.
착지와 명중음은 2D(`Land`, `HitEnemy`, `HitCover`)이고, 적 처치는 그 위치의 3D 소리(`EnemyDown`, 벽 뒤면 가려짐)입니다. 클립과 범위와 쿨다운은 이벤트 파일에 있습니다.

**핫 리로드와 상태 저장.** 처치 수와 페이싱 감독 상태가 디렉터의 `writeState` 로 실립니다. 다시 만든 디렉터는 감독 상태(단계, 웨이브, 예산)를 이어 받고,
감독 예산으로 서 있던 적을 같은 스폰 id 로 다시 만듭니다. 무리와 정예는 정리된 채로 두고, 플레이어는 몸을 다시 만듭니다.
무기 카탈로그와 외형 데이터는 게임 서비스입니다.

## 데이터

- `data/weapons.xml` 에 연사 간격, 피해, 퍼짐, 반동, 탄창이 모두 있습니다.
- 몸 이동, 체력, 적의 값은 컴포넌트 PROPERTY 이고 적은 프리팹에 있습니다. 적의 걷기 빠르기는 웨이브마다 디렉터가 넣습니다.
- `textures_raw/crosshair.png` 와 `hitmarker.png` 는 Kenney "Starter Kit FPS" 의 스프라이트(CC0)이고, `App --import-textures` 가 DDS 로 만듭니다.

## 함정과 주의

- **씬과 프리팹은 엔진 직렬화기가 쓴 파일입니다.** 손으로 고치면 형식을 깨기 쉬우므로 에디터로 고칩니다.
- **손 IK, 맞은 부위, 래그돌, 무기 떨어뜨리기는 아직 없습니다.** 애니메이션 리그와 게임플레이 작업이 들어오면 `MainHand.SupportHand` 같은 소켓과 물리 에셋을 씁니다.

## 더 볼 곳

- [Appearance](../../GameFramework/Base/Gameplay/Appearance/README.md) — 외형 데이터 형식
- [AI/Director](../../GameFramework/Base/Actor/AI/Director/README.md) — 페이싱 감독
- [GameFramework](../../GameFramework/README.md) — 폰과 조종자, 카메라 프리셋, HUD
- [Automation](../../Engine/Automation/README.md) — 시나리오 형식
