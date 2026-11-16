/**
 * @file InputMapBundle.h
 * @brief 게임의 입력 맵 묶음입니다 — `gamesettings.xml` 의 `<inputMap path>` 줄들과 키트가 선언한 맵을 읽는 차례로 늘어놓습니다.
 * @details 차례는 기본 맵(첫 줄) → 키트의 맵(id 순) → 나머지 줄(공통 묶음, 맨 끝이 게임 차이)입니다. 게임 차이가 맨 뒤라 키트 · 공통 액션을 덮거나 지울 수 있습니다.
 *          키트는 `SW_INPUT_MAP_EXTENSION` 한 줄로 선언합니다. 등록자는 모듈과 함께 올라가고 내려가므로 게임 매니페스트(`_listDependency`)가 닿는 키트만 묶음에 듭니다.
 *          Shipping 은 매니페스트를 복사하지 않지만(정적 링크) 의존한 키트만 링크되므로 같은 결과입니다.
 *          묶음의 규칙과 상용 엔진 비교는 `docs/InputMapBundle.md`.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Module/ModuleEntryRegistrar.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class InputMap;

    /** @brief 키트 · 기능이 게임 입력 맵 묶음에 얹는 맵 파일 하나의 등록 줄입니다. 문자열은 등록한 이미지의 정적 저장소에 있습니다. */
    struct InputMapExtension
    {
        const utf8* _pID;        ///< 등록 id(`themepark.input`). 묶음 안의 차례를 정한다
        const utf8* _pAssetPath; ///< `*.input.xml` 리소스 경로. 더하는 파일 규칙을 따른다(`InputMap::loadBundle`)
    };

    /**
     * @class InputMapExtensionRegistry
     * @brief 올라온 모듈이 선언한 입력 맵의 목록입니다(엔진 서비스 범위). 모듈을 올릴 때 붙고 내리기 전에 빠집니다(`ModuleObjectRegistrar`).
     */
    class SW_API InputMapExtensionRegistry final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "InputMapExtensionRegistry";

        InputMapExtensionRegistry();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        [[nodiscard]] bool registerEntry( const InputMapExtension* pExtension );
        void               unregisterEntry( const InputMapExtension* pExtension );
        /** @brief 등록된 맵 파일을 id 순으로 @p inoutListAsset 뒤에 더합니다. 읽는 차례가 모듈을 올린 순서에 달리지 않게 한다. */
        void appendAssets( vector<AssetID>& inoutListAsset ) const;

    private:
        mutable mutex                                           _mutex;                  ///< 등록 목록을 지킨다
        RegistrationList<const InputMapExtension> _registration SW_GUARDED_BY( _mutex ); ///< (0, id) 순서, 이름 없는 등록은 거절한다

        SW_SERVICE_SLOT( InputMapExtensionRegistry, Required );
    };

    /** @brief `SW_INPUT_MAP_EXTENSION` 이 두는 정적 등록자입니다. */
    using InputMapExtensionRegistrar = ModuleEntryRegistrar<InputMapExtensionRegistry, const InputMapExtension*>;

    /**
     * @brief `gamesettings.xml` 의 묶음 줄(`<inputMap path="common/input/base.input.xml"/>`) 하나입니다.
     * @details 예전에는 요소 글(`<inputMap>경로</inputMap>`)을 손으로 읽어 경로 칸을 이름 손 표가 따로 적었다 — 리플렉션 칸이라 참조 검사 · 옮기기가 칸을 안다.
     */
    REFLECT()
    struct SW_API InputMapBundleLine
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, Required )
        string _path{};
    };

    /**
     * @struct InputMapBundle
     * @brief 게임 설정 파일에서 맵 묶음을 만들어 읽습니다. 게임 시작 · 쿠킹 · 테스트 · 입력 맵 패널이 같은 길을 씁니다.
     */
    struct SW_API InputMapBundle
    {
        /** @brief `gamesettings.xml` 안 줄의 요소 이름입니다. 여러 줄이고 적은 차례대로 읽습니다. */
        static constexpr const utf8* kSettingsElement = "inputMap";

        /** @brief @p settingsAssetID 가 게임 설정 파일이면 true 입니다(패널의 경로 칸이 맵 파일과 묶음을 가른다). */
        [[nodiscard]] static bool isSettingsFile( string_view settingsAssetID );
        /**
         * @brief 게임 설정 파일의 `<inputMap>` 줄과 키트 맵으로 묶음 차례를 만듭니다.
         * @param outListAsset 읽는 차례입니다. 줄이 없고 키트 맵도 없으면 비고(입력 맵을 쓰지 않는 게임) true 입니다.
         * @return 파일을 읽지 못하거나, 줄의 글이 비었거나, 키트 맵을 받을 기본 맵 줄이 없으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectAssets( const AssetID& settingsAssetID, vector<AssetID>& outListAsset );
        /**
         * @brief 게임 설정 파일의 묶음을 @p map 에 읽습니다. 묶음이 비었으면 맵을 건드리지 않고 true 입니다.
         * @return 묶음을 읽지 못했거나 겹침이 있으면 false 입니다(`InputMap::loadBundle`).
         */
        [[nodiscard]] static bool loadFromSettings( InputMap& map, const AssetID& settingsAssetID );
        /** @brief 빈 맵에 묶음을 읽어 검사만 합니다. 패널이 편집 중인 맵을 건드리기 전에 부릅니다. */
        [[nodiscard]] static bool checkSettings( const AssetID& settingsAssetID );
        /** @brief `checkSettings` 를 팩 루트 @p packRoot 의 게임 설정 파일로 부릅니다 — 쿠킹이 씬을 굽기 전에 부른다. */
        [[nodiscard]] static bool checkPack( string_view packRoot );
    };
} // namespace sw

/**
 * @brief 키트 · 기능의 입력 맵을 게임 묶음에 얹습니다. 그 기능의 .cpp 에 한 줄 둡니다(`SW_EDITOR_INPUT_MAP` 과 같은 모양).
 * @code SW_INPUT_MAP_EXTENSION( Fighting, "fighting.input", "common/input/kits/fighting.input.xml" ); @endcode
 */
#define SW_INPUT_MAP_EXTENSION( varName, pID, pAssetPath )                                     \
    static const ::sw::InputMapExtension    sw_inputMapExtension_##varName{ pID, pAssetPath }; \
    static ::sw::InputMapExtensionRegistrar sw_inputMapExtensionRegistrar_##varName            \
    {                                                                                          \
        &sw_inputMapExtension_##varName                                                        \
    }
