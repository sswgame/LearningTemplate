#include "pch.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief `PROPERTY( Required )` 칸이 json 최상위 키로 모두 있으면 true 입니다. 빠진 칸마다 키 이름과 함께 오류를 남깁니다.
         * @details 엄격한 JSON 읽기(`ConfigManager::readConfigJSON`)는 모르는 키만 막고 빠진 키는 멤버 초기값으로 둔다. 이 타입의 필수 칸은 초기값이
         *          형식일 뿐이라(빈 글꼴 목록 · 0 색) 빠진 채로 서면 조용히 틀린 값으로 돈다.
         */
        [[nodiscard]] bool hasEveryRequiredKey( string_view jsonText, const utf8* pSourceLabel )
        {
            JSONDocument document;
            if ( document.parse( jsonText ) == false || document.getRoot().isObject() == false )
                return false; // 읽기(readConfigJSON)가 이미 오류를 남겼다
            const JSONValue root      = document.getRoot();
            bool            bComplete = true;
            for ( const PropertyInfo& prop : EditorToolDefaults::StaticType()->getPropertiesWithBase() )
            {
                // 판정은 직렬화기 · XMLFields 와 한 함수다 — 빠졌거나, 이름 · 경로 형식(DataPath)인데 빈 글이다.
                const JSONValue   value = root.get( prop._name.view() );
                const string      text  = value.isValid() && value.isString() ? value.asString() : string{};
                const string_view textView( text );
                if ( SerializerUtil::isRequiredValueMissing( prop, value.isValid(), value.isValid() && value.isString() ? &textView : nullptr ) == false )
                    continue;
                SW_LOG_ERROR_ID( "EditorToolDefaults.EditorToolDefaultsRequired", "Editor tool defaults %#: required key '%#' is missing", pSourceLabel, prop._name.c_str() );
                bComplete = false;
            }
            return bComplete;
        }
    } // namespace

    AbsolutePath EditorToolDefaults::getHostPath()
    {
        // 경로의 원본은 `Scripts/common/ProjectConstants.py` 하나다. 경로를 바꾸는 칸을 `EditorAppearanceConfig` 에 두지 말 것 — 그 파일은
        // **테마를 저장할 때마다 기계가 다시 쓴다.** 상수는 프로젝트 루트 기준 상대 경로라 푸는 일이 실패할 수 없다.
        const optional<AbsolutePath> path = AbsolutePath::tryResolve( config::kFileRuntimeEditorToolDefaults, ProcessRoots::get()._project );
        SW_ASSERT( path.has_value() );
        return *path;
    }

    bool EditorToolDefaults::loadFromHost()
    {
        const AbsolutePath hostPath = getHostPath();
        const string&      absPath  = hostPath.str();
        // 값의 원본은 이 파일 하나다 — 없으면 대신 쓸 값이 없다(헤더는 형식만 가진다).
        if ( FileUtil::isRegularFile( absPath ) == false )
        {
            SW_LOG_ERROR_ID( "EditorToolDefaults.EditorToolDefaultsMissing", "Editor tool defaults %# is missing - the editor needs this data file", absPath );
            return false;
        }
        string jsonText;
        if ( FileUtil::readTextFile( absPath, jsonText ) == false )
        {
            SW_LOG_ERROR_ID( "EditorToolDefaults.EditorToolDefaultsCould", "Editor tool defaults %# could not be read", absPath );
            return false;
        }

        // 따로 읽고 성공할 때만 덮는다 — 틀린 파일의 앞부분 값이 섞이면 무엇이 맞는 값인지 아무도 모른다. 두 검사는 오류를 모두 남기도록 둘 다 돈다.
        EditorToolDefaults loaded{};
        const bool         bRead     = ConfigManager::readConfigJSON( loaded, jsonText, absPath.c_str() );
        const bool         bComplete = hasEveryRequiredKey( jsonText, absPath.c_str() );
        if ( bRead == false || bComplete == false )
            return false;
        *this = std::move( loaded );
        SW_LOG_INFO_ID( "EditorToolDefaults.Loaded", "Loaded from %#", absPath );
        return true;
    }
} // namespace sw::editor
