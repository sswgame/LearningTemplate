/**
 * @file EditorSettingsRegistry.h
 * @brief 에디터 환경설정 섹션 등록(`SW_EDITOR_SETTINGS`)과 저장 파일 `Saved/Editor/EditorPreferences.json` 의 읽기와 쓰기를 담당합니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Config/EditorUserFile.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

#include "Engine/Reflection/TextAddress.h"

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
     *          값(`_pInstance`)과 기본값(`_pDefault`)은 이 항목의 소유자인 `EditorSettingsRegistrar<T>` 가 든다 — 등록하는 DLL 안에 있고 등록과 함께 생기고 사라진다.
     *          확장 모듈도 섹션을 등록할 수 있다.
     */
    struct EditorSettingsRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "settings";

        /**
         * @brief 섹션을 떼기 직전(`EditorExtensionRegistrar::detach`)에 파일에 씁니다. 값은 그 모듈의 소유자(`EditorSettingsRegistrar`) 멤버라 모듈을 내리면 사라지고,
         *        환경설정 창은 고친 뒤 잠시 기다렸다 저장하므로 그 사이에 내리면 고친 값이 사라진다. 다시 올라오면 새 항목이 파일에서 다시 읽는다.
         */
        static void onDetaching( const EditorSettingsRegistration& registration );

        const utf8* _pLabel;
        const TypeInfo* ( *_pfnGetType )();
        void*       _pInstance;    ///< 지금 값. 소유자(`EditorSettingsRegistrar`)의 멤버다. 페이지 방식(`SW_EDITOR_SETTINGS_PAGE`)은 nullptr 이다
        const void* _pDefault;     ///< 기본값. 위와 같이 소유자의 멤버이고 페이지 방식은 nullptr 이다
        void ( *_pfnOnChanged )(); ///< 값이 바뀐 뒤에 호출한다. 창에서 고쳤거나, 파일을 다시 읽었거나, 섹션을 되돌렸을 때다. nullptr 이면 호출하지 않는다
        /**
         * @brief 리플렉션 구조체가 아닌 페이지를 그리는 함수입니다(`SW_EDITOR_SETTINGS_PAGE`). 이 페이지는 타입이 없어(`_pfnGetType` 이 nullptr) 이 파일에 저장하지 않습니다.
         *        값의 주인이 따로 있기 때문입니다. 예를 들어 디버그 HUD 는 엔진 사용자 설정이 주인입니다. nullptr 이면 리플렉션 그리드를 그립니다.
         */
        void ( *_pfnDrawPage )();
        /**
         * @brief 이 항목이 올라온 뒤 파일 값을 입혔으면 SW_TRUE 입니다(`EditorPreferencesStore` 가 씁니다. 메인 스레드만).
         * @details 항목은 DLL 이 올라올 때 새로 생기고 내려갈 때 사라지므로, 다시 올라온 모듈의 섹션은 저절로 SW_FALSE 로 돌아가 파일에서 다시 읽는다.
         *          '입힌 인스턴스 주소 목록'을 따로 두고 언로드 훅이 지우던 방식을 대신한다.
         */
        mutable uint8 _bFileValueApplied;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그리기 함수 방식(`SW_EDITOR_SETTINGS_PAGE`)의 타입 함수입니다. nullptr 을 반환합니다 — 값이 없는 페이지입니다. */
    struct EditorSettingsPageNone
    {
        static const TypeInfo* getType() { return nullptr; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorSettingsRegistrar
     * @brief 환경설정 섹션 하나의 소유자입니다. 값과 기본값을 멤버로 들고, 그 값을 가리키는 등록 항목을 레지스트리에 올립니다(`SW_EDITOR_SETTINGS` 가 파일 범위에 둡니다).
     * @details 값의 수명은 등록의 수명과 같다 — 등록자가 만들어질 때 값이 생기고(등록 전에), 소멸할 때 등록이 먼저 빠지고 값이 사라진다.
     *          그래서 확장 모듈을 내리면 그 섹션의 값도 같이 사라지고(떼기 직전에 저장한다 — `EditorSettingsRegistration::onDetaching`), 다시 올라오면 항목이 새로 생겨 파일 값을 다시 읽는다.
     *          전에는 함수 정적 변수(`EditorSettingsInstance<T>`)라 등록과 별개의 숨은 전역이었고, 등록되지 않은 섹션을 읽으면 조용히 기본값을 돌려줬다.
     */
    template <typename TSettings>
    class EditorSettingsRegistrar
    {
    public:
        EditorSettingsRegistrar( const utf8* pID, const utf8* pLabel, int32 order, void ( *pfnOnChanged )() )
            : _instance{
        }
            , _default{}
            , _registrar{ EditorSettingsRegistration{ { pID, order }, pLabel, &TSettings::StaticType, &_instance, &_default, pfnOnChanged, nullptr, SW_FALSE } }
        {
        }

        EditorSettingsRegistrar( const EditorSettingsRegistrar& )            = delete;
        EditorSettingsRegistrar& operator=( const EditorSettingsRegistrar& ) = delete;

        const EditorSettingsRegistration& getRegistration() const { return _registrar.getRegistration(); }

    private:
        TSettings                                            _instance;
        const TSettings                                      _default;
        EditorExtensionRegistrar<EditorSettingsRegistration> _registrar; ///< 값 다음에 선언한다 — 소멸은 역순이라 등록이 먼저 빠진다
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
        /**
         * @brief 등록된 섹션마다 파일의 값을 적용합니다. 없는 값은 그대로 둡니다. 적용한 섹션의 `_pfnOnChanged` 를 호출합니다.
         * @return 파일이 없으면 `Missing`(처음 실행). 깨졌으면 `.corrupt` 로 치우고 오류를 남긴 뒤 새 파일을 쓰고 `Recovered`(`EditorUserFile`).
         */
        [[nodiscard]] static EditorUserFileResult loadAll( string_view filePath );
        /** @brief 등록된 섹션을 모두 기본과의 차이만으로 씁니다. 차이가 없는 섹션은 빼고, 모두 없으면 빈 오브젝트를 씁니다. */
        [[nodiscard]] static bool saveAll( string_view filePath );
        /**
         * @brief 아직 파일 값을 입히지 않은 섹션(`loadAll` 뒤에 올라온 확장 모듈의 섹션)에만 파일 값을 입힙니다. 입힌 섹션 수입니다.
         * @details 이미 입힌 섹션은 다시 읽지 않는다(사용자가 바꾼 값을 덮지 않는다). 에디터가 프레임마다, `saveAll` 이 쓰기 전에 부른다.
         */
        static uint32 loadNewSections( string_view filePath );
        /** @brief 파일 값을 아직 입히지 않은 섹션이 있으면 true 입니다. 할당하지 않으므로 프레임마다 물어도 됩니다. */
        [[nodiscard]] static bool hasNewSections();
        /** @brief @p pCurrent 가 @p pDefault 와 다른 프로퍼티만 담은 JSON 오브젝트 문자열을 만듭니다. */
        static string makeDifferenceJSON( const TypeInfo& type, const void* pCurrent, const void* pDefault );
        /** @brief JSON 오브젝트 문자열을 인스턴스에 적용합니다. 없는 키는 그대로 둡니다. 모르는 키는 @p outListUnknownKey 에 담습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool applyJSON( const TypeInfo& type, void* pInstance, string_view jsonText, vector<string>& outListUnknownKey );
        /** @brief @p prop 의 값이 기본값과 다르면 true 입니다. 창의 "Modified only" 필터와 수정 표시에 씁니다. */
        static bool isPropertyModified( const PropertyInfo& prop, ObjectAddress current, ObjectAddress defaultInstance );
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
    /** @brief 그 타입으로 등록된 값 있는 섹션의 항목입니다. 등록되지 않았으면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춥니다(조용히 기본값으로 가지 않는다). */
    SW_EDITOR_API const EditorSettingsRegistration& getSettingsRegistration( const TypeInfo* pType );

    /**
     * @brief 섹션 `TSettings` 의 현재 값을 반환합니다. 그 섹션이 등록되어 있어야 합니다.
     * @details 확장 모듈이 다른 모듈의 섹션을 읽을 때도 이 함수를 쓴다. 값은 등록한 DLL 의 소유자(`EditorSettingsRegistrar`)가 든다 — 그 모듈이 내려가면 사라지므로 참조를 오래 들지 않는다.
     */
    template <typename TSettings>
    const TSettings& getPreferences()
    {
        return *static_cast<const TSettings*>( getSettingsRegistration( TSettings::StaticType() )._pInstance );
    }

    /** @brief 섹션 `TSettings` 의 값을 고칠 수 있게 반환합니다. 고친 뒤 저장은 부른 쪽이 합니다(`EditorPreferencesStore::saveAll`). */
    template <typename TSettings>
    TSettings& editPreferences()
    {
        return *static_cast<TSettings*>( getSettingsRegistration( TSettings::StaticType() )._pInstance );
    }
} // namespace sw::editor

/**
 * @brief 환경설정 섹션 하나를 등록합니다. 예: `SW_EDITOR_SETTINGS( EditorViewportPreferences, "viewport", "Editor/Viewport", 300, nullptr );`
 * @param TSettings 리플렉션 구조체(`REFLECT()`, `PROPERTY()`)입니다. 기본 생성자가 만든 값이 기본값입니다.
 */
#define SW_EDITOR_SETTINGS( TSettings, pID, pLabel, order, pfnOnChanged )                 \
    static ::sw::editor::EditorSettingsRegistrar<TSettings> sw_editorSettings_##TSettings \
    {                                                                                     \
        pID, pLabel, order, pfnOnChanged                                                  \
    }

/**
 * @brief 그리기 함수 하나로 된 환경설정 페이지를 등록합니다. 값의 주인이 에디터 환경설정 파일 밖에 있을 때 씁니다.
 * @code SW_EDITOR_SETTINGS_PAGE( DebugHUD, "debug_hud", "Editor/Debug HUD", 360, &draw ); @endcode
 */
#define SW_EDITOR_SETTINGS_PAGE( name, pID, pLabel, order, pfnDraw )                                       \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSettingsRegistration, Settings_##name, { pID, order }, pLabel, \
                        &::sw::editor::EditorSettingsPageNone::getType, nullptr, nullptr, nullptr, pfnDraw, SW_FALSE )
