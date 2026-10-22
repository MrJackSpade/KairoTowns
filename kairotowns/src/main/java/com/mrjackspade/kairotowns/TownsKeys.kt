package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.ControllerBinding
import com.mrjackspade.kairo.frontend.ControllerBindingsCodec
import com.mrjackspade.kairo.frontend.ControllerLayout
import com.mrjackspade.kairo.frontend.GuestKeyboardLayout
import com.mrjackspade.kairo.frontend.GuestKeyboardPage
import com.mrjackspade.kairo.frontend.JisKana
import com.mrjackspade.kairo.frontend.JoystickInputRouter
import com.mrjackspade.kairo.frontend.KeyboardKey
import android.view.KeyEvent

/**
 * The FM Towns JIS keyboard as Tsugaru's key codes (TOWNS_JISKEY_*, towns/townsdef/townsdef.h),
 * and the gamepad in game port 0.
 */
object TownsKeys {
    const val ESC = 0x01
    const val MINUS = 0x0C
    const val HAT = 0x0D
    const val YEN = 0x0E
    const val BACKSPACE = 0x0F
    const val TAB = 0x10
    const val AT = 0x1B
    const val LEFT_BRACKET = 0x1C
    const val RETURN = 0x1D
    const val SEMICOLON = 0x27
    const val COLON = 0x28
    const val RIGHT_BRACKET = 0x29
    const val COMMA = 0x31
    const val DOT = 0x32
    const val SLASH = 0x33
    const val UNDERSCORE = 0x34
    const val SPACE = 0x35
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

    /** Digits 1 to 0, and the letter rows, by key position. */
    private const val DIGITS = "1234567890"
    private val letterRows = listOf("qwertyuiop" to 0x11, "asdfghjkl" to 0x1E, "zxcvbnm" to 0x2A)
    private val numpad = mapOf(7 to 0x3A, 8 to 0x3B, 9 to 0x3C, 4 to 0x3E, 5 to 0x3F, 6 to 0x40,
        1 to 0x42, 2 to 0x43, 3 to 0x44, 0 to 0x46)
    /** PF1 to PF20; the codes are not in order. */
    private val pf = listOf(0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x69, 0x5B,
        0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B)

    private fun digit(char: Char) = 0x02 + DIGITS.indexOf(char)
    private fun letter(char: Char): Int {
        val (row, base) = letterRows.first { char in it.first }
        return base + row.indexOf(char)
    }

    private fun key(char: Char, code: Int, shifted: String? = null, chordShift: Boolean = false) =
        JisKana.key(char, code, shifted = shifted, chordShift = chordShift)

    val layout = GuestKeyboardLayout(
        brand = "KAIROTOWNS",
        pages = listOf(
            GuestKeyboardPage("ABC", alphabetRows()),
            GuestKeyboardPage("?123", symbolRows(), pickerLabel = "Symbols"),
            GuestKeyboardPage("Towns", townsRows())
        ),
        modifiers = setOf(SHIFT, CTRL, ALT),
        shiftCodes = setOf(SHIFT),
        capsCode = null,
        chordShiftCode = SHIFT
    )

    // The alphabet page follows the FM Towns rows, so every kana legend has its key.
    private fun alphabetRows(): List<List<KeyboardKey>> = listOf(
        listOf(KeyboardKey("Esc", ESC, 1.3f)) +
            listOf("!", "\"", "#", "$", "%", "&", "'", "(", ")", null).mapIndexed { index, shifted ->
                key(DIGITS[index], digit(DIGITS[index]), shifted)
            } +
            listOf(key('-', MINUS, "="), key('^', HAT, "~"), key('¥', YEN, "|"),
                KeyboardKey("BS", BACKSPACE, 1.5f)),
        listOf(KeyboardKey("Tab", TAB, 1.3f)) + "qwertyuiop".map { key(it, letter(it)) } +
            listOf(key('@', AT, "`"), key('[', LEFT_BRACKET, "{"), KeyboardKey("Return", RETURN, 1.5f)),
        listOf(KeyboardKey("Ctrl", CTRL, 1.4f)) + "asdfghjkl".map { key(it, letter(it)) } +
            listOf(key(';', SEMICOLON, "+"), key(':', COLON, "*"), key(']', RIGHT_BRACKET, "}")),
        listOf(KeyboardKey("Shift", SHIFT, 1.5f)) + "zxcvbnm".map { key(it, letter(it)) } +
            listOf(key(',', COMMA, "<"), key('.', DOT, ">"), key('/', SLASH, "?"), key('_', UNDERSCORE),
                KeyboardKey("Shift", SHIFT, 1.5f)),
        listOf(KeyboardKey("Caps", CAPS), KeyboardKey("Alt", ALT), KeyboardKey("Kana", KATAKANA),
            KeyboardKey("Space", SPACE, 5f), KeyboardKey("Exec", EXECUTE), KeyboardKey("Cancel", CANCEL))
    )

    private fun symbolRows(): List<List<KeyboardKey>> = listOf(
        listOf(KeyboardKey("Esc", ESC, 1.3f)) + DIGITS.map { KeyboardKey("$it", digit(it)) } +
            KeyboardKey("BS", BACKSPACE, 1.5f),
        listOf("!", "\"", "#", "$", "%", "&", "'", "(", ")")
            .mapIndexed { index, label -> KeyboardKey(label, digit(DIGITS[index]), chordShift = true) } +
            KeyboardKey("Return", RETURN, 1.5f),
        listOf("-" to MINUS, "^" to HAT, "¥" to YEN, "@" to AT, "[" to LEFT_BRACKET, "]" to RIGHT_BRACKET,
            ";" to SEMICOLON, ":" to COLON, "," to COMMA, "." to DOT, "/" to SLASH)
            .map { (label, code) -> KeyboardKey(label, code) },
        listOf("=" to MINUS, "~" to HAT, "|" to YEN, "`" to AT, "{" to LEFT_BRACKET, "}" to RIGHT_BRACKET,
            "+" to SEMICOLON, "*" to COLON, "<" to COMMA, ">" to DOT, "?" to SLASH)
            .map { (label, code) -> KeyboardKey(label, code, chordShift = true) } +
            KeyboardKey("_", UNDERSCORE),
        listOf(KeyboardKey("Tab", TAB), KeyboardKey("Space", SPACE, 5f), KeyboardKey("Return", RETURN, 1.5f))
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

    fun label(code: Int): String? = when (code) {
        ESC -> "Esc"
        in 0x02..0x0B -> "${DIGITS[code - 0x02]}"
        MINUS -> "-"
        HAT -> "^"
        YEN -> "¥"
        BACKSPACE -> "BS"
        TAB -> "Tab"
        AT -> "@"
        LEFT_BRACKET -> "["
        RETURN -> "Return"
        SEMICOLON -> ";"
        COLON -> ":"
        RIGHT_BRACKET -> "]"
        COMMA -> ","
        DOT -> "."
        SLASH -> "/"
        UNDERSCORE -> "_"
        SPACE -> "Space"
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
        else -> letterRows.firstNotNullOfOrNull { (row, base) ->
            row.getOrNull(code - base)?.takeIf { code >= base }?.uppercaseChar()?.toString()
        }
    }

    /** The FM Towns key an Android key types: US punctuation by position on the JIS rows. */
    fun fromAndroid(key: Int): Int? {
        if (key in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z) return letter('a' + (key - KeyEvent.KEYCODE_A))
        if (key == KeyEvent.KEYCODE_0) return digit('0')
        if (key in KeyEvent.KEYCODE_1..KeyEvent.KEYCODE_9) return digit('1' + (key - KeyEvent.KEYCODE_1))
        if (key in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12) return pf[key - KeyEvent.KEYCODE_F1]
        if (key in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9) return numpad.getValue(key - KeyEvent.KEYCODE_NUMPAD_0)
        return when (key) {
            KeyEvent.KEYCODE_ESCAPE -> ESC
            KeyEvent.KEYCODE_DEL -> BACKSPACE
            KeyEvent.KEYCODE_FORWARD_DEL -> DELETE
            KeyEvent.KEYCODE_INSERT -> INSERT
            KeyEvent.KEYCODE_TAB -> TAB
            KeyEvent.KEYCODE_ENTER -> RETURN
            KeyEvent.KEYCODE_NUMPAD_ENTER -> NUM_RETURN
            KeyEvent.KEYCODE_SPACE -> SPACE
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
            KeyEvent.KEYCODE_MINUS -> MINUS
            KeyEvent.KEYCODE_EQUALS -> HAT
            KeyEvent.KEYCODE_BACKSLASH, KeyEvent.KEYCODE_YEN -> YEN
            KeyEvent.KEYCODE_LEFT_BRACKET -> AT
            KeyEvent.KEYCODE_RIGHT_BRACKET -> LEFT_BRACKET
            KeyEvent.KEYCODE_SEMICOLON -> SEMICOLON
            KeyEvent.KEYCODE_APOSTROPHE -> COLON
            KeyEvent.KEYCODE_COMMA -> COMMA
            KeyEvent.KEYCODE_PERIOD -> DOT
            KeyEvent.KEYCODE_SLASH -> SLASH
            KeyEvent.KEYCODE_RO -> UNDERSCORE
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
    val joystickLabels = listOf("Up", "Down", "Left", "Right", "A", "B", "RUN", "SELECT")

    val bindings = ControllerBindingsCodec(::defaults, { it in bindableCodes }, joystick,
        setOf("menu", "pause", "restart", "exit", "fastForward"))

    fun defaults(layout: ControllerLayout): List<ControllerBinding> =
        if (layout == ControllerLayout.WITH_STICKS) defaults()
        else defaults().filterNot { it.input.startsWith("virtual:ls") || it.input.startsWith("virtual:rs") }

    /**
     * FM Towns games read the gamepad in port 0: the D-pad and face buttons are the pad, the left
     * stick moves the mouse in port 1, and the right stick is the cursor keys.
     */
    fun defaults() = listOf(
        ControllerBinding("virtual:a", joystick = "button1"),
        ControllerBinding("virtual:b", joystick = "button2"),
        ControllerBinding("virtual:x", listOf(SPACE)),
        ControllerBinding("virtual:y", listOf(RETURN)),
        ControllerBinding("virtual:start", joystick = "run"),
        ControllerBinding("virtual:select", joystick = "select"),
        ControllerBinding("virtual:menu", action = "menu"),
        ControllerBinding("virtual:up", joystick = "up"),
        ControllerBinding("virtual:down", joystick = "down"),
        ControllerBinding("virtual:left", joystick = "left"),
        ControllerBinding("virtual:right", joystick = "right"),
        ControllerBinding("virtual:lsup", mouse = "moveUp"),
        ControllerBinding("virtual:lsdown", mouse = "moveDown"),
        ControllerBinding("virtual:lsleft", mouse = "moveLeft"),
        ControllerBinding("virtual:lsright", mouse = "moveRight"),
        ControllerBinding("virtual:rsup", listOf(UP)),
        ControllerBinding("virtual:rsdown", listOf(DOWN)),
        ControllerBinding("virtual:rsleft", listOf(LEFT)),
        ControllerBinding("virtual:rsright", listOf(RIGHT))
    )
}
