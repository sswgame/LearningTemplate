# 엔진 기본 화면과 접근성

## 이것은 무엇이고 왜 있나

옵션 메뉴, 일시정지 메뉴, 알림, 자막은 거의 모든 게임에 필요하고, 패드 탐색과 접근성 요구 사항도 같습니다.
게임마다 다시 만들면 품질이 갈리므로 엔진이 기본 화면을 제공합니다. 언리얼 Lyra가 CommonUI 위에 설정 화면과 Escape 메뉴를 두는 것과 같은 방식입니다.
게임은 프리셋으로 문서를 바꾸고 테마로 겉모습을 바꿉니다.

이 문서는 [UI](../README.md)의 하위 문서입니다. 화면 스택과 행동 입력은 UI 문서를, 문서와 바인딩 형식은 [UI 문서와 데이터 바인딩](../Document/README.md)을 먼저 읽으세요.
코드는 이 폴더(`UI/Screen/`)에 있습니다. 화면 기반 클래스(`UiScreen`), 옵션, 일시정지, 확인 창, 키 바인딩 창, 알림(`UiNotificationService`), 자막(`UiSubtitleService`)입니다.

## 작동 원리

### 화면 목록

엔진 기본 화면과 그 문서는 다음과 같습니다. 기본 메뉴를 엔진에 두기로 한 이유는 [결정 기록 5-7](../../../../docs/09_Decisions.md#5-7-런타임-ui-와-글자)에 있습니다.

| 화면 | 문서 | 언리얼 Lyra 대응 |
|---|---|---|
| `OptionsMenuScreen` | `engine/ui/options.ui.xml` | `UGameSettingRegistry` → 설정 목록 |
| `SettingsConfirmScreen` | `engine/ui/confirm_countdown.ui.xml` | 해상도 확인 창 |
| `KeyRebindScreen` | `engine/ui/key_rebind.ui.xml` | 키 입력 대기 창 |
| `PauseMenuScreen` | `engine/ui/pause.ui.xml` | Escape 메뉴 |
| 알림(`UiNotificationService`) | `engine/ui/notifications.ui.xml` | CommonUI 메시지 |
| 자막(`UiSubtitleService`) | `engine/ui/subtitles.ui.xml` | `FSubtitleManager` |

#### 옵션 메뉴

- 옵션 메뉴 문서에는 탭 줄 `Tabs`, 스크롤 행 목록 `Rows`, 다시 시작 알림 `RestartNotice`, 버튼 `Apply`, `Revert`, `Defaults`, `Close` 가 있습니다.
  행 견본은 조각 `engine/ui/parts/setting_{bool,int,float,enum,string,keybinding}.ui.xml` 이고, 스타일은 `engine/ui/styles/options.uistyle.xml` 입니다.
  게임 프리셋 `_uiOptionsMenu` 로 문서를 바꿀 수 있고, 위 이름과 명령만 지키면 됩니다.
- **행은 설정 스키마가 원본입니다.** 탭은 보일 설정이 있는 카테고리이고, 행은 `collectSettings` 순서입니다.
  행마다 견본을 만들어 안쪽 이름을 `<설정 id>.<이름>` 으로 바꾸고, 값 위젯 `Value` 에 `{setting:id}` 바인딩을 코드로 연결합니다.
  형식별로 연결하는 필드는 Bool이 `_bChecked`, Int와 Float가 `_value`, Enum이 `_selectedIndex`, String이 `_text` 이고, 값 글 `ValueText` 는 단방향입니다.
  키 바인딩 행은 버튼(`Rebind`)과 글리프 `Glyph`(`getBindingGlyph`)이고, 입력 방식이 바뀌면 글리프를 다시 씁니다. 탭을 바꾸면 행과 바인딩을 다시 만듭니다.
- **적용.** `applyPending` 으로 확인 대기가 시작되면 `SettingsConfirmScreen` 모달이 열립니다. 남은 초는 뷰모델 바인딩 `{bind:_secondsLeft, converter=Seconds}` 로 보입니다.
  시간은 설정 매니저가 세고, 시간이 다 되면 매니저가 스스로 되돌립니다. 창은 확인 대기가 끝난 것을 보고 닫힙니다. [되돌리기]와 뒤로 가기는 남은 시간을 다 쓴 것으로 보고 지금 되돌립니다.
- **닫기.** 닫기나 뒤로 가기에서 보류 값이 있으면 `engine/ui/confirm_unsaved.ui.xml`("적용, 버리기, 취소")을 엽니다. 이 창의 버튼은 메뉴를 번호로 찾으므로, 메뉴가 먼저 사라져도 매달린 포인터가 없습니다.
- **키 바인딩 창**은 연 다음 프레임부터 `InputManager::findFirstPressedSlot` 의 첫 슬롯(키보드, 마우스, 패드 순)을 받습니다.
  받는 동안 `wantsUiActions` 가 false라서 누른 키가 메뉴를 움직이지 않습니다. Esc는 떼면 취소이고, 1초 누르면 Esc 자체를 바인딩합니다.
  받은 슬롯은 UI가 먹습니다(`UiSystem::consumeSlot`). 뗄 때까지 게임이 보지 못합니다.
  다른 행동과 겹치면(`findBindingConflict`) "이미 쓰입니다" 창이 바꾸기와 취소를 묻고, 바꾸기는 `setPendingBinding( Swap )` 입니다. 스키마 밖 행동과 겹치면 취소만 할 수 있습니다.
- 개발 스위치 `-gv_uiOptionsMenu=1` 은 옵션 메뉴를 바로 띄웁니다. 스크린샷과 네 백엔드 확인에 씁니다.

#### 일시정지 메뉴

게임 프리셋 `_bUiPauseMenu` 를 켜면, 열린 화면이 없을 때 UI 맵의 `UIGlobal` 레이어 행동 `UI.Pause`(Esc, 패드 Start)가 `PauseMenuScreen` 을 엽니다.
문서는 프리셋 `_uiPauseMenu` 이고 기본은 `engine/ui/pause.ui.xml` 입니다. 그 입력은 UI가 먹으므로, 같은 키를 쓰는 게임 행동(Shooter3D의 마우스 잠금 토글)은 보지 못합니다.
메뉴가 커서를 원하므로 그동안 마우스 잠금이 쉬고, 닫으면 돌아옵니다. `OpenOptions` 명령은 옵션 메뉴를 위에 엽니다.

#### 알림과 행동 글리프

- **알림**은 `UiSystem::getNotifications().post( UiNotificationDesc{ 글, 길이, 우선순위, 종류 } )` 로 보냅니다. 한 번에 셋까지 보이고 오래된 것이 위에 있으며, 나머지는 대기열에서 기다립니다.
  대기열은 우선순위가 큰 것, 같으면 먼저 온 것부터 꺼냅니다. 같은 글이 보이거나 기다리는 중이면 새로 쌓지 않고 개수("x2")를 세며 시간을 다시 잽니다. 시간은 보이기 시작한 뒤부터 잽니다.
  화면은 오버레이 층이라 입력과 포커스를 받지 않고, 게임과 메뉴를 막지 않습니다. 보일 것이 없으면 화면을 닫습니다.
  항목은 조각 `engine/ui/parts/notification.ui.xml`(`Message`, `Count`)이고, 루트 클래스 `notification <info|achievement|warning|hint>` 를 `styles/notifications.uistyle.xml` 이 꾸밉니다.
  사용처는 옵션 메뉴의 "다시 시작하면 적용", 자동 저장 완료, GameFramework의 튜토리얼 힌트입니다.
- **행동 글리프.** 글 위젯의 글에 `[action=이름]` 이 있으면, 번역을 해석한 뒤 측정과 그리기 문맥의 `UiActionGlyphSource` 가 지금 장치의 글리프로 바꿉니다.
  게임 입력 맵을 먼저 보고 UI 맵을 보며, 장치 종류는 `InputManager::getActiveGlyphStyle` 입니다. 번역가가 태그 위치를 옮길 수 있습니다.
  장치 종류가 바뀌면 `UiSystem` 이 위젯마다 `onInputGlyphsChanged` 를 부르고, 태그가 있는 글만 다시 해석하고 계산합니다. 견본은 `engine/ui/parts/inputhint.ui.xml` 의 `[action=UI.Accept] Select` 입니다.

#### 자막

`UiSystem::getSubtitles().post( 화자, 글, 길이 )` 로 보냅니다. 글은 이미 현지화한 글을 받습니다. 대화 러너와 음성 이벤트가 해석해서 넘깁니다.

- **보이는 시간.** 길이가 0이면 읽기 시간 max( 2초, 코드 포인트 수 × 0.06초 )를 씁니다. 줄은 **둘까지** 동시에 보이고 오래된 것이 위입니다. 넘친 줄은 대기열에서 기다렸다가 빈 줄이 생기면 그때부터 시간을 셉니다.
- **화면.** 오버레이 층 문서 `engine/ui/subtitles.ui.xml` 이고, 화면 아래 가운데에 놓입니다. 줄마다 이름 `Line<n>`(바탕), `Speaker<n>`, `Text<n>` 위젯이 있습니다.
  줄이 있을 때만 열고 없으면 닫습니다. 화자 이름 색은 시트 `engine/ui/styles/subtitles.uistyle.xml` 의 변수 `speaker` 입니다.
- **사용자 설정**은 매 프레임 읽습니다. `gv_subtitles` 를 끄면 화면을 닫지만 줄은 계속 받고 시간도 흐릅니다. 다시 켜면 지금 줄부터 보입니다.
  `gv_subtitleSize` 0, 1, 2는 문서의 글 크기에 0.85, 1, 1.3을 곱하고, 글자 배율과 최소 크기는 그 위에 적용됩니다. `gv_subtitleBackgroundOpacity` 는 줄 바탕의 알파입니다.
- **보내는 쪽**은 GameFramework `DialogueRunnerComponent::_bPostSubtitles` 입니다. 기본은 꺼짐이고, 대화 UI가 글을 따로 보여 주면 켜지 않습니다.

#### HUD와 로딩 화면

HUD와 로딩 화면은 GameFramework에 있습니다. 엔진 UI의 문서와 뷰모델 위에 만들어졌습니다.

- **HUD**는 플레이어 오브젝트의 `HudControllerComponent` 가 `_documentPath` 문서를 Hud 층에 열고 `HudViewModel` 을 연결합니다. 게임은 뷰모델 세터만 부릅니다(Shooter3D의 `ShooterPlayerComponent::updateHud`).
- **로딩 화면**은 `LoadingScreenController` 가 씬을 비동기로 바꾸는 동안 팩의 `gamesettings.xml` 에 적힌 `_loadingScreen` 문서(기본 `engine/ui/loading.ui.xml`)를 Loading 층에 엽니다.
  `UiSystem::isLoadingScreenShown` 이 참인 동안 게임 입력이 막히고, 자동화의 "씬 플레이 중" 판정도 로딩 화면이 닫힌 뒤입니다.

### 접근성

접근성 기능은 여러 절에 나뉘어 있습니다. 한곳에서 보면 다음과 같습니다. 기준은 Xbox 접근성 지침이고, 결정의 근거는 [결정 기록 5-7](../../../../docs/09_Decisions.md#5-7-런타임-ui-와-글자)에 있습니다.

| 기능 | 사용자 설정 | 동작 위치 |
|---|---|---|
| 글자 크기 | `gv_uiTextScale` | 레이아웃의 글 측정 |
| 최소 글자 크기 | (항상) | `UiScaleUtil::computeScaledFontSize` |
| 고대비 테마 | `gv_uiTheme` | 스타일 테마 |
| 색각 보정 | `gv_colorVisionMode` | 렌더러 Canvas 패스 |
| 움직임 줄이기 | `gv_uiReduceMotion` | 애니메이션 단계 |
| 자막 | `gv_subtitles` 등 | `UiSubtitleService` |

- **최소 글자 크기.** 배율이 줄여도 글은 12 UI 단위 밑으로 내려가지 않습니다. 그보다 작게 적은 글은 적은 크기가 하한입니다.
  글자 배율 2에서 넘치지 않게 하는 것은 문서가 할 일입니다. 긴 이름은 줄 바꿈 없이 `_overflow="Ellipsis"` 로, 늘어난 줄은 `ScrollPanel` 로 처리합니다(`UiAccessibilityTest.OptionsMenuFitsAtDoubleTextScale`).
- **고대비 테마.** 사용자 설정 `accessibility.uiTheme`(`gv_uiTheme`)의 값은 `default` 와 `highcontrast` 입니다. 테마 목록을 적용할 때 그 이름이 목록에 있으면 그 테마, 없으면 목록의 기본 테마를 씁니다.
  실행 중에 바뀌면 다음 `update` 가 따라갑니다. 게임 테마 목록이 이 설정을 받으려면 같은 이름의 테마를 둡니다.
  `highcontrast` 는 기본 시트 위에 `engine/ui/styles/highcontrast.uistyle.xml` 을 더한 것입니다. 기본 시트와 같은 선택자로 색과 테두리만 덮으므로, 특정도가 같고 뒤 시트가 이깁니다.
  불투명한 검정 바탕, 흰 글(21:1), 노랑 강조, 상호작용 위젯의 두께 2 테두리이고, `UiStyleTest.HighContrastThemeLoads` 가 대비 7:1 이상을 확인합니다.
- **색각 안전 팔레트.** 두 시트 모두 경고 `warning`(주황)과 성공 `success`(파랑) 변수를 씁니다. 빨강과 초록을 짝으로 쓰지 않고, 색만이 아니라 형태로도 구분합니다.
  `BorderPanel.warning` 은 두꺼운 테두리와 각진 모서리, `BorderPanel.success` 는 얇은 테두리와 둥근 모서리입니다.
- **색각 보정**(`gv_colorVisionMode`, accessibility.colorVision). UI는 톤맵 뒤에 그려 톤맵 쪽 보정이 닿지 않으므로, 캔버스 셰이더가 직접 적용합니다.
  렌더러의 Canvas 패스와 `colorvision.hlsli` 가 Machado 2009 흉내 행렬과 오차 재분배를 씁니다. 스타일 시트와는 상관없습니다.
- **패드 도달성.** 모든 화면의 모든 포커스 위젯에 패드로 갈 수 있는지를 테스트가 확인합니다([UI의 테스트와 결정성](../README.md#테스트와-결정성) 참고).

화면 낭독기(스크린 리더)는 아직 없습니다([백로그](../../../../docs/06_Backlog.md) 1-12).

## 확장하는 법

### 게임의 테마와 기본 화면 바꾸기

게임 프리셋(`Config/Game/<게임>.json`)에서 `_uiThemes`, `_uiScaleSettings`, `_uiOptionsMenu`, `_uiPauseMenu`, `_bUiPauseMenu` 를 지정합니다.
고대비 설정을 받으려면 게임 테마 목록에도 `default` 와 `highcontrast` 테마를 둡니다.

## 함정과 주의

**옵션 메뉴 문서를 바꿀 때 위젯 이름과 명령 이름을 지킵니다.** `OptionsMenuScreen` 이 `Tabs`, `Rows`, `Apply` 같은 이름으로 위젯을 찾고 명령을 받습니다. 이름이 다르면 행이 만들어지지 않습니다.

**새 메뉴 문서는 패드로 모든 버튼에 닿게 만듭니다.** 패드 도달성 테스트가 `engine/ui` 의 모든 문서를 자동으로 검사하므로, 닿지 않는 버튼이 있으면 테스트가 실패합니다.

**빨강과 초록을 짝으로 쓰지 않습니다.** 경고와 성공은 `warning`, `success` 변수와 테두리 형태로 구분합니다. 색만으로 구분하면 색각 이상 사용자에게 같은 화면이 됩니다.

## 더 볼 곳

- [UI](../README.md): 화면 스택, 행동 입력, 테스트
- [UserSettings](../../UserSettings/README.md): 설정 스키마, 보류 값, 확인 대기
- [결정 기록 5-7](../../../../docs/09_Decisions.md#5-7-런타임-ui-와-글자): 기본 메뉴와 접근성 기준을 고른 이유

| 파일 | 여는 때 |
|---|---|
| `OptionsMenuScreen.h` | 옵션 메뉴가 찾는 위젯 이름과 명령 |
| `UiNotificationService.h` | 알림 API |
| `../Screen/UiSubtitleService.h` | 자막 API |
| `Resource/engine/ui/` | 엔진 기본 문서와 스타일 |
