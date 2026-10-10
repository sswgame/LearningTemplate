/**
 * @file EditorSettingsRegistry.h
 * @brief 에디터 환경설정 섹션 등록(`SW_EDITOR_SETTINGS`)과 저장 파일 `Saved/Editor/EditorPreferences.json` 읽기 · 쓰기입니다(ImGui 없음).
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
     * @brief 환경설정 섹션 하나(리플렉션 구조체 하나)의 등록 줄입니다. 그 구조체의 .cpp 가 `SW_EDITOR_SETTINGS` 로 둡니다.
     * @details id 는 저장 파일의 키(`"viewport"`)이고, 라벨은 창 왼쪽 목록에 보이는 이름(`"Editor/Viewport"`)입니다. '/' 앞이 묶음입니다.
     *          인스턴스와 기본값 인스턴스는 그 섹션 파일의 함수 정적입니다. 확장 모듈도 섹션을 등록할 수 있습니다.
     */
    struct EditorSettingsRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "settings";

        const utf8* _pLabel;
        const TypeInfo* ( *_pfnGetType )();
        void* ( *_pfnGetInstance )();
        const void* ( *_pfnGetDefault )();
        void ( *_pfnOnChanged )(); ///< 값이 바뀐 뒤(창에서 고침 · 파일을 다시 읽음 · 섹션 되돌리기). nullptr 이면 부르지 않습니다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 줄이 가리키는 인스턴스 함수입니다. 함수 정적이라 등록한 DLL 안에 하나입니다. */
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
     * @brief `Saved/Editor/EditorPreferences.json` 을 읽고 씁니다. 섹션 id 마다 JSON 오브젝트 하나이고, **기본값과 다른 프로퍼티만** 담습니다.
     * @details 값은 프로퍼티의 글 형식(`SerializerUtil::formatPropertyText`)입니다. 모르는 섹션과 키는 경고하고 건너뜁니다. 사용자 파일이라 기동을 막지 않습니다.
     *          사람이 커밋하는 설정 파일의 엄격 읽기와 다른 이유는, 확장 모듈을 끄면 그 섹션이 사라지기 때문입니다.
     */
    struct SW_EDITOR_API EditorPreferencesStore
    {
        /** @brief 등록된 섹션마다 파일의 값을 입힙니다(없는 값은 그대로). 입힌 섹션의 `_pfnOnChanged` 를 부릅니다. 파일이 없으면 false 입니다(처음 실행 — 정상). */
        [[nodiscard]] static bool loadAll( string_view filePath );
        /** @brief 등록된 섹션을 모두 기본과의 차이만으로 씁니다. 차이가 없는 섹션은 빼고, 모두 없으면 빈 오브젝트를 씁니다. */
        [[nodiscard]] static bool saveAll( string_view filePath );
        /** @brief @p pCurrent 가 @p pDefault 와 다른 프로퍼티만 담은 JSON 오브젝트 글입니다. */
        static string makeDifferenceJSON( const TypeInfo& type, const void* pCurrent, const void* pDefault );
        /** @brief JSON 오브젝트 글을 인스턴스에 입힙니다(없는 키는 그대로). 모르는 키는 @p outListUnknownKey 에 담습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool applyJSON( const TypeInfo& type, void* pInstance, string_view jsonText, vector<string>& outListUnknownKey );
        /** @brief @p prop 의 값이 기본값과 다르면 true 입니다(창의 "Modified only" · 고침 표시). */
        static bool isPropertyModified( const PropertyInfo& prop, const void* pCurrent, const void* pDefault );
        /** @brief 섹션 하나를 기본값으로 되돌리고 `_pfnOnChanged` 를 부릅니다. */
        static void resetSection( const EditorSettingsRegistration& registration );
        /** @brief 파일에 저장된 프로퍼티 수입니다(모든 섹션의 키 합). 파일이 없거나 틀리면 0 입니다. */
        static uint32 countSavedKeys( string_view filePath );
        /** @brief 저장 파일 경로입니다(에디터 상태 폴더의 `EditorPreferences.json`). */
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
     * @brief 섹션 `TSettings` 의 지금 값입니다. 등록되지 않았으면 기본값입니다.
     * @details 확장 모듈이 다른 모듈의 섹션을 읽을 때도 이 함수를 씁니다. 인스턴스는 등록한 DLL 의 것입니다.
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
 * @param TSettings 리플렉션 구조체(`REFLECT()` · `PROPERTY()`). 기본 생성자의 값이 기본값입니다.
 */
#define SW_EDITOR_SETTINGS( TSettings, pID, pLabel, order, pfnOnChanged )                                                               \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSettingsRegistration, Settings_##TSettings, { pID, order }, pLabel, &TSettings::StaticType, \
                        &::sw::editor::EditorSettingsInstance<TSettings>::getInstance, &::sw::editor::EditorSettingsInstance<TSettings>::getDefault, pfnOnChanged )
