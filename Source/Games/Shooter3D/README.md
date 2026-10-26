# Shooter3D — 슈터 테스트 게임(1인칭과 3인칭)

## 이 게임으로 무엇을 배우나

나무 상자 더미가 놓인 아레나에서 땅에서 일어나는 스켈레톤(KayKit)과 싸우는 슈터입니다. 플레이어는 KayKit 기사(블래스터를 든다)이고 3인칭, 궤도, CCTV 시점에서는 몸이 보입니다. 테스트 게임 중 엔진과 기반 기능을 가장 많이 엮은 게임입니다.

- 기반의 무기 규칙(`Base/Combat` 의 무기 카탈로그와 무기 상태)과 히트스캔(`RayMath`), 캐릭터 외형(`Base/Appearance`), 애니메이션 그래프와 상체 레이어, 소켓
- 페이싱 감독(`Base/AI/Director`)이 웨이브를 내는 방법, 플레이어와 적이 같은 몸 이동 코드를 쓰고 적은 내비메시 에이전트를 `SteerOnly` 로 쓰는 방법
- 카메라 프리셋과 CCTV 렌더 텍스처, HUD 뷰모델, 자동화 시나리오(무기 교체, 창 닫기, 자동 플레이 빙의)

총과 상자와 과녁은 Kenney Blaster Kit, 캐릭터는 KayKit(CC0, `Resource/game/shooter3d/credits.md`), 벽과 바닥은 내장 도형입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=Shooter3D -dx12
./App.exe -game=Shooter3D -dx12 -gv_shooterAutoPlay=1                     # 조준과 사격과 이동도 AI
./App.exe -game=Shooter3D -dx12 -gv_shooterAutoPlay=1 -gv_navDebugDraw=23  # 내비메시 걷는 면과 적의 경로를 화면에
./App.exe -game=Shooter3D -dx12 -gv_cameraPreset=thirdperson               # 시작 카메라 프리셋(firstperson, thirdperson, orbit, cctv)
./App.exe -game=Shooter3D -dx12 -EnableEditor "-gv_editorStartupScene=game/shooter3d/maps/arena.scene.xml"
```

`-gv_navDebugDraw` 는 비트 값입니다(16 이 게임 화면의 걷는 면과 경로, 1 · 2 · 4 는 에디터 뷰포트의 선). 움직임이 튀는 것을 진단할 때는 프레임마다 dt, 발과 몸의 위치와 요, 주요 본, 카메라, 적 하나를 CSV 로 남기고(끝날 때 씀) 연속 스크린샷과 같이 봅니다.

```powershell
./App.exe -game=Shooter3D -dx11 -gv_shooterAutoPlay=1 -gv_cameraPreset=thirdperson -gv_profileFrames=2700 "-gv_shooterMotionTrace=trace.csv" `
          -gv_screenshotFrame=2500 -gv_screenshotCount=30 -gv_screenshotInterval=2 "-gv_screenshot=tp.ppm"
```

### 자동화 시나리오

`Resource/game/shooter3d/automation/` 의 시나리오(`weaponswitch` — E/Q 와 숫자 키가 무기를 정확히 한 단계씩, `closewindow` — 커서 잠금 · Esc · 창 닫기, `autoplay` — 자동 플레이 = AI 빙의 / 끄면 플레이어 키가 먹는다, `fieldofview` — 팩 설정의 기본 시야각 80 이 `gv_cameraFieldOfView` 로 적용됐는지)를
`ctest --test-dir build/Ninja-Debug -R AppTest_HostOnly` 가 백엔드마다 돌립니다. `./App.exe -game=Shooter3D -dx12 -scenario=game/shooter3d/automation/<이름>.scenario.xml` 로 하나씩 돌립니다.
`closewindow` 는 진짜 커서 잠금(`ClipCursor`)을 보므로 OS 입력도 받는 `input="mixed"` 로 돌고 도는 동안 키보드와 마우스를 만지지 않습니다(전경 창을 얻지 못하는 세션이나 Windows 밖에서는 13).
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

스켈레톤은 다가와 칼이나 도끼를 휘두르고 클립 알림이 알린 맞는 시각에 손이 닿으면 피해를 줍니다. 쓰러지면 래그돌로 넘어져 3초 뒤 정리됩니다. 플레이어 체력은 `Vitality` 이고 4초 동안 안 맞으면 다시 차며 바닥나면 쓰러짐 클립(2.5초) 뒤 웨이브 1 부터 다시 시작합니다.

## 작동 원리

### 조종 — 폰, 조종자, 의도

플레이어(씬의 `Player`)와 스켈레톤(`prefabs/skeleton.prefab.xml`)은 **폰**(`PawnComponent`)이고 같은 몸 이동(`ShooterBodyMovementComponent`)이 폰의 의도만 읽습니다. 누가 의도를 내는지는 조종자가 정합니다: 플레이어는 사람이면 `PlayerControllerComponent`, 자동 플레이면 `ShooterAutoAimControllerComponent`,
스켈레톤은 자동 빙의 `AI` 가 만들고 폰과 함께 지워지는 `ShooterEnemyAIControllerComponent`(`skeleton_ai.prefab.xml`)입니다.

**자동 플레이는 AI 조종자의 빙의입니다.** 스위치(`-gv_shooterAutoPlay`, 씬의 `_bAutoPlay`, 에디터 툴바)가 바뀌면 디렉터가 틱 뒤에 플레이어 폰을 자동 조준 AI 에게 넘기고 끄면 플레이어 0 의 조종자에게 돌려줍니다(플레이어 몸 `ShooterPlayerComponent` 에는 자동 플레이 분기가 없다).
자동 조준 AI 는 막는 상자에 가리지 않은 가장 가까운 적의 가슴을 초당 3라디안으로 겨누고(보이는 적이 없으면 쏘지 않는다) 조준 오차 3도 안 · 7m 안이면 `Fire`, 8m 안이면 산탄총, 밖이면 소총을 누르며 아레나 가운데를 도는 나선을 따라 걷습니다.
**적의 판단은 조종자에 있습니다**(쫓기 — 플레이어가 0.5m 넘게 움직이면 경로 재계산, 손 닿는 거리의 0.85 안에서 서기, 닿으면 `Attack`). 휘두르기 간격 · 맞는 시각 · 움찔 · 쓰러짐은 폰(`ShooterEnemyComponent`)의 규칙입니다.

**막 누른 틱의 사격은 클릭한 순간의 시선으로 나갑니다**(서브틱 입력). 플레이어 폰의 `_triggerLookButton`(씬에서 `Fire`)이 정한 버튼이 눌린 틱에 플레이어 조종자가 프레임 시작의 조종 회전에 그 순간까지의 마우스 이동만 더해 의도의 발동 시선(`ControlIntent::_triggerYaw` · `_triggerPitch`)에 싣고 `ShooterPlayerComponent::onTick` 이 그 시선으로 쏩니다.
누르고 있는 틱, AI 조종자(발동 시선 없음), 누름 순간을 모르는 바인딩은 지금 카메라 시선입니다. `_triggerLookButton` 의 버튼 이름은 코드의 `kFireButton` 과 같아야 하고 비어 있으면 발동 시선을 싣지 않습니다.
**몸의 회전에는 상한이 있습니다**(몸은 시점 요를 초당 10라디안 `_turnRate` 까지만 따라가고 자동 조준은 초당 3라디안). 상한이 없으면 표적을 바꿀 때 3인칭 몸과 카메라가 튑니다.

### 페이싱

감독(`data/arena.director.xml`, 스폰 예산 `data/arena.spawns.xml`)이 쌓기, 절정, 쉼을 돕니다. 쌓기에서는 스켈레톤이 예산만큼 조금씩 나오고 단계 곡선에 따라 늘어납니다. 긴장도(맞은 피해, 쓰러뜨린 적, 7m 안 적 수, 탄 부족)가 0.7 을 넘거나 45초가 지나면 절정이고 무리(`swarm`)가 한꺼번에 오며
두 번째 순환부터는 가끔 체력이 두 배인 정예(`elite`)가 섞입니다. 쉼에는 적을 내지 않고 탄을 채우며(`ammo`) 식으면 다시 쌓기이고 그때가 다음 웨이브(웨이브 = 순환 + 1)이며 웨이브마다 체력과 속도가 오릅니다. 씨앗은 디렉터의 `_pacingSeed`, 새 웨이브와 쓰러짐은 텔레메트리 진행 이벤트(`data/shooter3d.telemetry.xml`)로 남습니다.

### 캐릭터 — 외형, 애니메이션, 소켓

외형 데이터는 `data/appearance/` 와 `data/items.xml` 이고 게임이 `AppearanceDatabase` 를 게임 서비스로 등록합니다. 모습은 프리셋 `ShooterPlayer`(기사 세트, 블래스터, 푸른 염색)와 `SkeletonMinion` · `SkeletonRaider` · `SkeletonRogue`, 정예 `SkeletonWarrior`(투구가 두건을 감춘다 — 규칙 `HelmetHidesHood`)입니다.
소켓은 몸 `data/sockets/kaykit_humanoid.sockets.xml`(Helmet, Back, Gun, Blade, Shield, Eyes = 1인칭 눈높이, Chest = 히트박스 중심)과 무기 `data/sockets/blaster_*.sockets.xml` 의 `Muzzle` · `SupportHand`(외형에서는 `MainHand.Muzzle`)입니다.
애니메이션 그래프는 `data/anim/adventurer.animgraph.xml` · `skeleton.animgraph.xml` 이고 레이어 `Aim` · `Shoot`(마스크 `data/anim/masks/upper_body.bonemask.xml`)이 겨눈 자세와 막 쏜 자세를 덮습니다(`ShooterAvatarComponent` 는 가중치 파라미터 `AimWeight` · `ShootWeight` 만 쓰고 맞음과 쓰러짐 동안은 내린다).
텍스처는 원본 GLB 의 아틀라스를 `textures_raw/kaykit_*.png` 로 꺼내 BC1 DDS 로 만들고 `materials/kaykit_*.material` 로 씁니다. 플레이어 그래프의 `Move` 파라미터는 0 서기, 1 걷기, 2 달리기, 3 뒷걸음, 4 와 5 옆걸음, 6 공중이고 `Hit` · `Dead` 가 있으며 스켈레톤 그래프는 땅에서 일어나기로 시작합니다.
**총구.** 1인칭이면 손에 든 총(카메라의 자식)과 그 무기 외형의 `Muzzle` 을, 아니면 몸 외형의 `MainHand.Muzzle`(지난 프레임 포즈)을 씁니다. 판정은 눈에서 쏘고 탄도선은 총구에서 맞은 곳까지 잇습니다.

### 구조

아레나는 씬 하나(`Resource/game/shooter3d/maps/arena.scene.xml`)에 바닥, 벽, 엄폐물, 해, 적 스폰 위치 여덟, 플레이어, 디렉터가 있고 스켈레톤 · 탄착과 섬광 · 탄도선 · 플레이어 몸은 프리팹입니다(효과는 디렉터가 미리 만들어 숨겨 둔 풀에서 꺼낸다). 컴포넌트의 역할은 다음과 같습니다.

- `ShooterDirectorComponent` — 페이싱, 쓰러뜨린 수, 효과 풀, 로그, 자동 플레이 빙의. `ShooterBlockerComponent`(벽 · 엄폐물의 충돌 상자)를 플레이 시작에 모읍니다.
- `ShooterPlayerComponent` — 무기 셋, 히트스캔(적은 래그돌 히트박스 물리 질의), 체력, 탄도선 요청. 카메라, 폰, 몸 이동, 손에 든 총, HUD 와 같은 오브젝트이고 `updateHUD` 가 틱 뒤에 `ShooterHUDViewModel` 을 걸고 값을 넣습니다(`HUDControllerComponent` 가 `ui/hud.ui.xml` 을 연다).
- `ShooterBodyMovementComponent`(스스로 틱하지 않고 같은 오브젝트의 규칙 컴포넌트가 자기 틱에서 부른다), `ShooterAvatarComponent`(몸이 플레이어를 따르기, 애니메이터 파라미터, 상체 레이어), `ShooterEnemyComponent`(일어나기, 쫓기, 휘두르기, 움찔, 쓰러짐).
- 내비메시는 씬의 `NavMeshSurfaceComponent`(보이는 메시로 Humanoid 베이크, Dev 는 플레이 첫 프레임에 베이크하고 쿠킹은 `maps/arena.navmesh`), 플레이어의 `NavMeshModifierComponent`(손에 든 총과 카메라를 뺀다), 스켈레톤의 `NavMeshAgentComponent`(`SteerOnly`)입니다.
- 시점은 `ViewCamera`(우선순위 10)의 `CameraDirectorComponent`(프리셋 `data/shooter.cameras.xml`)가 정하고 1인칭 시점과 손에 든 총 위치는 `FirstPersonCameraComponent` 입니다. 감시 카메라 `CctvCamera` 는 렌더 텍스처 `rendertarget/shooter_cctv` 에 `cctv_sweep` 프리셋으로 그리고 북쪽 벽 `CctvMonitor`(셰이더 `shaders/cctvmonitor.hlsl`)가 읽습니다.

HUD 의 위젯 값은 문서의 `{bind:필드}` 가 연결하고 무기 이름은 현지화 키 그대로 넣습니다. 1인칭 프리셋이면 플레이어가 몸의 머리만 숨기고(리그 `rigs/player_body.rig.xml` 의 `HideHead`(ScaleBone, 슬롯 `FirstPersonHead`)와 외형의 숨기는 슬롯 `Head` · `MainHand`) 손에 든 총과 조준선을 보이며 다른 프리셋에서는 반대입니다.
적 몸은 군중 공유(`_bShareCrowdPose`)를 켜 같은 반복 클립의 적끼리 포즈 하나를 나눕니다.

**틱.** 디렉터는 `PrePhysics` 에서 쓰러진 적을 세고 시체를 정리하고 이번 프레임의 적과 플레이어 위치를 적습니다. 1인칭 카메라도 `PrePhysics` 에서 폰의 조종 회전으로 시점을 둡니다. 플레이어와 적은 `DuringPhysics` 에서 그것을 읽기만 하고 자기 오브젝트에만 쓰며 몸(`ShooterAvatarComponent`)은 `PostPhysics` 에서 플레이어를 읽고 자기 위치와 애니메이터를 씁니다.
다른 오브젝트에 쓰는 일(피해, 효과와 탄도선 꺼내기, 효과음, 무기 바꾸기, 몸 보이기)은 쌓아 두고 틱 뒤에 합니다. 애니메이션은 틱 뒤에 평가되고 외형 컴포넌트가 몸의 포즈가 끝난 프레임에 무기와 투구 위치를 고칩니다.

**소리.** `Shooter3DGame` 이 사운드 이벤트(`audio/shooter3d.audioevents.xml`)를 로드하고 이벤트 이름으로 냅니다(착지와 명중음은 2D `Land` · `HitEnemy` · `HitCover`, 적 처치는 그 위치의 3D `EnemyDown` — 벽 뒤면 가려짐).

**핫 리로드와 상태 저장.** 처치 수와 페이싱 감독 상태가 디렉터의 `writeState` 로 실립니다. 다시 만든 디렉터는 감독 상태(단계, 웨이브, 예산)를 이어받아 서 있던 적을 같은 스폰 id 로 다시 만들고(무리와 정예는 정리된 채로 두고 플레이어는 몸을 다시 만든다) 무기 카탈로그와 외형 데이터는 게임 서비스입니다.

## 데이터

무기 값(연사 간격, 피해, 퍼짐, 반동, 탄창)은 `data/weapons.xml`, 몸 이동 · 체력 · 적의 값은 컴포넌트 PROPERTY(적은 프리팹, 걷기 빠르기는 웨이브마다 디렉터가 넣는다)입니다. 조준선 · 히트마커 스프라이트는 Kenney "Starter Kit FPS"(CC0)이고 `App --import-textures` 가 DDS 로 만듭니다.

## 함정과 주의

- **씬과 프리팹은 엔진 직렬화기가 쓴 파일입니다.** 손으로 고치면 형식을 깨기 쉬우므로 에디터로 고칩니다.
- **적의 히트박스는 래그돌 바디입니다.** 스켈레톤 프리팹의 `RagdollComponent`(물리 에셋 `characters/skeleton_warrior/skeleton_warrior.physics.xml`)가 뼈마다 키네마틱 바디를 두고 플레이어의 사격은 틱 뒤에 막는 상자까지의 거리 안에서 그 바디를 물리 질의로 묻습니다(머리는 피해 ×2, 쓰러지면 래그돌로 넘어진다). 막는 상자는 플레이 시작에 같은 상자의 정적 강체를 만들고 바닥은 씬의 정적 강체입니다.
- **휘두름이 닿는 시각은 클립 알림입니다.** 공격 클립의 `Impact`(곁 데이터 `models_raw/kaykit/skeleton_minion.clips.json`)를 알림 표가 `SwingLands` 로 보냅니다. 곁 데이터를 바꾸면 `App --import-models` 로 다시 가져와야 클립에 알림이 들어갑니다.
- **왼손 IK 와 스프링 망토는 아직 없습니다.** 무기 부품이 정적 메시라 리그의 소켓 대상(스킨드 유닛만 받는다)이 `MainHand.SupportHand` 를 따라갈 수 없고 망토는 사슬 뼈가 없는 정적 메시입니다.
- **플레이어 몸은 재생 속도와 보폭으로 발 미끄러짐을 줄입니다.** `ShooterAvatarComponent` 가 지금 이동 클립의 발 속도를 한 번 측정해(`LocomotionSpeedUtil`) 수평 속도를 그 값으로 나눈 재생 속도를 `_minPlayRate` · `_maxPlayRate` 사이로 쓰고(상체 레이어도 같은 비율로 빨라진다) 상한에 걸린 몫(뒷걸음 · 옆걸음)은 보폭 워핑 노드(`Stride`)가 맞춥니다.

## 더 볼 곳

- [게임 목록](../README.md)
