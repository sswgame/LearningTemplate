#include "pch.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/EditorPaths.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorToolDefaults" );

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
                if ( prop._metadata->_bRequired == SW_FALSE || root.has( prop._name.view(), false ) )
                    continue;
                SW_LOG_ERROR( "Editor tool defaults %#: required key '%#' is missing", pSourceLabel, prop._name.c_str() );
                bComplete = false;
            }
            return bComplete;
        }
    } // namespace

    bool EditorToolDefaults::loadFromHostPath( string_view hostRelativePath )
    {
        // 경로의 원본은 `Scripts/common/Constants.py` 하나다. 경로를 바꾸는 칸을 `EditorConfig` 에 두지 말 것 — 그 파일은
        // **테마를 저장할 때마다 기계가 다시 쓴다.**
        string rel = string( hostRelativePath );
        if ( rel.empty() )
            rel = string( config::kFileRuntimeEditorToolDefaults );

        const string absPath = EditorPaths::resolveProjectRelativePath( rel );
        // 값의 원본은 이 파일 하나다 — 없으면 대신 쓸 값이 없다(헤더는 형식만 가진다).
        if ( FileUtil::isRegularFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Editor tool defaults %# is missing - the editor needs this data file", absPath );
            return false;
        }
        string jsonText;
        if ( FileUtil::readTextFile( absPath, jsonText ) == false )
        {
            SW_LOG_ERROR( "Editor tool defaults %# could not be read", absPath );
            return false;
        }

        // 따로 읽고 성공할 때만 덮는다 — 틀린 파일의 앞부분 값이 섞이면 무엇이 맞는 값인지 아무도 모른다. 두 검사는 오류를 모두 남기도록 둘 다 돈다.
        EditorToolDefaults loaded{};
        const bool         bRead     = ConfigManager::readConfigJSON( loaded, jsonText, absPath.c_str() );
        const bool         bComplete = hasEveryRequiredKey( jsonText, absPath.c_str() );
        if ( bRead == false || bComplete == false )
            return false;
        *this = std::move( loaded );
        SW_LOG_INFO( "Loaded from %#", absPath );
        return true;
    }
} // namespace sw::editor
