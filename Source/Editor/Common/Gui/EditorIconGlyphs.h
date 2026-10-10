/**
 * @file EditorIconGlyphs.h
 * @brief 에디터 아이콘 폰트(`editor/fonts/sweditoricons.ttf`)의 글리프 문자열입니다. **생성 파일 — 고치지 마십시오.**
 * @details `Scripts/generate/GenerateEditorIcons.py` 가 같은 아이콘 목록에서 폰트와 이 헤더를 함께 만듭니다. 아이콘을 추가하려면
 *          `Scripts/common/EditorIconFont.py` 의 `kListIcon` 에 항목을 넣고 스크립트를 다시 실행합니다. 코드에서는 상수 이름으로만 쓰고
 *          바이트를 직접 쓰지 않습니다. 목록 순서가 코드포인트이기 때문입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor::editoricon
{
    inline constexpr const utf8* kFile             = "\xee\x80\x80"; ///< U+E000
    inline constexpr const utf8* kFolder           = "\xee\x80\x81"; ///< U+E001
    inline constexpr const utf8* kFolderOpen       = "\xee\x80\x82"; ///< U+E002
    inline constexpr const utf8* kSave             = "\xee\x80\x83"; ///< U+E003
    inline constexpr const utf8* kUndo             = "\xee\x80\x84"; ///< U+E004
    inline constexpr const utf8* kRedo             = "\xee\x80\x85"; ///< U+E005
    inline constexpr const utf8* kSearch           = "\xee\x80\x86"; ///< U+E006
    inline constexpr const utf8* kTerminal         = "\xee\x80\x87"; ///< U+E007
    inline constexpr const utf8* kExit             = "\xee\x80\x88"; ///< U+E008
    inline constexpr const utf8* kSettings         = "\xee\x80\x89"; ///< U+E009
    inline constexpr const utf8* kPalette          = "\xee\x80\x8a"; ///< U+E00A
    inline constexpr const utf8* kLayout           = "\xee\x80\x8b"; ///< U+E00B
    inline constexpr const utf8* kTable            = "\xee\x80\x8c"; ///< U+E00C
    inline constexpr const utf8* kHammer           = "\xee\x80\x8d"; ///< U+E00D
    inline constexpr const utf8* kWrench           = "\xee\x80\x8e"; ///< U+E00E
    inline constexpr const utf8* kBuildAll         = "\xee\x80\x8f"; ///< U+E00F
    inline constexpr const utf8* kCancel           = "\xee\x80\x90"; ///< U+E010
    inline constexpr const utf8* kSpinner          = "\xee\x80\x91"; ///< U+E011
    inline constexpr const utf8* kCheck            = "\xee\x80\x92"; ///< U+E012
    inline constexpr const utf8* kClose            = "\xee\x80\x93"; ///< U+E013
    inline constexpr const utf8* kSuccess          = "\xee\x80\x94"; ///< U+E014
    inline constexpr const utf8* kWarning          = "\xee\x80\x95"; ///< U+E015
    inline constexpr const utf8* kError            = "\xee\x80\x96"; ///< U+E016
    inline constexpr const utf8* kInfo             = "\xee\x80\x97"; ///< U+E017
    inline constexpr const utf8* kLock             = "\xee\x80\x98"; ///< U+E018
    inline constexpr const utf8* kUnlock           = "\xee\x80\x99"; ///< U+E019
    inline constexpr const utf8* kPlus             = "\xee\x80\x9a"; ///< U+E01A
    inline constexpr const utf8* kMinus            = "\xee\x80\x9b"; ///< U+E01B
    inline constexpr const utf8* kTrash            = "\xee\x80\x9c"; ///< U+E01C
    inline constexpr const utf8* kCopy             = "\xee\x80\x9d"; ///< U+E01D
    inline constexpr const utf8* kRefresh          = "\xee\x80\x9e"; ///< U+E01E
    inline constexpr const utf8* kFilter           = "\xee\x80\x9f"; ///< U+E01F
    inline constexpr const utf8* kStar             = "\xee\x80\xa0"; ///< U+E020
    inline constexpr const utf8* kLink             = "\xee\x80\xa1"; ///< U+E021
    inline constexpr const utf8* kEye              = "\xee\x80\xa2"; ///< U+E022
    inline constexpr const utf8* kEyeSlash         = "\xee\x80\xa3"; ///< U+E023
    inline constexpr const utf8* kPlay             = "\xee\x80\xa4"; ///< U+E024
    inline constexpr const utf8* kPause            = "\xee\x80\xa5"; ///< U+E025
    inline constexpr const utf8* kStop             = "\xee\x80\xa6"; ///< U+E026
    inline constexpr const utf8* kStepForward      = "\xee\x80\xa7"; ///< U+E027
    inline constexpr const utf8* kTranslate        = "\xee\x80\xa8"; ///< U+E028
    inline constexpr const utf8* kRotate           = "\xee\x80\xa9"; ///< U+E029
    inline constexpr const utf8* kScale            = "\xee\x80\xaa"; ///< U+E02A
    inline constexpr const utf8* kGlobe            = "\xee\x80\xab"; ///< U+E02B
    inline constexpr const utf8* kAxes             = "\xee\x80\xac"; ///< U+E02C
    inline constexpr const utf8* kMagnet           = "\xee\x80\xad"; ///< U+E02D
    inline constexpr const utf8* kGrid             = "\xee\x80\xae"; ///< U+E02E
    inline constexpr const utf8* kBookmark         = "\xee\x80\xaf"; ///< U+E02F
    inline constexpr const utf8* kAlign            = "\xee\x80\xb0"; ///< U+E030
    inline constexpr const utf8* kChart            = "\xee\x80\xb1"; ///< U+E031
    inline constexpr const utf8* kCamera           = "\xee\x80\xb2"; ///< U+E032
    inline constexpr const utf8* kCube             = "\xee\x80\xb3"; ///< U+E033
    inline constexpr const utf8* kScene            = "\xee\x80\xb4"; ///< U+E034
    inline constexpr const utf8* kPrefab           = "\xee\x80\xb5"; ///< U+E035
    inline constexpr const utf8* kTexture          = "\xee\x80\xb6"; ///< U+E036
    inline constexpr const utf8* kShader           = "\xee\x80\xb7"; ///< U+E037
    inline constexpr const utf8* kMaterial         = "\xee\x80\xb8"; ///< U+E038
    inline constexpr const utf8* kAudio            = "\xee\x80\xb9"; ///< U+E039
    inline constexpr const utf8* kAnimGraph        = "\xee\x80\xba"; ///< U+E03A
    inline constexpr const utf8* kDialogue         = "\xee\x80\xbb"; ///< U+E03B
    inline constexpr const utf8* kSprite           = "\xee\x80\xbc"; ///< U+E03C
    inline constexpr const utf8* kTileMap          = "\xee\x80\xbd"; ///< U+E03D
    inline constexpr const utf8* kSequence         = "\xee\x80\xbe"; ///< U+E03E
    inline constexpr const utf8* kSkeleton         = "\xee\x80\xbf"; ///< U+E03F
    inline constexpr const utf8* kAnimClip         = "\xee\x81\x80"; ///< U+E040
    inline constexpr const utf8* kRig              = "\xee\x81\x81"; ///< U+E041
    inline constexpr const utf8* kHeightfield      = "\xee\x81\x82"; ///< U+E042
    inline constexpr const utf8* kFracture         = "\xee\x81\x83"; ///< U+E043
    inline constexpr const utf8* kWidget           = "\xee\x81\x84"; ///< U+E044
    inline constexpr const utf8* kFont             = "\xee\x81\x85"; ///< U+E045
    inline constexpr const utf8* kGameObject       = "\xee\x81\x86"; ///< U+E046
    inline constexpr const utf8* kComponent        = "\xee\x81\x87"; ///< U+E047
    inline constexpr const utf8* kLightPoint       = "\xee\x81\x88"; ///< U+E048
    inline constexpr const utf8* kLightDirectional = "\xee\x81\x89"; ///< U+E049
    inline constexpr const utf8* kLightSpot        = "\xee\x81\x8a"; ///< U+E04A
    inline constexpr const utf8* kLightGlobal      = "\xee\x81\x8b"; ///< U+E04B
    inline constexpr const utf8* kAudioEmitter     = "\xee\x81\x8c"; ///< U+E04C
    inline constexpr const utf8* kAudioListener    = "\xee\x81\x8d"; ///< U+E04D
    inline constexpr const utf8* kAudioZone        = "\xee\x81\x8e"; ///< U+E04E
    inline constexpr const utf8* kPhysics          = "\xee\x81\x8f"; ///< U+E04F
    inline constexpr const utf8* kCollider         = "\xee\x81\x90"; ///< U+E050
    inline constexpr const utf8* kCharacter        = "\xee\x81\x91"; ///< U+E051
    inline constexpr const utf8* kAnimation        = "\xee\x81\x92"; ///< U+E052
    inline constexpr const utf8* kController       = "\xee\x81\x93"; ///< U+E053
    inline constexpr const utf8* kAI               = "\xee\x81\x94"; ///< U+E054
    inline constexpr const utf8* kNavigation       = "\xee\x81\x95"; ///< U+E055
    inline constexpr const utf8* kTrigger          = "\xee\x81\x96"; ///< U+E056
    inline constexpr const utf8* kSpawnPoint       = "\xee\x81\x97"; ///< U+E057
    inline constexpr const utf8* kSpline           = "\xee\x81\x98"; ///< U+E058
    inline constexpr const utf8* kWater            = "\xee\x81\x99"; ///< U+E059
    inline constexpr const utf8* kFoliage          = "\xee\x81\x9a"; ///< U+E05A
    inline constexpr const utf8* kWind             = "\xee\x81\x9b"; ///< U+E05B
    inline constexpr const utf8* kHealth           = "\xee\x81\x9c"; ///< U+E05C
    inline constexpr const utf8* kAbility          = "\xee\x81\x9d"; ///< U+E05D
    inline constexpr const utf8* kVehicle          = "\xee\x81\x9e"; ///< U+E05E
    inline constexpr const utf8* kTag              = "\xee\x81\x9f"; ///< U+E05F
    inline constexpr const utf8* kMissing          = "\xee\x81\xa0"; ///< U+E060
    inline constexpr const utf8* kBug              = "\xee\x81\xa1"; ///< U+E061
    inline constexpr const utf8* kMap              = "\xee\x81\xa2"; ///< U+E062

    /** @brief 폰트에 있는 코드포인트 구간입니다. ImGui 글리프 범위 형식이고 0 으로 끝납니다. 뒤의 다섯 구간은 ImGuiNotify 가 쓰는 코드포인트입니다. */
    inline constexpr uint16 kArrGlyphRange[] = { 0xE000, 0xE062, 0xF00D, 0xF00D, 0xF058, 0xF058, 0xF05A, 0xF05A, 0xF06A, 0xF06A, 0xF071, 0xF071, 0 };
    /** @brief 폰트에 있는 아이콘 수입니다. */
    inline constexpr uint32 kIconCount = 99;
} // namespace sw::editor::editoricon
