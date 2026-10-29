/**
 * @file Editor/Common/Backend/FileDialog.h
 * @brief 네이티브 파일 열기 · 저장 대화상자입니다(언리얼 DesktopPlatform 자리 — 편집기 도구 전용).
 * @details 대화상자는 별도 스레드가 띄우고 사용자가 닫을 때까지 돌아오지 않는다. 그 스레드가 이 DLL 의 코드로 돌아오므로, 열려 있는 동안(`isOpen` —
 *          스레드를 합류시킬 때까지)은 편집기 모듈 리로드를 잡아 둔다(`LiveReloadManager::addReloadHold`, 편집기 API `isReloadHeld`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // FileDialogParams — 열기/저장, 필터 · 시작 폴더 · 다중 선택
    // ------------------------------------------------------------------------------
    /** @brief 네이티브 파일 열기 · 저장 다이얼로그의 매개변수입니다. */
    struct FileDialogParams
    {
        /** @brief 열기인지 저장인지 나타냅니다. */
        enum class Type : uint8
        {
            Open,
            Save,
        };

        Type           _type{ Type::Open };         ///< Open 또는 Save
        string         _title;                      ///< 다이얼로그 창 제목(비어 있으면 OS 기본값)
        string         _description;                ///< 필터 설명 문자열
        vector<string> _listFilterExtension;        ///< 허용 확장자 목록(예: ".png", "hlsl")
        string         _initialDirectory;           ///< 시작 폴더(비어 있으면 OS 기본값)
        bool           _bEnableMultiselect{ true }; ///< 다중 선택 허용(열기에서만)
    };
} // namespace sw

namespace sw
{
    SW_DECLARE_DELEGATE( void, FileDialogDelegate, const vector<string>& fileName );

    /** @brief 네이티브 파일 대화상자입니다. **결과 델리게이트는 메인 스레드에서 불립니다**(`pumpResults`). 한 번에 하나만 열린다. */
    class SW_EDITOR_API FileDialog
    {
    public:
        /**
         * @brief 네이티브 파일 다이얼로그를 엽니다. **결과 델리게이트는 메인 스레드에서 불립니다.** 이미 열려 있으면 경고하고 열지 않습니다.
         * @details 다이얼로그 자체는 별도 스레드가 띄웁니다. 네이티브 다이얼로그는 사용자가 닫을 때까지 돌아오지
         *          않으므로 그 호출에서 기다리면 프레임이 멈추기 때문입니다. 그래서 **결과만** 담고, `pumpResults`
         *          가 메인 스레드에서 스레드를 합류시키고 델리게이트를 부릅니다.
         *
         *          그 스레드에서 곧바로 델리게이트를 부르면 콜백이 씬 · 컴포넌트 · 에디터 상태처럼 메인 스레드가 매 프레임
         *          만지는 것들을 **동시에** 고치게 됩니다(씬 직렬화 · 컴포넌트 역직렬화 등). 호출하는 곳마다 큐를 하나씩 두는 대신
         *          여기서 한 번에 막습니다.
         * @param params 다이얼로그 설정
         * @param onSuccess 사용자가 파일을 골랐을 때 **메인 스레드에서** 불릴 델리게이트(취소하면 불리지 않습니다)
         */
        static void open( const FileDialogParams& params, FileDialogDelegate onSuccess );
        /** @brief 대화상자가 닫혔으면 스레드를 합류시키고 결과를 넘깁니다. **메인 스레드에서 프레임마다** 부릅니다. */
        static void pumpResults();
        /**
         * @brief 열린 대화상자의 결과를 버립니다(델리게이트만 놓는다). 편집기 인스턴스를 없앨 때 부릅니다 — 델리게이트가 가리키는 객체가 사라진다.
         * @details 스레드는 그대로 두고 대화상자가 닫히면 `pumpResults` 가 합류시킨다. 그때까지 `isOpen` 은 true 라 리로드는 계속 잡혀 있다.
         */
        static void cancelResults();
        /** @brief 대화상자 스레드가 아직 합류되지 않았으면 true 입니다. 이 동안 편집기 모듈을 내리면 안 된다(리로드 잡음). */
        static bool isOpen();
    };
} // namespace sw
