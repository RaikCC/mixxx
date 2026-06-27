#pragma once

#include <QColor>

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"
#include "waveform/etanotecolors.h"

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

/// Preferences page for the ETA Notes feature (concept document section 9). It
/// drives the settings stored in WaveformWidgetFactory (which persists them to
/// the [EtaNotes] config group); allshader::WaveformRenderNotes reads them from
/// there each frame, so Apply takes effect immediately on the waveforms.
class DlgPrefEtaNotes : public DlgPreferencePage {
    Q_OBJECT

  public:
    DlgPrefEtaNotes(QWidget* pParent, UserSettingsPointer pConfig);
    ~DlgPrefEtaNotes() override = default;

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private:
    // One column per scheme color; index into m_colorButtons' second dimension.
    enum ColorRole {
        BgNormal = 0,
        BgContrast,
        FontNormal,
        FontContrast,
        NumColorRoles,
    };

    // Reflects the schemes/scalars currently shown by the widgets back into the
    // members, and the reverse, without touching the factory.
    void setSchemeOnButtons(EtaColorCase colorCase, const EtaNoteColorScheme& scheme);
    EtaNoteColorScheme schemeFromButtons(EtaColorCase colorCase) const;
    void setButtonColor(QPushButton* pButton, const QColor& color);
    QColor buttonColor(const QPushButton* pButton) const;
    void pickColor(QPushButton* pButton);
    // Grays out the rest of the page while the master switch is off.
    void updateControlsEnabled();

    UserSettingsPointer m_pConfig;

    QCheckBox* m_pEnabled{};
    // Holds every control except the master switch, so it can be grayed out as a
    // whole when the notes display is turned off.
    QWidget* m_pDependentControls{};
    QCheckBox* m_pShowBeats{};
    QCheckBox* m_pShowTime{};
    QCheckBox* m_pAlignRight{};
    QDoubleSpinBox* m_pFontSize{};
    QSpinBox* m_pWindowBeats{};
    QDoubleSpinBox* m_pNoteWidth{};
    QSpinBox* m_pAfterglowBeats{};
    QDoubleSpinBox* m_pAfterglowOpacity{};
    QPushButton* m_colorButtons[kNumEtaColorCases][NumColorRoles]{};
};
