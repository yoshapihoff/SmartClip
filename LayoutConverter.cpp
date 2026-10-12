#include "LayoutConverter.h"

#include <QProcess>
#include <QFile>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDebug>
#include <QVarLengthArray>

#include <algorithm>

// ── Диагностика (включается SMARTCLIP_LAYOUT_DEBUG=1) ────────────────────
static bool layoutDebug()
{
    static const bool on = qEnvironmentVariableIsSet("SMARTCLIP_LAYOUT_DEBUG");
    return on;
}

// Составной ключ карты: (клавиша << 3) | уровень. Уровней максимум 8.
static inline quint64 mapKey(quint32 key, int level)
{
    return (quint64(key) << 3) | quint64(level & 0x7);
}

// Кэш построенных раскладок: список кодов + сами карты по коду.
namespace {

struct LayoutDb {
    QStringList codes;
    QHash<QString, LayoutConverter::Layout> byCode;
    bool built = false;
    bool xkbUsed = false;   // на Linux: удалось ли построить карты через xkbcommon
};

LayoutDb &db()
{
    static LayoutDb d;
    return d;
}

// ── Статический фолбэк us↔ru (используется, если карту собрать не удалось:
//    libxkbcommon не найдена на этапе сборки ИЛИ система не отдала раскладки).
//    Каждая пара (base, shift) соответствует одной физической клавише.
//    Намеренно используем только символы-формфакторы клавиш (без букв),
//    чтобы таблица читалась однозначно: ключи/значения сопоставляются по
//    позиции в строке.
struct StaticPair { const char *base; const char *shift; };

const StaticPair kUs[] = {
    {"`", "~"}, {"-", "_"}, {"=", "+"}, {"[", "{"}, {"]", "}"},
    {"\\", "|"}, {";", ":"}, {"'", "\""}, {",", "<"}, {".", ">"}, {"/", "?"},
};

const StaticPair kRu[] = {
    {"ё", "Ё"}, {"-", "_"}, {"=", "+"}, {"х", "Х"}, {"ъ", "Ъ"},
    {"\\", "/"}, {"ж", "Ж"}, {"э", "Э"}, {"б", "Б"}, {"ю", "Ю"}, {".", ","},
};

void addStaticPair(LayoutConverter::Layout &us, LayoutConverter::Layout &ru)
{
    auto addPair = [](LayoutConverter::Layout &lay, quint32 key,
                      const QString &base, const QString &shift) {
        if (!base.isEmpty() && base != QStringLiteral(" ")) {
            lay.keyToChar.insert(mapKey(key, 0), base);
            if (!lay.charToKey.contains(base))
                lay.charToKey.insert(base, {key, 0});
        }
        if (!shift.isEmpty() && shift != QStringLiteral(" ")) {
            lay.keyToChar.insert(mapKey(key, 1), shift);
            if (!lay.charToKey.contains(shift))
                lay.charToKey.insert(shift, {key, 1});
        }
    };

    // Символьные клавиши: индексы массивов соответствуют друг другу.
    const int n = qMin(int(sizeof(kUs) / sizeof(kUs[0])),
                       int(sizeof(kRu) / sizeof(kRu[0])));
    for (int i = 0; i < n; ++i) {
        const quint32 key = 200 + quint32(i);   // синтетические номера клавиш
        addPair(us, key, QString::fromUtf8(kUs[i].base), QString::fromUtf8(kUs[i].shift));
        addPair(ru, key, QString::fromUtf8(kRu[i].base), QString::fromUtf8(kRu[i].shift));
    }

    // Буквенные клавиши (порядок = физический ряд QWERTY / ЙЦУКЕН).
    const QString usLetters = QStringLiteral("qwertyuiopasdfghjklzxcvbnm");
    const QString ruLetters = QString::fromUtf8("йцукенгшщзфывапролдячсмить");
    const int ln = qMin(usLetters.size(), ruLetters.size());
    for (int i = 0; i < ln; ++i) {
        const quint32 key = 100 + quint32(i);
        const QString ul = usLetters.mid(i, 1);
        const QString rl = ruLetters.mid(i, 1);
        addPair(us, key, ul, ul.toUpper());
        addPair(ru, key, rl, rl.toUpper());
    }
}

} // namespace

// ════════════════════════════════════════════════════════════════════════
//  Linux / generic: список раскладок рабочего стола
// ════════════════════════════════════════════════════════════════════════
namespace {

QStringList layoutCodesFromGsettings()
{
    // GNOME/Cinnamon/MATE/Unity: gsettings get org.gnome.desktop.input-sources sources
    // → [('xkb', 'us'), ('xkb', 'ru')]
    if (!QStandardPaths::findExecutable(QStringLiteral("gsettings")).isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"),
                 QStringLiteral("org.gnome.desktop.input-sources"),
                 QStringLiteral("sources")});
        if (p.waitForFinished(1500) && p.exitStatus() == QProcess::NormalExit
            && p.exitCode() == 0) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            QRegularExpression re(QStringLiteral("\\('([^']+)'\\s*,\\s*'([^']+)'\\)"));
            QStringList codes;
            auto it = re.globalMatch(out);
            while (it.hasNext()) {
                const auto m = it.next();
                if (m.captured(1) == QStringLiteral("xkb"))
                    codes << m.captured(2);
            }
            if (!codes.isEmpty())
                return codes;
        }
    }
    return {};
}

QStringList layoutCodesFromKde()
{
    // KDE Plasma: ~/.config/kxkbrc → LayoutList=us,ru
    const QString cfg = QStandardPaths::writableLocation(
                            QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/kxkbrc");
    if (QFile::exists(cfg)) {
        QSettings s(cfg, QSettings::IniFormat);
        const QString list = s.value(QStringLiteral("Layout/LayoutList")).toString();
        if (!list.trimmed().isEmpty()) {
            QStringList codes;
            for (const QString &part : list.split(QLatin1Char(','),
                                                  Qt::SkipEmptyParts))
                codes << part.trimmed();
            if (!codes.isEmpty())
                return codes;
        }
    }
    return {};
}

QStringList layoutCodesFromSetxkbmap()
{
    // Общий X11-путь (i3, openbox, XFCE без GNOME-настроек и т.п.).
    if (!QStandardPaths::findExecutable(QStringLiteral("setxkbmap")).isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("setxkbmap"), {QStringLiteral("-query")});
        if (p.waitForFinished(1500) && p.exitStatus() == QProcess::NormalExit
            && p.exitCode() == 0) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            QRegularExpression re(QStringLiteral("layout:\\s*(.+)"));
            const auto m = re.match(out);
            if (m.hasMatch()) {
                QStringList codes;
                for (const QString &part : m.captured(1).trimmed().split(
                         QLatin1Char(','), Qt::SkipEmptyParts))
                    codes << part.trimmed();
                if (!codes.isEmpty())
                    return codes;
            }
        }
    }
    return {};
}

#if defined(SMARTCLIP_HAVE_XKB)
#include <xkbcommon/xkbcommon.h>

// Построить карту «клавиша+уровень → символ» для одной раскладки через
// libxkbcommon (те же правила/модель/опции, что у системы; layout — явный).
// Это ровно то, что делает `xkbcli`, но без внешнего бинаря — библиотека и
// так есть у любой системы с Qt.
bool buildLayoutWithXkb(const QString &code, LayoutConverter::Layout &out)
{
    const QByteArray layout = code.toUtf8();
    const QByteArray rules = qgetenv("XKB_DEFAULT_RULES");
    const QByteArray model = qgetenv("XKB_DEFAULT_MODEL");
    const QByteArray variant = qgetenv("XKB_DEFAULT_VARIANT");
    const QByteArray options = qgetenv("XKB_DEFAULT_OPTIONS");

    xkb_rule_names names{};
    names.rules = rules.isEmpty() ? nullptr : rules.constData();
    names.model = model.isEmpty() ? nullptr : model.constData();
    names.layout = layout.constData();
    names.variant = variant.isEmpty() ? nullptr : variant.constData();
    names.options = options.isEmpty() ? nullptr : options.constData();

    xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx)
        return false;
    xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names,
                                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        xkb_context_unref(ctx);
        return false;
    }
    xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        xkb_context_unref(ctx);
        return false;
    }

    // Перебираем «обычный» диапазон клавиш главного блока (как это делает
    // xkbcli). Уровни получаем, явно нажимая Shift в состоянии: так корректно
    // собираются и верхний регистр букв, и символы верхнего уровня клавиш.
    for (int level = 0; level < 2; ++level) {
        if (level == 1)
            xkb_state_update_mask(state, 1u << 0, 0, 0, 0, 0, 0);
        for (xkb_keycode_t key = 10; key <= 69; ++key) {
            char buf[32];
            // Длина без завершающего '\0' (см. docs xkbcommon).
            const int len = xkb_state_key_get_utf8(state, key, buf, sizeof(buf));
            if (len <= 0)
                continue;   // нет символа на этой клавише
            const QString ch = QString::fromUtf8(buf, len);
            if (ch.isEmpty() || ch == QStringLiteral(" "))
                continue;
            out.keyToChar.insert(mapKey(quint32(key), level), ch);
            if (!out.charToKey.contains(ch))
                out.charToKey.insert(ch, {quint32(key), level});
        }
        // Вернуть состояние base перед следующей итерацией.
        if (level == 1)
            xkb_state_update_mask(state, 0, 0, 0, 0, 0, 0);
    }

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    return !out.keyToChar.isEmpty();
}
#endif // SMARTCLIP_HAVE_XKB

// ════════════════════════════════════════════════════════════════════════
//  macOS: список раскладок + карты через TIS / UCKeyTranslate
// ════════════════════════════════════════════════════════════════════════
#if defined(Q_OS_MAC)
#include <Carbon/Carbon.h>

QString macSourceCode(TISInputSourceRef src)
{
    // Пытаемся получить осмысленный короткий код из языков источника.
    auto langsRef = (CFArrayRef)TISGetInputSourceProperty(
        src, kTISPropertyInputSourceLanguages);
    QString base;
    if (langsRef && CFArrayGetCount(langsRef) > 0) {
        auto lang = (CFStringRef)CFArrayGetValueAtIndex(langsRef, 0);
        char buf[64] = {0};
        if (CFStringGetCString(lang, buf, sizeof(buf), kCFStringEncodingUTF8))
            base = QString::fromUtf8(buf).section(QLatin1Char('-'), 0, 0);
    }
    if (base.isEmpty())
        base = QStringLiteral("??");
    // «en» → «us»: так код совпадает с привычным обозначением раскладки.
    if (base == QStringLiteral("en"))
        base = QStringLiteral("us");
    return base;
}

bool buildLayoutWithTis(TISInputSourceRef src, LayoutConverter::Layout &out)
{
    auto dataRef = (CFDataRef)TISGetInputSourceProperty(
        src, kTISPropertyUnicodeKeyLayoutData);
    if (!dataRef)
        return false;
    const auto *layout = (const UCKeyboardLayout *)CFDataGetBytePtr(dataRef);
    if (!layout)
        return false;

    // Виртуальные коды клавиш macOS (основной блок). Они одинаковы для любой
    // раскладки — меняется лишь результирующий символ.
    static const UInt16 vks[] = {
        kVK_ANSI_Grave,   kVK_ANSI_Minus,  kVK_ANSI_Equal,  kVK_ANSI_LeftBracket,
        kVK_ANSI_RightBracket, kVK_ANSI_Backslash, kVK_ANSI_Semicolon,
        kVK_ANSI_Quote,   kVK_ANSI_Comma,  kVK_ANSI_Period, kVK_ANSI_Slash,
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E,
        kVK_ANSI_F, kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J,
        kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O,
        kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T,
        kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y,
        kVK_ANSI_Z,
    };

    for (UInt16 vk : vks) {
        for (int level = 0; level < 2; ++level) {
            const UInt32 modifiers = (level == 0) ? 0 : shiftKey;
            UInt32 deadKeyState = 0;
            UniChar chars[8];
            UniCharCount len = 0;
            const OSStatus st = UCKeyTranslate(
                layout, vk, kUCKeyActionDown,
                (modifiers >> 8) & 0xFF, LMGetKbdType(),
                kUCKeyTranslateNoDeadKeysMask, &deadKeyState,
                sizeof(chars) / sizeof(chars[0]), &len, chars);
            if (st != noErr || len == 0)
                continue;
            const QString ch = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(chars), len);
            if (ch.isEmpty() || ch == QStringLiteral(" "))
                continue;
            out.keyToChar.insert(mapKey(quint32(vk), level), ch);
            if (!out.charToKey.contains(ch))
                out.charToKey.insert(ch, {quint32(vk), level});
        }
    }
    return !out.keyToChar.isEmpty();
}
#endif // Q_OS_MAC

// ════════════════════════════════════════════════════════════════════════
//  Windows: список активных раскладок + карты через ToUnicodeEx
// ════════════════════════════════════════════════════════════════════════
#if defined(Q_OS_WIN)
#include <windows.h>

// Короткий код раскладки по HKL: ISO-639-код языка («ru», «de», …).
// Латинскую «en» приводим к «us» — так же, как на macOS, чтобы код совпадал
// с привычным обозначением раскладки. Уникализацию дублей делает вызывающий.
QString winLayoutCode(HKL hkl)
{
    const LANGID lang = LOWORD(reinterpret_cast<quintptr>(hkl));
    wchar_t buf[16] = {0};
    QString base;
    if (GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_SISO639LANGNAME,
                       buf, 16) > 0)
        base = QString::fromWCharArray(buf).toLower();
    if (base.isEmpty())
        base = QStringLiteral("??");
    if (base == QStringLiteral("en"))
        base = QStringLiteral("us");
    return base;
}

// Построить карту «клавиша+уровень → символ» для раскладки HKL через
// ToUnicodeEx (ровно то, что видит пользователь при нажатии клавиш).
// VK-код = физическая клавиша (аналог keycode на Linux).
// Флаг 1<<2 — не изменять состояние клавиатуры (чтобы dead keys не «залипали»).
bool buildLayoutWithWin(HKL hkl, LayoutConverter::Layout &out)
{
    static const UINT vks[] = {
        VK_OEM_3, VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_4, VK_OEM_6, VK_OEM_5,
        VK_OEM_1, VK_OEM_7, VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2,
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    };

    for (UINT vk : vks) {
        const UINT sc = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, hkl);
        for (int level = 0; level < 2; ++level) {
            BYTE keystate[256] = {0};
            if (level == 1)
                keystate[VK_SHIFT] = 0x80;
            wchar_t buf[8] = {0};
            const int len =
                ToUnicodeEx(vk, sc, keystate, buf, 8, 1u << 2, hkl);
            if (len <= 0)
                continue;
            const QString ch = QString::fromWCharArray(buf, len);
            if (ch.isEmpty() || ch == QStringLiteral(" "))
                continue;
            out.keyToChar.insert(mapKey(quint32(vk), level), ch);
            if (!out.charToKey.contains(ch))
                out.charToKey.insert(ch, {quint32(vk), level});
        }
    }
    return !out.keyToChar.isEmpty();
}
#endif // Q_OS_WIN

} // namespace

// ════════════════════════════════════════════════════════════════════════
//  Публичный интерфейс
// ════════════════════════════════════════════════════════════════════════

QStringList LayoutConverter::availableLayouts()
{
    LayoutDb &d = db();
    if (d.built)
        return d.codes;

#if defined(Q_OS_MAC)
    // ── macOS ──────────────────────────────────────────────────────────
    QStringList codes;
    // false — только ВКЛЮЧЁННЫЕ источники (то, что пользователь реально
    // переключает); отключённые раскладки в цикл не попадают.
    CFArrayRef list = TISCreateInputSourceList(nullptr, false);
    if (list) {
        const CFIndex n = CFArrayGetCount(list);
        for (CFIndex i = 0; i < n; ++i) {
            auto src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, i);
            auto type = (CFStringRef)TISGetInputSourceProperty(
                src, kTISPropertyInputSourceType);
            if (!type || CFStringCompare(type, kTISTypeKeyboardLayout, 0)
                             != kCFCompareEqualTo)
                continue;
            auto selectable = (CFBooleanRef)TISGetInputSourceProperty(
                src, kTISPropertyInputSourceIsSelectCapable);
            if (!selectable || !CFBooleanGetValue(selectable))
                continue;

            LayoutConverter::Layout lay;
            if (!buildLayoutWithTis(src, lay))
                continue;   // источник без данных раскладки (напр. IME)
            QString code = macSourceCode(src);
            while (codes.contains(code))   // уникализируем коды
                code += QLatin1Char('+');
            codes << code;
            d.byCode.insert(code, lay);
        }
        CFRelease(list);
    }
    if (codes.isEmpty())
        codes = {QStringLiteral("us"), QStringLiteral("ru")};
#elif defined(Q_OS_WIN)
    // ── Windows ─────────────────────────────────────────────
    // GetKeyboardLayoutList отдаёт раскладки, загруженные в сессии пользователя
    // (те, что реально переключаются в трее). Строим карты через ToUnicodeEx.
    QStringList codes;
    const int n = GetKeyboardLayoutList(0, nullptr);
    if (n > 0) {
        QVarLengthArray<HKL, 32> hkls(n);
        const int got = GetKeyboardLayoutList(n, hkls.data());
        for (int i = 0; i < got; ++i) {
            LayoutConverter::Layout lay;
            if (!buildLayoutWithWin(hkls[i], lay))
                continue;   // раскладка без данных (напр. IME)
            QString code = winLayoutCode(hkls[i]);
            while (codes.contains(code))   // уникализируем коды
                code += QLatin1Char('+');
            codes << code;
            d.byCode.insert(code, lay);
        }
    }
    if (codes.isEmpty())
        codes = {QStringLiteral("us"), QStringLiteral("ru")};
#else
    // ── Linux / прочие Unix ────────────────────────────────────────────
    QStringList codes = layoutCodesFromGsettings();
    if (codes.isEmpty())
        codes = layoutCodesFromKde();
    if (codes.isEmpty())
        codes = layoutCodesFromSetxkbmap();
    if (codes.isEmpty())
        codes = {QStringLiteral("us"), QStringLiteral("ru")};   // безопасный дефолт

    bool xkbUsed = false;
    for (const QString &code : codes) {
        LayoutConverter::Layout lay;
#if defined(SMARTCLIP_HAVE_XKB)
        if (buildLayoutWithXkb(code, lay))
            xkbUsed = true;
#endif
        d.byCode.insert(code, lay);   // возможно пустая — заполним статикой ниже
    }

    // Если через xkbcommon ничего собрать не удалось — заполняем статическим
    // фолбэком (us/ru), чтобы функция всё равно работала.
    if (!xkbUsed) {
        LayoutConverter::Layout us, ru;
        addStaticPair(us, ru);
        for (const QString &code : codes) {
            if (code == QStringLiteral("us")
                && d.byCode.value(code).keyToChar.isEmpty())
                d.byCode.insert(code, us);
            else if (code == QStringLiteral("ru")
                     && d.byCode.value(code).keyToChar.isEmpty())
                d.byCode.insert(code, ru);
        }
    }
    d.xkbUsed = xkbUsed;
#endif

    // Если конкретные раскладки так и остались без карт — даём им статику
    // только для us/ru (остальные останутся пустыми и просто не сработают).
    for (const QString &code : codes) {
        if (!d.byCode.value(code).keyToChar.isEmpty())
            continue;
        if (code == QStringLiteral("us") || code == QStringLiteral("ru")) {
            LayoutConverter::Layout us, ru;
            addStaticPair(us, ru);
            d.byCode.insert(code, code == QStringLiteral("us") ? us : ru);
        }
    }

    if (codes.isEmpty())
        codes = {QStringLiteral("us"), QStringLiteral("ru")};

    d.codes = codes;
    d.built = true;

    if (layoutDebug()) {
        QStringList withMaps;
        for (const QString &c : codes) {
            if (!d.byCode.value(c).keyToChar.isEmpty())
                withMaps << c;
        }
        qInfo() << "LayoutConverter: раскладки =" << codes
#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
                << "xkb =" << d.xkbUsed
#endif
                << "с картами =" << withMaps;
    }
    return d.codes;
}

bool LayoutConverter::available()
{
    return availableLayouts().size() >= 2;
}

QString LayoutConverter::convert(const QString &text, const QString &from,
                                 const QString &to)
{
    availableLayouts();   // гарантируем, что кэш построен
    LayoutDb &d = db();
    if (!d.byCode.contains(from) || !d.byCode.contains(to))
        return {};
    const Layout &src = d.byCode.value(from);
    const Layout &dst = d.byCode.value(to);
    if (src.charToKey.isEmpty() || dst.keyToChar.isEmpty())
        return {};

    QString out;
    out.reserve(text.size());
    int converted = 0;
    for (const QChar &ch : text) {
        const QString s(ch);
        // Символ, который можно набрать в ЦЕЛЕВОЙ раскладке, считаем «уже в
        // нужной раскладке» и не трогаем: подстроки, уже набранные в целевую
        // раскладку (в середине/конце текста), остаются как были. Сюда же
        // попадают символы, общие для обеих раскладок (напр. '-', '.', ','):
        // они остаются собой в ОБЕ стороны, поэтому цикл переключений не ломается.
        if (dst.charToKey.contains(s)) {
            out += s;
            continue;
        }
        auto it = src.charToKey.constFind(s);
        if (it == src.charToKey.constEnd()) {
            out += s;   // символа нет в исходной раскладке — как есть
            continue;
        }
        auto jt = dst.keyToChar.constFind(mapKey(it.value().key, it.value().level));
        if (jt == dst.keyToChar.constEnd()) {
            out += s;   // в целевой раскладке на этой позиции ничего нет
            continue;
        }
        // s ∉ dst.charToKey, а jt — символ целевой раскладки, значит jt != s.
        ++converted;
        out += jt.value();
    }
    return converted > 0 ? out : QString();
}

QString LayoutConverter::nextLayout(const QString &current)
{
    const QStringList codes = availableLayouts();
    if (codes.isEmpty())
        return {};
    const int idx = codes.indexOf(current);
    if (idx < 0)
        return codes.first();
    return codes.at((idx + 1) % codes.size());
}

QString LayoutConverter::guessLayout(const QString &text)
{
    const QStringList codes = availableLayouts();
    LayoutDb &d = db();
    QString best;
    int bestScore = 0;
    for (const QString &code : codes) {
        const Layout &lay = d.byCode.value(code);
        if (lay.charToKey.isEmpty())
            continue;
        int score = 0;
        for (const QChar &ch : text) {
            if (lay.charToKey.contains(QString(ch)))
                ++score;
        }
        if (score > bestScore) {
            bestScore = score;
            best = code;
        }
    }
    return best;
}
