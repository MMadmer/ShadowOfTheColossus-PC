// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: language of the port's own on-screen texts (overlay, F7/F8 messages): the launcher's,
// passed as SOTC_UI_LANG (pt, en, es, ru, zh, ja, ko; anything else is English). Texts are
// written as T(portuguese, english); the other languages are looked up by the Portuguese text in
// sotc_ui_lang.cpp (as launcher/sotc_i18n.py), and a text missing there shows in English.
#pragma once

#include <cstdlib>
#include <cstring>

namespace SotcUi {

enum class Lang { Pt, En, Es, Ru, Zh, Ja, Ko };

/// SOTC_UI_LANG, read once.
Lang Current();

/// The text in the current language. Format strings have the same printf specifiers, in the
/// same order, in every language.
const char* T(const char* pt, const char* en);

} // namespace SotcUi
