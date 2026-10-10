/**
 * @file ModuleBuildID.h
 * @brief 실행 파일 · DLL 의 빌드 id — 심볼(PDB · 디버그 정보)과 덤프를 짝짓는 열쇠입니다.
 * @details Windows 는 PE 디버그 디렉터리의 CodeView(RSDS) 서명(GUID + age)과 PDB 경로, 리눅스는 ELF `NT_GNU_BUILD_ID` 노트입니다. 미니덤프의 모듈 목록에
 *          같은 값이 들어 있어, 심볼 서버(`symstore` · Sentry · Backtrace)는 이 열쇠로 덤프에 맞는 심볼을 찾습니다. 크래시 보고 묶음 · 텔레메트리 문맥이
 *          이 값을 적습니다. 매 빌드 다른 값이라 "같은 빌드에서 난 크래시인가" 의 답이기도 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @struct ModuleBuildID
     * @brief 모듈 하나의 빌드 id 입니다.
     */
    struct SW_API ModuleBuildID
    {
        string _id{};         ///< 심볼 서버 열쇠 — Windows `<GUID 32 자리><age 16진>`(symstore 디렉터리 이름), 리눅스 build-id 16진
        string _debugFile{};  ///< Windows 는 링커가 적은 PDB 경로, 리눅스는 비어 있다
        string _modulePath{}; ///< 모듈 파일 경로

        bool isValid() const { return _id.empty() == false; }

        /**
         * @brief @p pAddressInside 를 담은 모듈의 빌드 id 입니다. nullptr 이면 실행 파일입니다. 찾지 못하면(서명 없는 링크) 빈 값입니다.
         * @details 디스크를 읽지 않습니다 — 올라온 이미지의 머리에서 읽습니다.
         */
        static ModuleBuildID find( const void* pAddressInside );
    };
} // namespace sw
