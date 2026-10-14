/**
 * @file SocketEditModel.h
 * @brief 소켓 에셋(`*.sockets.xml`) 편집 모델입니다 — 소켓 추가 · 삭제 · 이름 바꾸기 · 부모 · 종류 · 변환 편집, 좌우 대칭, 검증, 되돌리기, 파일 읽기 · 쓰기. ImGui 와 씬을 모릅니다.
 * @details 편집 대상은 사람이 고치는 원본 파일이다(조립 결과가 아니다). 편집은 모두 `SocketSet` 을 직접 고치고, 편집 직전 표를 되돌리기 기록에 넣는다.
 *          저장은 `SocketSet::saveToXMLText` 이며 **적은 필드만** 쓰므로(`SocketFieldBit`), 값을 고치는 편집은 그 필드의 비트를 함께 켠다.
 *          이름을 바꾸면 다른 소켓의 후보 목록(`_listFallback`)이 따라간다. 부모 본 이름 검증은 본 목록(`setBoneNames`)이 있을 때만 한다.
 *          파일을 쓰면 핫 리로드가 같은 파일을 다시 읽는다 — 편집 중 다시 읽기와 겨루지 않게 하는 것(저장 직후 감시 무시 한 번)은 패널 쪽 일이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Socket/SocketSet.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/EditSnapshotHistory.h"

namespace sw::editor
{
    /**
     * @class SocketEditModel
     * @brief 소켓 표 하나와 그 편집 기록입니다.
     */
    class SocketEditModel
    {
    public:
        SocketEditModel();

        /** @brief 소켓 종류 표를 정합니다. 종류를 고르거나 검증할 때 이 표를 봅니다. 비어 있으면 종류 검증을 하지 않습니다. */
        void setKindTable( const SocketKindTable& kinds ) { _kinds = kinds; }
        /** @brief 부모로 쓸 수 있는 본 이름을 정합니다. 비어 있으면 부모 이름을 검증하지 않습니다. 가상 본은 따로 인정합니다. */
        void setBoneNames( const vector<hashed_string>& listBoneName ) { _listBoneName = listBoneName; }
        void setMirrorRules( const MirrorNameRules& rules ) { _mirrorRules = rules; }
        void setMirrorAxis( MirrorAxis axis ) { _mirrorAxis = axis; }

        const SocketKindTable& getKindTable() const { return _kinds; }
        const MirrorNameRules& getMirrorRules() const { return _mirrorRules; }
        MirrorAxis             getMirrorAxis() const { return _mirrorAxis; }

        // --- 읽기 · 쓰기 ---------------------------------------------------------------------------------------------

        /** @brief XML 텍스트에서 읽어 현재 내용을 바꿉니다. 실패하면 현재 내용을 그대로 두고 false 입니다. 성공하면 기록을 지우고 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 파일을 읽습니다. 파일이 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] bool loadFromFile( string_view absolutePath );
        /** @brief 새 문서로 비웁니다. 기록을 지우고 "저장된 상태" 로 맞춥니다. */
        void clear();
        /** @brief 현재 내용을 XML 텍스트로 씁니다. */
        string saveToXMLText() const { return _sockets.saveToXMLText(); }
        /** @brief 파일에 씁니다. 성공하면 "저장된 상태" 로 맞춥니다(되돌리기 기록은 유지). */
        [[nodiscard]] bool saveToFile( string_view absolutePath );

        // --- 조회 ----------------------------------------------------------------------------------------------------

        const SocketSet& getSockets() const { return _sockets; }
        const SocketDef* findSocket( const hashed_string& name ) const { return _sockets.findSocket( name ); }
        /** @brief 저장된 상태와 내용이 다르면 true 입니다. */
        bool isDirty() const;
        /**
         * @brief 문제를 모아 @p outListIssue 에 한 줄씩 추가합니다(영어 문장). 모르는 종류, 없는 부모(본 목록이 있을 때), 없는 후보 이름이 해당합니다.
         * @return 문제가 없으면 true 입니다.
         */
        bool validate( vector<string>& outListIssue ) const;

        // --- 편집 ----------------------------------------------------------------------------------------------------
        // 실패하면 표를 바꾸지 않고 되돌리기 기록도 남기지 않습니다. 이유는 @p outError 에 영어 한 문장으로 담습니다.
        // bContinuous 가 true 이면 같은 소켓 · 같은 필드의 연속 편집(끌기)을 되돌리기 한 단계로 합칩니다. 끌기가 끝나면 `endContinuousEdit` 를 부릅니다.

        [[nodiscard]] bool addSocket( const hashed_string& name, const hashed_string& parent, const hashed_string& kind, string& outError );
        [[nodiscard]] bool removeSocket( const hashed_string& name, string& outError );
        /** @brief 이름을 바꿉니다. 다른 소켓의 후보 목록에 있는 옛 이름도 새 이름으로 바꿉니다. 표 안의 순서는 유지합니다. */
        [[nodiscard]] bool renameSocket( const hashed_string& oldName, const hashed_string& newName, string& outError );
        [[nodiscard]] bool setParent( const hashed_string& name, const hashed_string& parent, string& outError );
        [[nodiscard]] bool setKind( const hashed_string& name, const hashed_string& kind, string& outError );
        [[nodiscard]] bool setTranslation( const hashed_string& name, const float3& translation, bool bContinuous, string& outError );
        /** @brief 회전을 도(피치 · 요 · 롤, 파일의 `rotation` 과 같은 배치)로 정합니다. */
        [[nodiscard]] bool setRotationDegrees( const hashed_string& name, const float3& degrees, bool bContinuous, string& outError );
        [[nodiscard]] bool setScale( const hashed_string& name, const float3& scale, bool bContinuous, string& outError );
        [[nodiscard]] bool setPreviewMesh( const hashed_string& name, string_view previewMesh, string& outError );
        [[nodiscard]] bool setAnchor( const hashed_string& name, SocketAnchor anchor, string& outError );
        [[nodiscard]] bool setFallback( const hashed_string& name, const vector<hashed_string>& listFallback, string& outError );
        /**
         * @brief 소켓의 반대편 짝을 만들거나(없으면) 맞춥니다(있으면). 이름은 규칙 표로, 부모 본은 같은 규칙으로 반대편 이름이 있으면 그것을(없으면 같은 부모를),
         *        위치 · 회전은 대칭 축으로 뒤집어 씁니다. 종류 · 크기 · 미리보기 · 앵커 · 후보는 그대로 옮기고 후보 이름도 반대편으로 바꿉니다.
         * @param outMirrorName 짝 소켓의 이름입니다.
         * @details 이름에 짝 토큰이 없는 소켓(가운데)은 대칭할 수 없어 실패합니다. 짝 이름이 원본 자신이 되는 경우는 없다(토큰이 서로 다르다).
         */
        [[nodiscard]] bool mirrorSocket( const hashed_string& name, hashed_string& outMirrorName, string& outError );
        /** @brief 연속 편집을 끝냅니다. 다음 편집은 새 되돌리기 단계를 만듭니다. */
        void endContinuousEdit() { _history.endCoalescing(); }

        // --- 되돌리기 ------------------------------------------------------------------------------------------------

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _history.canUndo(); }
        bool               canRedo() const { return _history.canRedo(); }

    private:
        /** @brief 편집 직전에 부릅니다. */
        void recordUndo( string_view coalesceKey = {} );
        /** @brief 이름의 소켓을 복사해 @p outSocket 에 담습니다. 없으면 오류를 쓰고 false 입니다. */
        [[nodiscard]] bool copySocket( const hashed_string& name, SocketDef& outSocket, string& outError ) const;
        /** @brief 소켓 목록을 통째로 바꿉니다(가상 본은 유지). */
        void replaceSockets( const vector<SocketDef>& listSocket );
        bool isParentAllowed( const hashed_string& parent ) const;
        bool isKindAllowed( const hashed_string& kind ) const;
        /** @brief 값을 고치는 편집의 공통 경로입니다. 소켓을 복사해 @p fieldBit 를 켜고 되돌리기를 남긴 뒤 표에 되돌려 넣습니다. */
        bool beginFieldEdit( const hashed_string& name, uint16 fieldBit, bool bContinuous, string_view fieldName, SocketDef& outSocket, string& outError );

        SocketSet                      _sockets;
        SocketKindTable                _kinds;
        MirrorNameRules                _mirrorRules;
        vector<hashed_string>          _listBoneName;
        string                         _savedText;
        EditSnapshotHistory<SocketSet> _history;
        MirrorAxis                     _mirrorAxis;
    };
} // namespace sw::editor
