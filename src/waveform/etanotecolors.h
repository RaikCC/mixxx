#pragma once

#include <QColor>
#include <algorithm>
#include <array>

/// ETA Notes color model (concept document section 9). A note is drawn with a
/// background color and a font color; both come in a "normal" and a "contrast"
/// variant (the contrast variant is revealed from the right by the live-ETA
/// proximity indicator, section 7). One such scheme exists for each of five
/// cases: the deck's own notes, plus the four decks a transition note can refer
/// to (section 8). Phase 2d makes all five configurable; only the "own" scheme
/// is applied until the inter-deck display (phase 2e) is built.
struct EtaNoteColorScheme {
    QColor bgNormal;
    QColor bgContrast;
    QColor fontNormal;
    QColor fontContrast;

    bool operator==(const EtaNoteColorScheme& other) const {
        return bgNormal == other.bgNormal && bgContrast == other.bgContrast &&
                fontNormal == other.fontNormal &&
                fontContrast == other.fontContrast;
    }
    bool operator!=(const EtaNoteColorScheme& other) const {
        return !(*this == other);
    }
};

/// The five color cases, in storage/UI order: own notes first, then decks 1-4.
enum class EtaColorCase {
    Own = 0,
    Deck1,
    Deck2,
    Deck3,
    Deck4,
};
constexpr int kNumEtaColorCases = 5;

/// A readable text color on top of `background` (Rec. 601 luma: black on light,
/// white on dark). Used to derive default font colors.
inline QColor etaContrastingTextColor(const QColor& background) {
    const double luma = 0.299 * background.redF() +
            0.587 * background.greenF() + 0.114 * background.blueF();
    return luma > 0.5 ? QColor(Qt::black) : QColor(Qt::white);
}

/// A visibly different color for the proximity indicator: the complementary hue
/// (opposite on the color wheel), kept saturated and bright. Falls back to a
/// fixed accent for achromatic (gray) inputs that have no hue.
inline QColor etaComplementaryColor(const QColor& color) {
    int h = 0;
    int s = 0;
    int v = 0;
    color.getHsv(&h, &s, &v);
    if (h < 0) { // achromatic: no hue to complement
        return QColor(0, 153, 255);
    }
    QColor out;
    out.setHsv((h + 180) % 360, std::max(s, 180), std::max(v, 200));
    return out;
}

/// A full scheme derived from a single base background color: the base as the
/// normal background, its complement as the contrast background, and readable
/// font colors on each. Used for the built-in defaults (section 9).
inline EtaNoteColorScheme etaSchemeFromBase(const QColor& base) {
    EtaNoteColorScheme scheme;
    scheme.bgNormal = base;
    scheme.bgContrast = etaComplementaryColor(base);
    scheme.fontNormal = etaContrastingTextColor(scheme.bgNormal);
    scheme.fontContrast = etaContrastingTextColor(scheme.bgContrast);
    return scheme;
}

/// The default scheme for each case. "Own" keeps the historic amber so existing
/// installations look unchanged; decks 1-4 get distinct hues (only visible once
/// phase 2e shows inter-deck notes).
inline EtaNoteColorScheme etaDefaultColorScheme(EtaColorCase colorCase) {
    switch (colorCase) {
    case EtaColorCase::Own:
        return etaSchemeFromBase(QColor(255, 200, 0)); // amber
    case EtaColorCase::Deck1:
        return etaSchemeFromBase(QColor(0xC0, 0x39, 0x2B)); // red
    case EtaColorCase::Deck2:
        return etaSchemeFromBase(QColor(0x27, 0xAE, 0x60)); // green
    case EtaColorCase::Deck3:
        return etaSchemeFromBase(QColor(0x29, 0x80, 0xB9)); // blue
    case EtaColorCase::Deck4:
        return etaSchemeFromBase(QColor(0x8E, 0x44, 0xAD)); // purple
    }
    return etaSchemeFromBase(QColor(255, 200, 0));
}
