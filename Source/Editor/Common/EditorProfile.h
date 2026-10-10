/**
 * @file EditorProfile.h
 * @brief 에디터 모듈의 프레임 계측 구간(`SW_EDITOR_PROFILE_SCOPE`)입니다 — 엔진의 `SW_PROFILE_SCOPE` 와 같은 표에 쌓입니다.
 * @details 엔진 매크로는 `engine::getFrameProfiler()`(엔진 내부 서비스 표)를 부르므로 에디터 모듈은 쓸 수 없다. 여기서는 에디터 서비스 로케이터로
 *          프로파일러를 받아 같은 표에 등록한다. 구간 이름은 엔진이 복사해 든다(`FrameProfiler::registerScope`) — 모듈을 다시 올려도 표가 매달린
 *          포인터를 들지 않는다. 지점은 함수 지역 static 이라 모듈을 다시 올리면 다시 등록되고, 같은 이름은 같은 슬롯이다.
 */
#pragma once
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    /**
     * @struct EditorProfileUtil
     * @brief `SW_EDITOR_PROFILE_SCOPE` 의 등록 창구입니다.
     */
    struct EditorProfileUtil
    {
        /** @brief 구간 지점을 등록합니다. 프로파일러 서비스가 없으면 아무 데도 쌓이지 않는 지점(빈 슬롯)입니다. */
        static ProfileScopeID registerScope( const utf8* pName, const utf8* pFunction, const utf8* pFile, uint32 line )
        {
            FrameProfiler* pProfiler = getService<FrameProfiler>();
            if ( pProfiler != nullptr )
                return pProfiler->registerScopeSite( pName, pFunction, pFile, line );
            ProfileScopeID emptyID{};
            emptyID._slot = FrameProfiler::kInvalidSlot;
            return emptyID;
        }
    };
} // namespace sw::editor

/** @brief 이 스코프의 CPU 시간을 name 구간에 쌓습니다(에디터 모듈용 `SW_PROFILE_SCOPE`). */
#define SW_EDITOR_PROFILE_SCOPE( name )                                                                              \
    static const ::sw::ProfileScopeID SW_PROFILE_CONCAT( swEditorProfileSlot_, __LINE__ ) =                          \
        ::sw::editor::EditorProfileUtil::registerScope( name, __func__, __FILE__, static_cast<uint32>( __LINE__ ) ); \
    ::sw::ScopedFrameProfile SW_PROFILE_CONCAT( swEditorProfileScope_, __LINE__ )                                    \
    {                                                                                                                \
        SW_PROFILE_CONCAT( swEditorProfileSlot_, __LINE__ )                                                          \
    }
