/**
 * @file FileDialog.h
 * @brief 네이티브 파일 열기 · 저장 대화상자입니다. 편집기 빌드만 짓는다(`SW_WITH_EDITOR` — 언리얼 DesktopPlatform 처럼 개발 도구 전용).
 * @details 편집기 DLL 이 아니라 Core 에 두는 까닭: 대화상자는 분리된 스레드가 띄우고 사용자가 닫을 때까지 돌아오지 않는다. 그 스레드의 코드가
 *          핫 리로드로 내려가는 DLL 안에 있으면 내린 뒤에 돌아와 죽는다.
 */
#pragma once
#include "Core/Foundation/EditorBuild.h"

#if SW_WITH_EDITOR == 0
    #error "FileDialog 는 편집기 빌드 전용이다 - 배포 빌드의 코드는 파일 대화상자를 쓰지 않는다"
#endif
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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

    /** @brief 네이티브 파일 대화상자입니다. **결과 델리게이트는 메인 스레드에서 불립니다**(`pumpResults`). */
    class SW_API FileDialog
    {
    public:
        /**
         * @brief 네이티브 파일 다이얼로그를 엽니다. **결과 델리게이트는 메인 스레드에서 불립니다.**
         * @details 다이얼로그 자체는 분리된(detached) 스레드가 띄웁니다. 네이티브 다이얼로그는 사용자가 닫을 때까지 돌아오지
         *          않으므로 그 호출에서 기다리면 프레임이 멈추기 때문입니다. 그래서 **결과만** 큐에 담고, `pumpResults`
         *          가 메인 스레드에서 꺼내 델리게이트를 부릅니다.
         *
         *          그 스레드에서 곧바로 델리게이트를 부르면 콜백이 씬 · 컴포넌트 · 에디터 상태처럼 메인 스레드가 매 프레임
         *          만지는 것들을 **동시에** 고치게 됩니다(씬 직렬화 · 컴포넌트 역직렬화 등). 호출하는 곳마다 큐를 하나씩 두는 대신
         *          여기서 한 번에 막습니다.
         * @param params 다이얼로그 설정
         * @param onSuccess 사용자가 파일을 골랐을 때 **메인 스레드에서** 불릴 델리게이트(취소하면 불리지 않습니다)
         */
        static void open( const FileDialogParams& params, FileDialogDelegate onSuccess );
        /**
         * @brief 완료된 파일 다이얼로그 결과를 처리합니다. **메인 스레드에서 프레임마다** 부릅니다.
         * @details 아무도 부르지 않으면 결과가 전달되지 않을 뿐 경합은 없습니다(헤드리스 · 도구 실행이 그렇습니다).
         */
        static void pumpResults();
        /**
         * @brief 아직 전달하지 않은 파일 다이얼로그 결과를 버립니다. 종료할 때나 모듈을 언로드할 때 부릅니다.
         * @details 델리게이트는 로드 가능한 모듈(EditorModule 등) 안의 코드를 가리킬 수 있고, 델리게이트는 대상이 살아 있는지
         *          확인하지 않습니다. 그래서 그 모듈이 내려가기 전에 이 함수를 불러 연결을 끊어야 합니다. 이미 열려 있는
         *          다이얼로그의 결과도 세대 번호로 함께 버려집니다.
         */
        static void cancelResults();
    };
} // namespace sw
