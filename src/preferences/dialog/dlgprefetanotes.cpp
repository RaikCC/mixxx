#include "preferences/dialog/dlgprefetanotes.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "moc_dlgprefetanotes.cpp"
#include "waveform/waveformwidgetfactory.h"

DlgPrefEtaNotes::DlgPrefEtaNotes(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent), m_pConfig(pConfig) {
    auto* pMainLayout = new QVBoxLayout(this);

    // Master visibility switch (concept section 9). Kept outside the dependent
    // container so it stays usable when everything else is grayed out.
    m_pEnabled = new QCheckBox(tr("Show ETA notes on the waveforms"), this);
    pMainLayout->addWidget(m_pEnabled);

    m_pDependentControls = new QWidget(this);
    auto* pDependentLayout = new QVBoxLayout(m_pDependentControls);
    pDependentLayout->setContentsMargins(0, 0, 0, 0);

    // --- behaviour / layout settings -----------------------------------------
    auto* pGeneralGroup = new QGroupBox(tr("Display"), m_pDependentControls);
    auto* pGeneralForm = new QFormLayout(pGeneralGroup);

    m_pShowBeats = new QCheckBox(tr("Show the countdown in beats"), pGeneralGroup);
    pGeneralForm->addRow(m_pShowBeats);
    m_pShowTime = new QCheckBox(tr("Show the countdown in time"), pGeneralGroup);
    pGeneralForm->addRow(m_pShowTime);
    m_pAlignRight = new QCheckBox(
            tr("Anchor the note's right edge at the play marker"), pGeneralGroup);
    m_pAlignRight->setToolTip(
            tr("Off: the note reaches into the upcoming waveform (right of the "
               "play marker).\nOn: the note sits in the past (left of it), keeping "
               "the upcoming waveform readable."));
    pGeneralForm->addRow(m_pAlignRight);

    m_pFontSize = new QDoubleSpinBox(pGeneralGroup);
    m_pFontSize->setRange(6.0, 30.0);
    m_pFontSize->setSingleStep(0.5);
    m_pFontSize->setDecimals(1);
    m_pFontSize->setSuffix(tr(" pt"));
    pGeneralForm->addRow(tr("Note font size:"), m_pFontSize);

    m_pWindowBeats = new QSpinBox(pGeneralGroup);
    m_pWindowBeats->setRange(1, 512);
    m_pWindowBeats->setSuffix(tr(" beats"));
    pGeneralForm->addRow(tr("Live-ETA preview window:"), m_pWindowBeats);

    m_pNoteWidth = new QDoubleSpinBox(pGeneralGroup);
    m_pNoteWidth->setRange(0.0, 2000.0);
    m_pNoteWidth->setSingleStep(10.0);
    m_pNoteWidth->setDecimals(0);
    m_pNoteWidth->setSuffix(tr(" px"));
    m_pNoteWidth->setToolTip(
            tr("Fixed bar width while playing. 0 fits the content, which disables "
               "the proximity-color indicator."));
    pGeneralForm->addRow(tr("Note width while playing:"), m_pNoteWidth);

    m_pAfterglowBeats = new QSpinBox(pGeneralGroup);
    m_pAfterglowBeats->setRange(0, 64);
    m_pAfterglowBeats->setSuffix(tr(" beats"));
    pGeneralForm->addRow(tr("Afterglow after passing:"), m_pAfterglowBeats);

    m_pAfterglowOpacity = new QDoubleSpinBox(pGeneralGroup);
    m_pAfterglowOpacity->setRange(0.0, 1.0);
    m_pAfterglowOpacity->setSingleStep(0.05);
    m_pAfterglowOpacity->setDecimals(2);
    pGeneralForm->addRow(tr("Afterglow opacity:"), m_pAfterglowOpacity);

    pDependentLayout->addWidget(pGeneralGroup);

    // --- color matrix: 5 cases x 4 roles (concept section 9) -----------------
    auto* pColorGroup = new QGroupBox(tr("Note colors"), m_pDependentControls);
    auto* pColorGrid = new QGridLayout(pColorGroup);
    pColorGrid->addWidget(new QLabel(tr("Background"), pColorGroup), 0, BgNormal + 1);
    pColorGrid->addWidget(
            new QLabel(tr("Background (contrast)"), pColorGroup), 0, BgContrast + 1);
    pColorGrid->addWidget(new QLabel(tr("Font"), pColorGroup), 0, FontNormal + 1);
    pColorGrid->addWidget(
            new QLabel(tr("Font (contrast)"), pColorGroup), 0, FontContrast + 1);

    for (int c = 0; c < kNumEtaColorCases; ++c) {
        const QString caseLabel =
                (c == 0) ? tr("Own notes") : tr("Deck %1").arg(c);
        pColorGrid->addWidget(new QLabel(caseLabel, pColorGroup), c + 1, 0);
        for (int r = 0; r < NumColorRoles; ++r) {
            auto* pButton = new QPushButton(pColorGroup);
            pButton->setFixedSize(40, 22);
            pButton->setCursor(Qt::PointingHandCursor);
            connect(pButton, &QPushButton::clicked, this, [this, pButton]() {
                pickColor(pButton);
            });
            m_colorButtons[c][r] = pButton;
            pColorGrid->addWidget(pButton, c + 1, r + 1);
        }
    }
    pColorGrid->setColumnStretch(NumColorRoles + 1, 1);
    pDependentLayout->addWidget(pColorGroup);

    pMainLayout->addWidget(m_pDependentControls);
    pMainLayout->addStretch(1);

    connect(m_pEnabled, &QCheckBox::toggled, this, [this]() {
        updateControlsEnabled();
    });

    setScrollSafeGuardForAllInputWidgets(this);
    slotUpdate();
}

void DlgPrefEtaNotes::slotUpdate() {
    auto* pFactory = WaveformWidgetFactory::instance();
    m_pEnabled->setChecked(pFactory->getEtaNotesEnabled());
    m_pShowBeats->setChecked(pFactory->getEtaShowBeats());
    m_pShowTime->setChecked(pFactory->getEtaShowTime());
    m_pAlignRight->setChecked(pFactory->getEtaAlignRightEdgeAtPlayhead());
    m_pFontSize->setValue(pFactory->getEtaFontPointSize());
    m_pWindowBeats->setValue(pFactory->getEtaWindowBeats());
    m_pNoteWidth->setValue(pFactory->getEtaNoteWidthPx());
    m_pAfterglowBeats->setValue(pFactory->getEtaAfterglowBeats());
    m_pAfterglowOpacity->setValue(pFactory->getEtaAfterglowOpacity());
    for (int c = 0; c < kNumEtaColorCases; ++c) {
        const auto colorCase = static_cast<EtaColorCase>(c);
        setSchemeOnButtons(colorCase, pFactory->getEtaColorScheme(colorCase));
    }
    updateControlsEnabled();
}

void DlgPrefEtaNotes::slotApply() {
    auto* pFactory = WaveformWidgetFactory::instance();
    pFactory->setEtaNotesEnabled(m_pEnabled->isChecked());
    pFactory->setEtaShowBeats(m_pShowBeats->isChecked());
    pFactory->setEtaShowTime(m_pShowTime->isChecked());
    pFactory->setEtaAlignRightEdgeAtPlayhead(m_pAlignRight->isChecked());
    pFactory->setEtaFontPointSize(m_pFontSize->value());
    pFactory->setEtaWindowBeats(m_pWindowBeats->value());
    pFactory->setEtaNoteWidthPx(m_pNoteWidth->value());
    pFactory->setEtaAfterglowBeats(m_pAfterglowBeats->value());
    pFactory->setEtaAfterglowOpacity(m_pAfterglowOpacity->value());
    for (int c = 0; c < kNumEtaColorCases; ++c) {
        const auto colorCase = static_cast<EtaColorCase>(c);
        pFactory->setEtaColorScheme(colorCase, schemeFromButtons(colorCase));
    }
}

void DlgPrefEtaNotes::slotResetToDefaults() {
    // Update the widgets to the built-in defaults; the user commits with Apply.
    m_pEnabled->setChecked(true);
    m_pShowBeats->setChecked(true);
    m_pShowTime->setChecked(false);
    m_pAlignRight->setChecked(false);
    m_pFontSize->setValue(10.0);
    m_pWindowBeats->setValue(32);
    m_pNoteWidth->setValue(240.0);
    m_pAfterglowBeats->setValue(6);
    m_pAfterglowOpacity->setValue(0.7);
    for (int c = 0; c < kNumEtaColorCases; ++c) {
        const auto colorCase = static_cast<EtaColorCase>(c);
        setSchemeOnButtons(colorCase, etaDefaultColorScheme(colorCase));
    }
    updateControlsEnabled();
}

void DlgPrefEtaNotes::setSchemeOnButtons(
        EtaColorCase colorCase, const EtaNoteColorScheme& scheme) {
    const int c = static_cast<int>(colorCase);
    setButtonColor(m_colorButtons[c][BgNormal], scheme.bgNormal);
    setButtonColor(m_colorButtons[c][BgContrast], scheme.bgContrast);
    setButtonColor(m_colorButtons[c][FontNormal], scheme.fontNormal);
    setButtonColor(m_colorButtons[c][FontContrast], scheme.fontContrast);
}

EtaNoteColorScheme DlgPrefEtaNotes::schemeFromButtons(EtaColorCase colorCase) const {
    const int c = static_cast<int>(colorCase);
    EtaNoteColorScheme scheme;
    scheme.bgNormal = buttonColor(m_colorButtons[c][BgNormal]);
    scheme.bgContrast = buttonColor(m_colorButtons[c][BgContrast]);
    scheme.fontNormal = buttonColor(m_colorButtons[c][FontNormal]);
    scheme.fontContrast = buttonColor(m_colorButtons[c][FontContrast]);
    return scheme;
}

void DlgPrefEtaNotes::setButtonColor(QPushButton* pButton, const QColor& color) {
    pButton->setProperty("etaColor", color);
    pButton->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; "
                           "border: 1px solid palette(mid); border-radius: 3px; }")
                    .arg(color.name()));
}

QColor DlgPrefEtaNotes::buttonColor(const QPushButton* pButton) const {
    return pButton->property("etaColor").value<QColor>();
}

void DlgPrefEtaNotes::pickColor(QPushButton* pButton) {
    const QColor chosen = QColorDialog::getColor(
            buttonColor(pButton), this, tr("Select ETA note color"));
    if (chosen.isValid()) {
        setButtonColor(pButton, chosen);
    }
}

void DlgPrefEtaNotes::updateControlsEnabled() {
    m_pDependentControls->setEnabled(m_pEnabled->isChecked());
}
