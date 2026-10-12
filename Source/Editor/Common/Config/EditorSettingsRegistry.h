/**
 * @file EditorSettingsRegistry.h
 * @brief 에디터 환경설정 섹션 등록(`SW_EDITOR_SETTINGS`)과 저장 파일 `Saved/Editor/EditorPreferences.json` 의 읽기와 쓰기를 담당합니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorSettingsRegistration
     * @brief 환경설정 섹션 하나(리플렉션 구조체 하나)의 등록 항목입니다. 그 구조체의 .cpp 가 `SW_EDITOR_SETTINGS` 로 둡니다.
     * @details id 는 저장 파일의 키(`"viewport"`)이고, 라벨은 창 왼쪽 목록에 보이는 이름(`"Editor/Viewport"`)이다. '/' 앞이 그룹이다.
     *          인스턴스와 기본값 인스턴스는 그 섹션 파일의 함수 정적 변수다. 확장 모듈도 섹션을 등록할 수 있다.
     */
    struct EditorSettingsRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "settings";

        const utf8* _pLabel;
        const TypeInfo* ( *_pfnGetType )();
        void* ( *_pfnGetInstance )();
        const void* ( *_pfnGetDefault )();
        void ( *_pfnOnChanged )(); ///< 값이 바뀐 뒤에 호출한다. 창에서 고쳤거나, 파일을 다시 읽었거나, 섹션을 되돌렸을 때다. nullptr 이면 호출하지 않는다
        /**
         * @brief 리플렉션 구조체가 아닌 페이지를 그리는 함수입니다(`SW_EDITOR_SETTINGS_PAGE`). 이 페이지는 타입이 없어(`_pfnGetType` 이 nullptr) 이 파일에 저장하지 않습니다.
         *        값의 주인이 따로 있기 때문입니다. 예를 들어 디버그 HUD 는 엔진 사용자 설정이 주인입니다. nullptr 이면 리플렉션 그리드를 그립니다.
         */
        void ( *_pfnDrawPage )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그리기 함수 방식(`SW_EDITOR_SETTINGS_PAGE`)의 타입과 인스턴스 함수입니다. 모두 nullptr 을 반환합니다. */
    struct EditorSettingsPageNone
    {
        static const TypeInfo* getType() { return nullptr; }
        static void*           getInstance() { return nullptr; }
        static const void*     getDefault() { return nullptr; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 항목이 가리키는 인스턴스 함수입니다. 함수 정적 변수이므로 등록한 DLL 안에 하나만 있습니다. */
    template <typename TSettings>
    struct EditorSettingsInstance
    {
        static void* getInstance()
        {
            static TSettings s_instance{};
            return &s_instance;
        }
        static const void* getDefault()
        {
            static const TSettings s_default{};
            return &s_default;
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPreferencesStore
     * @brief `Saved/Editor/EditorPreferences.json` 을 읽고 씁니다. 섹션 id 마다 JSON 오브젝트 하나이고, 기본값과 다른 프로퍼티만 담습니다.
     * @details 값은 프로퍼티의 텍스트 형식(`SerializerUtil::formatPropertyText`)이다. 모르는 섹션과 키는 경고하고 건너뛴다. 사용자 파일이므로 기동을 막지 않는다.
     *          사람이 커밋하는 설정 파일처럼 엄격하게 읽지 않는 이유는, 확장 모듈을 끄면 그 섹션이 사라지기 때문이다.
     */
    struct SW_EDITOR_API EditorPreferencesStore
    {
        /** @brief 등록된 섹션마다 파일의 값을 적용합니다. 없는 값은 그대로 둡니다. 적용한 섹션의 `_pfnOnChanged` 를 호출합니다. 파일이 없으면 false 이며, 처음 실행이면 정상입니다. */
        [[nodiscard]] static bool loadAll( string_view filePath );
        /** @brief 등록된 섹션을 모두 기본과의 차이만으로 씁니다. 차이가 없는 섹션은 빼고, 모두 없으면 빈 오브젝트를 씁니다. */
        [[nodiscard]] static bool saveAll( string_view filePath );
        /**
         * @brief 인스턴스 함수가 [@p pBegin, @p pEnd) 안에 있는 섹션이 있으면 true 입니다(확장 모듈 이미지가 가진 섹션).
         * @details 그 섹션의 값은 그 이미지의 함수 정적이라 이미지를 내리면 사라진다. 언로드 훅이 이것으로 묻고 내리기 전에 `saveAll` 한다.
         */
        [[nodiscard]] static bool hasSectionWithin( const void* pBegin, const void* pEnd );
        /**
         * @brief 아직 파일 값을 입히지 않은 섹션(`loadAll` 뒤에 올라온 확장 모듈의 섹션)에만 파일 값을 입힙니다. 입힌 섹션 수입니다.
         * @details 이미 입힌 섹션은 다시 읽지 않는다(사용자가 바꾼 값을 덮지 않는다). 에디터가 프레임마다, `saveAll` 이 쓰기 전에 부른다.
         */
        static uint32 loadNewSections( string_view filePath );
        /** @brief 파일 값을 아직 입히지 않은 섹션이 있으면 true 입니다. 할당하지 않으므로 프레임마다 물어도 됩니다. */
        [[nodiscard]] static bool hasNewSections();
        /** @brief 인스턴스가 [@p pBegin, @p pEnd) 안에 있는 섹션을 '입힘' 기록에서 뺍니다. 이미지가 같은 주소로 다시 올라와도 다시 읽게 언로드 훅이 부릅니다. */
        static void forgetSectionsWithin( const void* pBegin, const void* pEnd );
        /** @brief @p pCurrent 가 @p pDefault 와 다른 프로퍼티만 담은 JSON 오브젝트 문자열을 만듭니다. */
        static string makeDifferenceJSON( const TypeInfo& type, const void* pCurrent, const void* pDefault );
        /** @brief JSON 오브젝트 문자열을 인스턴스에 적용합니다. 없는 키는 그대로 둡니다. 모르는 키는 @p outListUnknownKey 에 담습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool applyJSON( const TypeInfo& type, void* pInstance, string_view jsonText, vector<string>& outListUnknownKey );
        /** @brief @p prop 의 값이 기본값과 다르면 true 입니다. 창의 "Modified only" 필터와 수정 표시에 씁니다. */
        static bool isPropertyModified( const PropertyInfo& prop, const void* pCurrent, const void* pDefault );
        /** @brief 섹션 하나를 기본값으로 되돌리고 `_pfnOnChanged` 를 부릅니다. */
        static void resetSection( const EditorSettingsRegistration& registration );
        /** @brief 파일에 저장된 프로퍼티 수를 반환합니다. 모든 섹션의 키를 합한 값입니다. 파일이 없거나 형식이 틀리면 0 입니다. */
        static uint32 countSavedKeys( string_view filePath );
        /** @brief 저장 파일 경로를 반환합니다. 에디터 상태 폴더의 `EditorPreferences.json` 입니다. */
        static string getDefaultFilePath();
        /** @brief 저장 파일 이름입니다. */
        static constexpr const utf8* kFileName = "EditorPreferences.json";
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그 타입으로 등록된 섹션을 찾습니다. 없으면 nullptr 입니다. */
    SW_EDITOR_API const EditorSettingsRegistration* findSettingsRegistration( const TypeInfo* pType );

    /**
     * @brief 섹션 `TSettings` 의 현재 값을 반환합니다. 등록되지 않았으면 기본값입니다.
     * @details 확장 모듈이 다른 모듈의 섹션을 읽을 때도 이 함수를 쓴다. 인스턴스는 등록한 DLL 의 것이다.
     */
    template <typename TSettings>
    const TSettings& getPreferences()
    {
        const EditorSettingsRegistration* pRegistration = findSettingsRegistration( TSettings::StaticType() );
        if ( pRegistration == nullptr )
            return *static_cast<const TSettings*>( EditorSettingsInstance<TSettings>::getDefault() );
        return *static_cast<const TSettings*>( pRegistration->_pfnGetInstance() );
    }
} // namespace sw::editor

/**
 * @brief 환경설정 섹션 하나를 등록합니다. 예: `SW_EDITOR_SETTINGS( EditorViewportPreferences, "viewport", "Editor/Viewport", 300, nullptr );`
 * @param TSettings 리플렉션 구조체(`REFLECT()`, `PROPERTY()`)입니다. 기본 생성자가 만든 값이 기본값입니다.
 */
#define SW_EDITOR_SETTINGS( TSettings, pID, pLabel, order, pfnOnChanged )                                                               \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSettingsRegistration, Settings_##TSettings, { pID, order }, pLabel, &TSettings::StaticType, \
                        &::sw::editor::EditorSettingsInstance<TSettings>::getInstance, &::sw::editor::EditorSettingsInstance<TSettings>::getDefault, pfnOnChanged, nullptr )

/**
 * @brief 그리기 함수 하나로 된 환경설정 페이지를 등록합니다. 값의 주인이 에디터 환경설정 파일 밖에 있을 때 씁니다.
 * @code SW_EDITOR_SETTINGS_PAGE( DebugHUD, "debug_hud", "Editor/Debug HUD", 360, &draw ); @endcode
 */
#define SW_EDITOR_SETTINGS_PAGE( name, pID, pLabel, order, pfnDraw )                                                        \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSettingsRegistration, Settings_##name, { pID, order }, pLabel,                  \
                        &::sw::editor::EditorSettingsPageNone::getType, &::sw::editor::EditorSettingsPageNone::getInstance, \
                        &::sw::editor::EditorSettingsPageNone::getDefault, nullptr, pfnDraw )
