/**
 * @file Engine/Input/DebugKeyActions.h
 * @brief 디버그 키 액션 맵(`EngineDefaultAssets::_debugKeyInputMap`)을 만들고 프레임마다 돌립니다. 서비스 범위의 서브시스템이고
 *        (`engine::getSubsystem<DebugKeyActions>()`), 호스트 루프(App)가 프레임마다 돌리고 개발 도구 · 개발 명령이 같은 창구로 읽습니다.
 * @details 이 클래스가 내주는 것은 **입력 사실**(그 액션이 이번 프레임에 발동했나)뿐입니다. 액션이 무엇을 뜻하는지(셰이더를 다시 베이크한다,
 *          모듈을 다시 올린다)는 그 장치를 가진 쪽이 정합니다 — 셰이더 리로드 단축키는 재컴파일기를 가진 개발 도구(`DevToolsHost`)가 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class InputManager;
    class InputMap;

    /**
     * @class DebugKeyActions
     * @brief 셸 디버그 InputMap 하나의 수명과 프레임 갱신입니다. 맵은 처음 `update` 할 때 만듭니다(그 전과 Shipping 에서는 없다).
     * @details 서비스 범위에 서는 까닭: 엔진의 값 멤버로 두면 호스트 · 개발 도구가 엔진을 거쳐 읽는 전달 함수(`Engine::wasDebugActionTriggered` …)가 늘었다.
     *          맵이 입력 매니저(기동 단계)를 가리키므로, 엔진 종료가 기동 단계를 내린 뒤 · 해제하기 전에 `reset` 을 부른다(`Engine::shutdown`).
     */
    class SW_API DebugKeyActions final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "DebugKeyActions";

        DebugKeyActions();
        ~DebugKeyActions() override;

        DebugKeyActions( const DebugKeyActions& )            = delete;
        DebugKeyActions& operator=( const DebugKeyActions& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 맵을 놓습니다(`reset`). 엔진 종료는 그 전에 입력 매니저가 사라지기 전 자리에서 이미 놓았다. */
        void        shutdownSubsystem() override { reset(); }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 맵을 (처음이면 만들고) @p inputManager 에 붙여 한 프레임 돌립니다. 타이틀 레이어는 끕니다 — 셸 단축키는 화면과 상관없이 받는다.
         */
        void update( InputManager& inputManager, float32 deltaTime );
        /** @brief 맵을 놓습니다. 맵이 입력 매니저를 가리키므로 입력 매니저를 없애기 전에 부릅니다(다음 `update` 가 다시 만든다). */
        void reset();

        /**
         * @brief 디버그 키 액션 맵을 @p inputMapPath 의 InputMap 리소스에서 만듭니다.
         * @details 경로가 비었거나 읽지 못하면 오류를 남기고 **빈 맵**을 돌려줍니다. 손으로 적은 바인딩으로 바꿔 끼우지 않습니다 —
         *          그 내용은 리소스와 따로 낡고, 실패를 가립니다.
         */
        static unique_ptr<InputMap> createInputMap( const AssetID& assetID );

        /** @brief 해당 액션이 이번 프레임에 발동했는지 반환합니다. 맵이 아직 없으면 false 입니다. */
        bool wasActionTriggered( const hashed_string& actionName ) const;
        /** @brief 셸 InputMap 입니다(처음 `update` 전에는 nullptr). */
        InputMap* getInputMap() const { return _mapDebugAction.get(); }

        // ------------------------------------------------------------------------------
        // 플레이어별 재배치 — 콘솔을 여는 키 같은 디버그 키를 사용자가 바꾼 것을 사용자 폴더에 둡니다.
        // ------------------------------------------------------------------------------
        /** @brief 사용자 바인딩 파일의 이름입니다. 사용자 설정 파일(`usersettings.json`)과 같은 폴더에 둡니다. */
        static constexpr const utf8* kUserBindingsFileName = "debugkeys.xml";
        /**
         * @brief 사용자 설정 파일 경로 @p userSettingsFilePath 와 같은 폴더의 사용자 바인딩 파일 경로입니다. 설정 파일 경로가 비었으면(테스트, 헤드리스) 빈 경로입니다.
         * @details 자동화가 `-gv_userSettingsFile` 로 사용자 폴더를 피하면 이 파일도 같이 피한다.
         */
        static string makeUserBindingsPath( string_view userSettingsFilePath );
        /**
         * @brief @p map 에서 **기본 바인딩과 다른 슬롯만** @p filePath 에 씁니다. 다른 것이 없으면 파일을 지웁니다. 키보드 키 · 단축키는 수정 키(`modifier`)도 함께 적습니다.
         * @details 디버그 키 맵과 에디터 입력 맵(`EditorInputActions`)이 같은 형식 · 같은 함수로 사용자 재배치를 저장한다.
         *          전체 스냅샷(`InputRemapSerializer::save`)을 두지 않는 까닭: 읽을 때 `bind*` 가 기본 바인딩 위에 더해져 재배치가 아니라 중복이 되고, 그 뒤 리소스의 기본 키를 바꿔도
         *          스냅샷이 옛 기본 키를 붙든다. 사용자가 바꾼 슬롯만 적으면 사용자 설정(`applyKeyBinding`)처럼 "기본값에서의 차이" 가 된다.
         */
        [[nodiscard]] static bool saveUserBindings( const InputMap& map, string_view filePath );
        /**
         * @brief @p filePath 의 재배치를 @p map 에 적용합니다(`InputMap::rebindSlot`). 파일이 없으면 아무것도 하지 않고 true 입니다.
         * @details 없는 액션, 읽지 못하는 슬롯, 키 하나로 바꿀 수 없는 바인딩은 경고하고 건너뜁니다 — 리소스의 맵이 바뀌어도 낡은 파일이 기동을 막지 않는다.
         * @return 파일을 읽었으면(없는 것 포함) true, 읽지 못했으면 false 입니다.
         */
        [[nodiscard]] static bool loadUserBindings( InputMap& map, string_view filePath );
        /**
         * @brief 이 디버그 키 맵의 사용자 바인딩 파일 경로를 정합니다(`makeUserBindingsPath`). 맵을 처음 만들 때 이 파일의 재배치를 읽으므로 첫 `update` 전에 부릅니다.
         *        빈 경로면 읽지도 저장하지도 않습니다(테스트, 헤드리스).
         */
        void          setUserBindingsPath( string_view filePath ) { _userBindingsPath = string( filePath ); }
        const string& getUserBindingsPath() const { return _userBindingsPath; }
        /** @brief 디버그 키 맵의 재배치를 사용자 파일에 저장합니다. 경로가 없으면 false 입니다. */
        [[nodiscard]] bool saveUserBindings() const;

    private:
        unique_ptr<InputMap> _mapDebugAction;
        string               _userBindingsPath; ///< 맵을 만들 때 정한 사용자 바인딩 파일 경로입니다(빈 값 = 저장하지 않는다).
        bool                 _bBound;
    };
} // namespace sw
