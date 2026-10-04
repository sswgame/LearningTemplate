# Shooter3D — 슈터 시험 게임(1인칭 · 3인칭)

기반의 무기 규칙(`GameFramework/Combat` — 무기 카탈로그 · 무기 상태, 히트스캔은 `RayMath` · 1인칭 시점은 `FirstPersonLook`)과 캐릭터 시스템(애니메이션 ·
외형 · 소켓)을 실제로 쓰는 아레나 슈터입니다. 나무 상자 더미가 놓인 아레나에 땅에서 스켈레톤(KayKit)이 일어나 다가옵니다. 언제 · 얼마나 오는지는 페이싱
감독(`GameFramework/AI/Director`)이 정합니다. 플레이어는 KayKit 기사(투구 · 망토 · 방패, 오른손에 블래스터)이고, 3인칭 · 궤도 · CCTV 시점에서 보입니다.
총 · 상자 · 과녁은 Kenney Blaster Kit, 캐릭터는 KayKit(CC0, `Resource/game/shooter3d/credits.md`), 벽 · 바닥은 내장 도형입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-Shooter3D
cmake --build --preset Ninja-Debug-Shooter3D
cd build/Ninja-Debug-Shooter3D/Bin
./App.exe -dx12
./App.exe -dx12 -gv_shooterAutoPlay=1   # 조준 · 사격 · 이동도 AI(가까우면 산탄총, 멀면 소총 — 8 m 안의 적부터 쏜다)
./App.exe -dx12 -gv_cameraPreset=thirdperson   # 시작 카메라 프리셋(firstperson · thirdperson · orbit · cctv) — 프리셋마다 스크린샷을 찍을 때
./App.exe -dx12 -EnableEditor "-gv_editorStartupScene=game/shooter3d/maps/arena.scene.xml"
# 튐 진단 — 프레임마다 dt · 발 · 몸 자리와 요 · 루트/골반/머리/손 본 · 카메라 · 적 하나를 CSV 로(끝날 때 쓴다). 연속 스크린샷과 같이 본다.
./App.exe -dx11 -gv_shooterAutoPlay=1 -gv_cameraPreset=thirdperson -gv_profileFrames=2700 "-gv_shooterMotionTrace=trace.csv" `
          -gv_screenshotFrame=2500 -gv_screenshotCount=30 -gv_screenshotInterval=2 "-gv_screenshot=tp.ppm"
```

몸은 시점 요를 각속도 상한(`ShooterAvatarComponent._turnRate`, 10 rad/s)으로 따라가고, 자동 조준도 3 rad/s 로 돈다 — 상한이 없으면 표적을 바꿀 때 한 프레임에
수십 도 돌아 3인칭 몸 · 카메라가 튄다.

## 조작 — 입력 맵 `data/shooter.input.xml`

게임 코드는 키를 묻지 않고 액션만 묻습니다(팩의 `data/gamesettings.xml` `<inputMap>` 이 통합 입력 맵에 읽힌다). 키를 바꾸려면 이 파일을 고칩니다.

| 액션 | 기본 바인딩 | 하는 일 |
|------|-------------|---------|
| `Move` | WASD · 왼쪽 스틱 | 이동 |
| `Look` | 마우스 이동량(`<mouseDelta>`) | 시점 — 1인칭 카메라의 `_lookAction`(마우스는 창 가운데에 잠긴다, Esc 로 풀고 다시 잠근다) |
| `Fire` | 왼쪽 버튼 · RB | 쏘기(소총은 누르고 있으면 연사, 산탄총 · 권총은 누를 때마다) |
| `Jump` · `Sprint` | Space · LeftShift | 점프 · 달리기 |
| `Reload` | R | 재장전(빈 탄창은 저절로) |
| `Weapon1..3` · `SwitchWeapon` | 1 · 2 · 3 · Q/E | 소총 · 산탄총 · 권총, 이전 · 다음 |
| `CycleCamera` | C | 카메라 프리셋 돌리기(시점 카메라 디렉터의 `_cycleAction`) — 1인칭 → 3인칭(어깨 너머, 스프링 암) → 궤도(오른쪽 버튼 끌기 · 휠) → CCTV |

스켈레톤은 다가와 칼 · 도끼를 휘두르고(클립의 맞는 시각에 손이 닿으면 피해), 맞으면 움찔하고 HP 바가 뜨며, 쓰러지면 흩어지는 쓰러짐 클립 뒤 3 초 남았다가
걷힙니다. 플레이어 체력은 `Vitality`(4 초 동안 안 맞으면 다시 찬다)이고, 바닥나면 쓰러짐 클립(2.5 초) 뒤 웨이브 1 부터 다시 시작합니다.
탄도선 · 총구 섬광은 디렉터의 효과 풀(숨겨 둔 상자 · 구 메시)이라 에디터 없이 App 에서도 보입니다.

**페이싱.** 감독(`data/arena.director.xml` · 스폰 예산 `data/arena.spawns.xml`)이 쌓기 → 절정 → 쉼을 돕니다. 쌓기는 스켈레톤이 예산만큼 조금씩(단계 곡선으로 늘며),
긴장도(맞은 피해 · 쓰러뜨린 적 · 7 m 안 적 수 · 탄 부족)가 0.7 을 넘거나 45 초가 지나면 절정 — 들어서며 무리(`swarm`, 두 번째 순환부터 가끔 체력 2 배 · 전사
프리셋의 `elite`)가 한꺼번에 오고, 쉼은 적을 내지 않고 탄을 채우며(`ammo`) 식으면 다시 쌓기 — 그때가 다음 웨이브입니다(웨이브 = 순환 + 1, 웨이브마다
체력 · 속도가 오른다). 씨앗은 디렉터의 `_pacingSeed` 입니다. 새 웨이브와 쓰러짐은 텔레메트리 진행 사건(`data/shooter3d.telemetry.xml`)으로 남습니다.

## 캐릭터 — 외형 · 애니메이션 · 소켓

| 무엇 | 어디 |
|------|------|
| 외형 데이터 | `data/appearance/`(칸 · 세트 · 아이템 외형 · 꾸미기 · 규칙 · 프리셋 — 형식은 `GameFramework/Appearance/README.md`)와 `data/items.xml`. 게임이 `AppearanceDatabase` 를 게임 서비스로 건다 |
| 플레이어 모습 | 프리셋 `ShooterPlayer` — 기사 몸 + 기사 세트(투구 · 망토 · 방패를 다 갖추면 망토가 기사 망토로) + MainHand 블래스터 + 푸른 염색(`OutfitDye` → 머티리얼 `color`) |
| 적 모습 | `SkeletonMinion` · `SkeletonRaider`(씨앗마다 몸 · 두건 · 무기) · `SkeletonRogue` · 정예 `SkeletonWarrior`(고른 두건을 투구가 감춘다 — 규칙 `HelmetHidesHood`). 뼈 색은 씨앗으로 뽑는다 |
| 몸 소켓 | `data/sockets/kaykit_humanoid.sockets.xml` — 임포트의 본 부착 표에서 옮긴 Helmet · Back · Gun · Blade · Shield 와 Eyes(1인칭 눈높이) · Chest(히트박스 중심) |
| 무기 소켓 | `data/sockets/blaster_*.sockets.xml` — `Muzzle`(탄도선 · 섬광) · `SupportHand`(손 IK 가 들어오면 왼손 목표). 외형에서는 `MainHand.Muzzle` |
| 애니메이션 그래프 | `data/anim/adventurer.animgraph.json`(파라미터 `Move` 0 서기 · 1 걷기 · 2 달리기 · 3 뒷걸음 · 4/5 옆걸음 · 6 공중, `Hit`, `Dead`) · `skeleton.animgraph.json`(`Move`, `Attack`, `Hit`, `Dead` — 시작은 땅에서 일어나기) |
| 상체 레이어 | `ShooterAvatarComponent` 가 `spine` 아래에 `1H_Ranged_Aiming`(겨눈 자세) · `1H_Ranged_Shooting`(막 쏜 동안)을 덮는다. 맞음 · 쓰러짐 동안은 내린다 |
| 텍스처 · 머티리얼 | 원본 GLB 에 든 아틀라스를 `textures_raw/kaykit_*.png` 로 꺼내 BC1 DDS(`Character_Atlases` 규칙) · `materials/kaykit_*.material` |

## 구조 — 씬 · 프리팹 · 컴포넌트

아레나는 씬 하나(`Resource/game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 레시피는 `Source/Games/README.md` 입니다.

| 무엇 | 어디 |
|------|------|
| 바닥 · 벽 넷 · 엄폐물 열(나무 상자 더미는 엄폐물의 자식) · 해 · 적 스폰 자리 여덟 · 플레이어 · 디렉터 | 씬(엔티티) — 에디터에서 옮긴다 |
| 벽 · 엄폐물의 충돌 상자 | `ShooterBlockerComponent`(오브젝트 자리 ± 반 크기) — 디렉터가 플레이 시작에 모은다 |
| 스켈레톤 · 탄착/섬광 구 · 탄도선 상자 | 프리팹 `prefabs/skeleton.prefab.xml`(스킨드 메시 · 애니메이터 · 외형 · HP 바 · `ShooterEnemyComponent`) · `effect.prefab.xml` · `tracer.prefab.xml` — 효과는 풀(디렉터가 미리 세워 숨겨 두고 꺼내 쓴다) |
| 플레이어의 몸 | 프리팹 `prefabs/player_body.prefab.xml`(스킨드 메시 · 애니메이터 · 외형 · `ShooterAvatarComponent`) — 플레이어가 플레이 시작에 세운다. 장비는 `prefabs/kaykit/*` · `prefabs/blaster_*` |
| 페이싱 · 쓰러뜨린 수 · 효과 풀 · 로그 | `ShooterDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 이동 · 점프 · 무기 셋 · 히트스캔(적은 캡슐) · 체력 · 조준선 · 탄도선 요청 | `ShooterPlayerComponent` — 플레이어 오브젝트(카메라 · 1인칭 손에 든 총 · 조준선 스프라이트와 같은 오브젝트) |
| 몸이 플레이어를 따르기 · 애니메이터 파라미터 · 상체 레이어 | `ShooterAvatarComponent`(몸 오브젝트) |
| 적 하나 | `ShooterEnemyComponent` — 일어나기 → 쫓기(이웃과 떨어지고 상자를 돌아감) → 휘두르기 → 움찔 → 쓰러짐 |
| 1인칭 시점 · 마우스 잠금 · 손에 든 총 자리 | GameFramework `FirstPersonCameraComponent`(플레이어 오브젝트) |
| 화면에 나가는 시점 | `ViewCamera` 오브젝트(우선순위 10)의 `CameraDirectorComponent` — 프리셋 `data/shooter.cameras.xml`, 대상은 플레이어. 1인칭 프리셋이면 플레이어가 몸을 숨기고 손에 든 총 · 조준선을 보인다(그 밖은 반대) |
| 감시 카메라 · 모니터 | `CctvCamera`(렌더 텍스처 `rendertarget/shooter_cctv`)의 디렉터가 `data/cctv.cameras.xml` 의 `cctv_sweep` 을 쓴다. 북쪽 벽 `CctvMonitor` 가 그 텍스처를 읽는다 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 쓰러진 적을 세고(처치 수 · 효과음 · 감독의 예산 자리) 시체를 걷고 이번 프레임의 적 자리 · 플레이어 자리를 적습니다.
1인칭 카메라도 `PrePhysics` 에서 `Look` 으로 시점을 돌립니다. 플레이어 · 적은 기본 그룹(`DuringPhysics`)에서 그것을 **읽기만** 하고 자기 오브젝트에만 씁니다.
몸(`ShooterAvatarComponent`)은 `PostPhysics` 에서 플레이어를 읽고 자기 자리 · 애니메이터를 씁니다. 다른 오브젝트에 쓰는 일 — 적에 피해, 플레이어에 피해,
효과 · 탄도선 꺼내기, 효과음, 손에 든 총 · 몸의 무기 바꾸기, 몸 보이기 — 은 쌓아 두고 `executeOrDeferPostTick` 으로 틱 뒤 게임 스레드에서 합니다.
애니메이션은 틱 뒤에 평가되고, 외형 컴포넌트가 몸의 포즈가 끝난 프레임에 무기 · 투구 자리를 고칩니다.

**총구.** 1인칭이면 손에 든 총(카메라 자식) × 그 무기 외형의 소켓 에셋 `Muzzle`, 아니면 몸 외형의 `MainHand.Muzzle`(지난 프레임 포즈)입니다. 판정은 눈에서
쏘고 탄도선은 총구에서 맞은 자리까지 잇습니다. 손 IK(왼손을 `MainHand.SupportHand` 로) · 맞은 부위 · 래그돌 · 무기 떨어뜨리기는 애니메이션 리그 · 게임플레이
작업이 들어오면 이 소켓 · 물리 에셋 자리를 씁니다.

**핫 리로드 · 상태 저장.** 판의 진행(처치 수)은 디렉터의 `writeState` 로 상태 스냅샷의 컴포넌트 섹션에 실려 넘어갑니다(`ComponentStateStore`).
상태를 쓰기 전에 게임 인스턴스(생성자의 `registerDirector` 한 줄 — `GameInstanceBase`)가 진행을 싣고 디렉터가 세운 적 · 효과 풀과 플레이어의 몸을 걷으며, 다시 만든 디렉터는 감독을 처음부터
돌리고 플레이어가 몸을 다시 세웁니다. 무기 카탈로그 · 외형 데이터는 게임 서비스입니다.

## 파일 · 에셋

- `Shooter3DGame` — 무기 카탈로그(`data/weapons.xml`) · 아이템(`data/items.xml`) · 외형 데이터(`data/appearance/`)를 게임 서비스로 걸고, 사운드 이벤트
  (`audio/shooter3d.audioevents.xml`)를 올리고, 첫 씬을 열고, 상태 저장 전에 세운 것을 걷습니다. 소리는 이벤트 이름으로 냅니다 — 착지 · 명중음은
  2D(`Land` · `HitEnemy` · `HitCover`), 적 처치는 그 자리 3D(`EnemyDown` — 레이어 둘, 벽 뒤면 가림). 클립 · 범위 · 쿨다운 · 상한은 이벤트 파일에 있습니다.
- `ShooterDirectorComponent` · `ShooterPlayerComponent` · `ShooterAvatarComponent` · `ShooterEnemyComponent` · `ShooterEffectComponent` · `ShooterBlockerComponent` — 위 표.
- `Resource/game/shooter3d/maps/arena.scene.xml` · `prefabs/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).
- `textures_raw/crosshair.png` · `hitmarker.png` — Kenney "Starter Kit FPS" 의 스프라이트(CC0), `textures_raw/kaykit_*.png` — KayKit GLB 의 아틀라스.
  `App --import-textures` 가 DDS 로 굽는다. 출처는 `Resource/game/shooter3d/credits.md`.

연사 간격 · 피해 · 퍼짐 · 반동 · 탄창은 전부 `weapons.xml` 에 있습니다. 이동 · 체력 · 적 값은 컴포넌트 PROPERTY(적은 프리팹)입니다.
