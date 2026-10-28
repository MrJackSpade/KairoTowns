package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.ControllerBinding
import com.mrjackspade.kairo.frontend.ControllerBindingsCodec
import com.mrjackspade.kairo.frontend.ControllerLayout
import com.mrjackspade.kairo.frontend.GuestKeyboardLayout
import com.mrjackspade.kairo.frontend.GuestKeyboardPage
import com.mrjackspade.kairo.frontend.JisMainBlock
import com.mrjackspade.kairo.frontend.JisMainBlock.RETURN
import com.mrjackspade.kairo.frontend.JisMainBlock.SPACE
import com.mrjackspade.kairo.frontend.JoystickInputRouter
import com.mrjackspade.kairo.frontend.KeyboardKey
import com.mrjackspade.kairo.frontend.UiText
import com.mrjackspade.kairo.frontend.R as FrontendR
import android.view.KeyEvent

/**
 * The FM Towns JIS keyboard as Tsugaru's key codes (TOWNS_JISKEY_*, towns/townsdef/townsdef.h),
 * and the gamepad in game port 0. The JIS main block, 0x01 to 0x35, is [JisMainBlock]; these
 * are the rest.
 */
object TownsKeys {
    const val NUM_STAR = 0x36
    const val NUM_SLASH = 0x37
    const val NUM_PLUS = 0x38
    const val NUM_MINUS = 0x39
    const val NUM_EQUAL = 0x3D
    const val NUM_RETURN = 0x45
    const val NUM_DOT = 0x47
    const val INSERT = 0x48
    const val NUM_000 = 0x4A
    const val DELETE = 0x4B
    const val UP = 0x4D
    const val HOME = 0x4E
    const val LEFT = 0x4F
    const val DOWN = 0x50
    const val RIGHT = 0x51
    const val CTRL = 0x52
    const val SHIFT = 0x53
    const val CAPS = 0x55
    const val HIRAGANA = 0x56
    const val NO_CONVERT = 0x57
    const val CONVERT = 0x58
    const val KANA_KANJI = 0x59
    const val KATAKANA = 0x5A
    const val ALT = 0x5C
    const val PREV = 0x6E
    const val NEXT = 0x70
    const val CANCEL = 0x72
    const val EXECUTE = 0x73
    const val BREAK = 0x7C
    const val COPY = 0x7D
    const val MAX_CODE = 0x7F

    private val numpad = mapOf(7 to 0x3A, 8 to 0x3B, 9 to 0x3C, 4 to 0x3E, 5 to 0x3F, 6 to 0x40,
        1 to 0x42, 2 to 0x43, 3 to 0x44, 0 to 0x46)
    /** PF1 to PF20; the codes are not in order. */
    private val pf = listOf(0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x69, 0x5B,
        0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B)

    val layout = GuestKeyboardLayout(
        brand = "KAIROTOWNS",
        pages = listOf(
            GuestKeyboardPage("ABC", JisMainBlock.alphabetRows(CTRL, SHIFT, listOf(KeyboardKey("Caps", CAPS),
                KeyboardKey("Alt", ALT), KeyboardKey("Kana", KATAKANA), KeyboardKey("Space", SPACE, 5f),
                KeyboardKey("Exec", EXECUTE), KeyboardKey("Cancel", CANCEL)))),
            GuestKeyboardPage("?123", JisMainBlock.symbolRows(), pickerLabel = UiText.res(R.string.keyboard_symbols)),
            GuestKeyboardPage("Towns", townsRows())
        ),
        modifiers = setOf(SHIFT, CTRL, ALT),
        shiftCodes = setOf(SHIFT),
        capsCode = null,
        chordShiftCode = SHIFT
    )

    private fun townsRows(): List<List<KeyboardKey>> = listOf(
        (0..9).map { KeyboardKey("PF${it + 1}", pf[it], shifted = "PF${it + 11}") },
        listOf(KeyboardKey("Break", BREAK), KeyboardKey("Copy", COPY), KeyboardKey("Home", HOME),
            KeyboardKey("Ins", INSERT), KeyboardKey("Del", DELETE), KeyboardKey("Prev", PREV),
            KeyboardKey("Next", NEXT), KeyboardKey("Nfer", NO_CONVERT), KeyboardKey("Xfer", CONVERT)),
        listOf(KeyboardKey("7", numpad.getValue(7)), KeyboardKey("8", numpad.getValue(8)),
            KeyboardKey("9", numpad.getValue(9)), KeyboardKey("*", NUM_STAR), KeyboardKey("/", NUM_SLASH),
            KeyboardKey("-", NUM_MINUS)),
        listOf(KeyboardKey("4", numpad.getValue(4)), KeyboardKey("5", numpad.getValue(5)),
            KeyboardKey("6", numpad.getValue(6)), KeyboardKey("+", NUM_PLUS), KeyboardKey("=", NUM_EQUAL),
            KeyboardKey("↑", UP)),
        listOf(KeyboardKey("1", numpad.getValue(1)), KeyboardKey("2", numpad.getValue(2)),
            KeyboardKey("3", numpad.getValue(3)), KeyboardKey("0", numpad.getValue(0)), KeyboardKey("000", NUM_000),
            KeyboardKey(".", NUM_DOT), KeyboardKey("←", LEFT), KeyboardKey("↓", DOWN), KeyboardKey("→", RIGHT))
    )

    /** Every key a controller binding can send. */
    val bindableCodes: List<Int> = (1..MAX_CODE).filter { code -> label(code) != null }

    fun label(code: Int): String? = JisMainBlock.label(code) ?: when (code) {
        NUM_STAR -> "Numpad *"
        NUM_SLASH -> "Numpad /"
        NUM_PLUS -> "Numpad +"
        NUM_MINUS -> "Numpad -"
        NUM_EQUAL -> "Numpad ="
        NUM_RETURN -> "Numpad Return"
        NUM_DOT -> "Numpad ."
        NUM_000 -> "Numpad 000"
        in numpad.values -> "Numpad ${numpad.entries.first { it.value == code }.key}"
        INSERT -> "Ins"
        DELETE -> "Del"
        UP -> "Up"
        HOME -> "Home"
        LEFT -> "Left"
        DOWN -> "Down"
        RIGHT -> "Right"
        CTRL -> "Ctrl"
        SHIFT -> "Shift"
        CAPS -> "Caps"
        HIRAGANA -> "Hiragana"
        NO_CONVERT -> "Nfer"
        CONVERT -> "Xfer"
        KANA_KANJI -> "Kana/Kanji"
        KATAKANA -> "Katakana"
        ALT -> "Alt"
        PREV -> "Prev"
        NEXT -> "Next"
        CANCEL -> "Cancel"
        EXECUTE -> "Exec"
        BREAK -> "Break"
        COPY -> "Copy"
        in pf -> "PF${pf.indexOf(code) + 1}"
        else -> null
    }

    /** The FM Towns key an Android key types: the JIS main block by position, then the Towns keys. */
    fun fromAndroid(key: Int): Int? {
        JisMainBlock.fromAndroid(key)?.let { return it }
        if (key in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12) return pf[key - KeyEvent.KEYCODE_F1]
        if (key in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9) return numpad.getValue(key - KeyEvent.KEYCODE_NUMPAD_0)
        return when (key) {
            KeyEvent.KEYCODE_FORWARD_DEL -> DELETE
            KeyEvent.KEYCODE_INSERT -> INSERT
            KeyEvent.KEYCODE_NUMPAD_ENTER -> NUM_RETURN
            KeyEvent.KEYCODE_DPAD_UP -> UP
            KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
            KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
            KeyEvent.KEYCODE_MOVE_HOME -> HOME
            KeyEvent.KEYCODE_PAGE_UP -> PREV
            KeyEvent.KEYCODE_PAGE_DOWN -> NEXT
            KeyEvent.KEYCODE_BREAK -> BREAK
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> SHIFT
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> CTRL
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> ALT
            KeyEvent.KEYCODE_CAPS_LOCK -> CAPS
            KeyEvent.KEYCODE_KATAKANA_HIRAGANA -> KATAKANA
            KeyEvent.KEYCODE_HENKAN -> CONVERT
            KeyEvent.KEYCODE_MUHENKAN -> NO_CONVERT
            KeyEvent.KEYCODE_NUMPAD_MULTIPLY -> NUM_STAR
            KeyEvent.KEYCODE_NUMPAD_DIVIDE -> NUM_SLASH
            KeyEvent.KEYCODE_NUMPAD_ADD -> NUM_PLUS
            KeyEvent.KEYCODE_NUMPAD_SUBTRACT -> NUM_MINUS
            KeyEvent.KEYCODE_NUMPAD_EQUALS -> NUM_EQUAL
            KeyEvent.KEYCODE_NUMPAD_DOT -> NUM_DOT
            else -> null
        }
    }

    /** The gamepad's controls in the order of the core's pad bits (towns_machine.h PadButton). */
    val joystick = JoystickInputRouter.DEFAULT_CONTROLS + listOf("run", "select")
    val joystickLabels = listOf(FrontendR.string.kairo_up, FrontendR.string.kairo_down, FrontendR.string.kairo_left,
        FrontendR.string.kairo_right).map { UiText.res(it) } + listOf("A", "B", "RUN", "SELECT").map(UiText::fixed)

    val bindings = ControllerBindingsCodec({ it in bindableCodes }, joystick,
        setOf("menu", "pause", "restart", "exit", "fastForward"))

}
