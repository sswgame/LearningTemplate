/**
 * @file CityBuildInputComponent.h
 * @brief 도시 건설 키트의 클라이언트 쪽 입력 — 이 화면의 사람이 도구(도로 · 건물)를 고르고 포인터 아래 칸에 짓고 허물며 시간 · 현황 · 자동 계획 입력을 디렉터(`CityBuilderGameModeComponent`)의 명령으로 넘기고, 도시 알림을 소리로 냅니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CityBuilder`, 전용 서버는 짓지 않는다). 디렉터의 명령과 읽기 API 만 쓰고 도시(규칙)를 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 언리얼의 PlayerController 자리다.
 *          명령은 틱 뒤 게임 스레드에서 낸다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 · 카메라 뒤에 돌아, 이번 프레임의 시점으로 칸을 고르고 지은 것은 다음 걸음부터 돈다.
 *          고른 도구는 이 화면 사람의 것이라 판 상태(디렉터)가 아니라 이 컴포넌트의 구간이다 — 세이브 · 핫 리로드는 넘기고 전용 서버의 판 상태에는 없다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

namespace sw
{
    class CityBuilderGameModeComponent;
    class GameInputView;
    class GameObjectManager;
    class InputManager;

    /**
     * @class CityBuildInputComponent
     * @brief 이 화면의 도구 고르기와 짓기 · 허물기 입력입니다. 도구 번호는 0 이 도로, 그 뒤는 카탈로그 건물 순서입니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Build Input",
             Tooltip = "Client side of a city: picks a tool and sends building, demolishing and speed input to the city game mode on the same object" )
    class SW_KIT_CLIENT_API CityBuildInputComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        /** @brief 도구 · 커서는 틱 뒤 게임 스레드에서만 바뀝니다 — 커서 뷰가 병렬 틱에서 읽어도 됩니다(자기 틱이 쓰는 것은 따로 둔 `_bInputLive` 뿐이다). */
        static constexpr bool      kTickReadSafe = true;
        static constexpr FormatDef kStateFormat{ "CTBI" };

        CityBuildInputComponent();
        virtual ~CityBuildInputComponent() override;

        /** @brief 틱 그룹은 뷰의 `PostUpdate` 입니다. 입력 액션이 맵에 모두 있는지 봅니다(빠지면 오류를 남기고 입력을 읽지 않는다). */
        void onBeginPlay() override;

        /** @brief 고른 도구를 씁니다(`ComponentStateCodec::captureObject`). */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief `writeState` 가 쓴 바이트로 고른 도구를 되돌립니다. 읽지 못하면 알리고 그대로 둡니다. */
        void restoreState( vector<uint8>&& bytes );

        // ---- 커서 뷰 · 감시 값이 읽는 것 ----
        /** @brief 커서가 땅 위에 있으면 true 를 반환하고, @p outTile 과 @p outSize 에 타일과 고른 것의 크기(타일 수)를 채웁니다. */
        [[nodiscard]] bool findCursor( int2& outTile, int32& outSize ) const;
        int32              getSelectedTool() const { return _selectedTool; }

    private:
        /** @brief 틱 뒤 게임 스레드: 알림을 소리로 내고, 시간이 흐르는 동안 입력을 명령으로 바꿉니다. */
        void updateInput( GameObjectManager& manager, CityBuilderGameModeComponent& director );
        void submitInput( const GameModeInput& inputMap, CityBuilderGameModeComponent& director );
        /** @brief 디렉터의 이번 걸음 알림을 소리로 냅니다. */
        static void playStepEvents( const CityBuilderGameModeComponent& director );
        void        updateTool( const CityBuilderGameModeComponent& director, const GameInputView& inputMap );
        /** @brief 포인터 아래 칸을 고릅니다. 카메라 리그가 없거나 땅 밖이면 커서가 없다. */
        void updateCursor( const GameObjectManager& manager, const CityBuilderGameModeComponent& director, const InputManager& input );
        /** @brief 커서 칸에 고른 것을 짓습니다. */
        void placeSelected( CityBuilderGameModeComponent& director );
        /** @brief 고른 건물입니다. 도로 도구면 nullptr 입니다. */
        const CityBuildingDef* findSelectedBuilding( const CityBuilderGameModeComponent& director ) const;

    private:
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent the pointer is picked through" )
        GameObjectHandle _cameraRig;
        // ---- 입력 액션 — 이름은 입력 맵(팩의 묶음)의 액션이다. 모두 필수다 ----
        PROPERTY( Category = "Input Actions", DisplayName = "Place", Tooltip = "Builds the selected tool at the cursor; the road tool keeps laying while held" )
        hashed_string _placeAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Demolish", Tooltip = "Demolishes the building or the road at the cursor" )
        hashed_string _demolishAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Next Tool" )
        hashed_string _nextToolAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Previous Tool" )
        hashed_string _prevToolAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Road Tool" )
        hashed_string _roadToolAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Toggle Auto Plan" )
        hashed_string _autoPlanAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Pause" )
        hashed_string _pauseAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Faster" )
        hashed_string _fasterAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Slower" )
        hashed_string _slowerAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Status", Tooltip = "Logs the city status" )
        hashed_string _statusAction;

        int2                   _cursorTile;
        int32                  _cursorSize; ///< 고른 것의 크기(타일 수)
        int32                  _selectedTool;
        uint8                  _bInputLive; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다). 틱이 쓰므로 뷰가 읽는 비트와 나눈다
        uint8                  _bActionsValid : 1;
        uint8                  _bCursorValid  : 1;
        [[maybe_unused]] uint8 _reserved      : 6;
    };
} // namespace sw
