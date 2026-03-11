/*
===================================================
Created on: 13-11-2024
Author: Chang Xu
File: ControlUI.cpp
Version: 2.1
Language: C++ (Qt Framework)
Description:
This file implements the ControlUI class, which provides
a paged control surface for status, image tuning, capture,
network settings, and AI controls.
===================================================
*/

#include "ControlUI.h"
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>
#include <QVBoxLayout>

namespace {
QFrame *createCard(QWidget *parent, const QString &titleText) {
    auto *card = new QFrame(parent);
    card->setObjectName("Card");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 22, 22, 22);
    layout->setSpacing(16);

    auto *title = new QLabel(titleText, card);
    title->setObjectName("CardTitle");
    layout->addWidget(title);

    return card;
}
}

ControlUI::ControlUI(QWidget *parent)
    : QWidget(parent),
      fpsLabel(nullptr),
      performanceLabel(nullptr),
      pageButtonLayout(nullptr),
      pageStack(nullptr),
      brightnessSlider(nullptr),
      brightnessValueLabel(nullptr),
      gammaSlider(nullptr),
      gammaValueLabel(nullptr),
      sharpnessSlider(nullptr),
      sharpnessValueLabel(nullptr),
      denoiseSlider(nullptr),
      denoiseValueLabel(nullptr),
      horizontalFlip(nullptr),
      verticalFlip(nullptr),
      snapshotButton(nullptr),
      recordButton(nullptr),
      browseButton(nullptr),
      saveDirectoryLabel(nullptr),
      formatComboBox(nullptr),
      addressEdit(nullptr),
      portSpinBox(nullptr),
      receiverStatusLabel(nullptr),
      applyReceiverButton(nullptr),
      demoModeCheckBox(nullptr),
      demoStatusLabel(nullptr),
      aiEnableCheckBox(nullptr),
      aiStatusLabel(nullptr),
      isRecording(false),
      recordingTimer(nullptr) {
    setObjectName("ControlUI");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
        QWidget#ControlUI {
            background: #0f0f10;
            border: 1px solid #232325;
            border-radius: 28px;
        }
        QFrame#Card {
            background: #171719;
            border: 1px solid #26262a;
            border-radius: 20px;
        }
        QLabel {
            color: #ececed;
            border: none;
            background: transparent;
        }
        QLabel#Eyebrow {
            color: #8c8c93;
            font-size: 12px;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#PanelTitle {
            color: #fafafa;
            font-size: 30px;
            font-weight: 700;
        }
        QLabel#PanelSubtitle {
            color: #b0b0b6;
            font-size: 14px;
            line-height: 1.35;
        }
        QLabel#CardTitle {
            color: #f4f4f5;
            font-size: 21px;
            font-weight: 650;
        }
        QLabel#MetricValue {
            color: #ffffff;
            font-size: 48px;
            font-weight: 700;
        }
        QLabel#MetricUnit {
            color: #8a8a92;
            font-size: 18px;
            font-weight: 600;
        }
        QLabel#PerfText,
        QLabel#DirLabel,
        QLabel#StatusText {
            color: #a5a5ac;
            font-size: 15px;
            line-height: 1.35;
        }
        QLabel#ValuePill {
            background: #1f1f22;
            color: #f2f2f3;
            border: 1px solid #333338;
            border-radius: 13px;
            font-size: 15px;
            font-weight: 700;
            padding: 6px 12px;
        }
        QLabel#ControlLabel {
            color: #f2f2f4;
            font-size: 18px;
            font-weight: 600;
        }
        QLabel#HintLabel {
            color: #94949b;
            font-size: 15px;
        }
        QSlider::groove:horizontal {
            height: 10px;
            border-radius: 5px;
            background: #252529;
        }
        QSlider::sub-page:horizontal {
            background: #f0f0f2;
            border-radius: 5px;
        }
        QSlider::add-page:horizontal {
            background: #2a2a2f;
            border-radius: 5px;
        }
        QSlider::handle:horizontal {
            width: 22px;
            margin: -7px 0;
            border-radius: 11px;
            background: #ffffff;
            border: 2px solid #111113;
        }
        QCheckBox {
            color: #ededf0;
            spacing: 12px;
            font-size: 17px;
            font-weight: 600;
        }
        QCheckBox::indicator {
            width: 20px;
            height: 20px;
            border-radius: 6px;
            border: 1px solid #3a3a40;
            background: #111113;
        }
        QCheckBox::indicator:checked {
            background: #f1f1f3;
            border-color: #f1f1f3;
        }
        QPushButton {
            min-height: 54px;
            border-radius: 16px;
            border: 1px solid #303036;
            padding: 0 16px;
            font-size: 16px;
            font-weight: 700;
        }
        QPushButton#PrimaryButton {
            background: #f1f1f3;
            color: #111113;
            border-color: #f1f1f3;
        }
        QPushButton#PrimaryButton:hover {
            background: #ffffff;
        }
        QPushButton#SecondaryButton,
        QPushButton#RecordButton {
            background: #1d1d20;
            color: #f1f1f3;
        }
        QPushButton#SecondaryButton:hover,
        QPushButton#RecordButton:hover {
            background: #26262b;
        }
        QPushButton#RecordButton[recording="true"] {
            background: #d44d3f;
            border-color: #d44d3f;
            color: #ffffff;
        }
        QPushButton#RecordButton[recording="true"]:hover {
            background: #e15848;
        }
        QComboBox,
        QLineEdit,
        QSpinBox {
            min-height: 50px;
            border-radius: 14px;
            border: 1px solid #303036;
            padding: 0 14px;
            color: #f1f1f4;
            background: #1a1a1d;
            font-size: 16px;
        }
        QComboBox::drop-down,
        QSpinBox::down-button,
        QSpinBox::up-button {
            border: none;
            width: 28px;
        }
        QComboBox QAbstractItemView {
            background: #161618;
            color: #f1f1f4;
            border: 1px solid #303036;
            selection-background-color: #2a2a30;
        }
        QPushButton#PageButton {
            min-height: 46px;
            border-radius: 14px;
            background: #121214;
            color: #9f9fa7;
            border: 1px solid #26262a;
            font-size: 14px;
            font-weight: 700;
            text-align: center;
        }
        QPushButton#PageButton:hover {
            background: #1b1b1f;
            color: #f1f1f3;
        }
        QPushButton#PageButton[active="true"] {
            background: #f1f1f3;
            color: #111113;
            border-color: #f1f1f3;
        }
        QPushButton#PageButton[active="true"]:hover {
            background: #ffffff;
            color: #111113;
        }
        QWidget#PageSwitchBar {
            border-radius: 14px;
            background: transparent;
        }
    )");

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(18);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setAlignment(Qt::AlignTop);

    auto *eyebrowLabel = new QLabel("POST-TRAIN CONSOLE", this);
    eyebrowLabel->setObjectName("Eyebrow");
    layout->addWidget(eyebrowLabel);

    auto *titleLabel = new QLabel("Receiver Control Surface", this);
    titleLabel->setObjectName("PanelTitle");
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel("Live status stays pinned here. Tuning, capture, network, and AI controls move into dedicated pages.", this);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    auto *statusCard = createCard(this, "Stream Status");
    auto *statusLayout = qobject_cast<QVBoxLayout *>(statusCard->layout());

    auto *fpsRow = new QHBoxLayout();
    fpsRow->setContentsMargins(0, 0, 0, 0);
    fpsRow->setSpacing(8);
    fpsLabel = new QLabel("0", statusCard);
    fpsLabel->setObjectName("MetricValue");
    auto *fpsUnitLabel = new QLabel("FPS", statusCard);
    fpsUnitLabel->setObjectName("MetricUnit");
    fpsRow->addWidget(fpsLabel);
    fpsRow->addWidget(fpsUnitLabel);
    fpsRow->addStretch();
    statusLayout->addLayout(fpsRow);

    performanceLabel = new QLabel("Perf: waiting for frames", statusCard);
    performanceLabel->setObjectName("PerfText");
    performanceLabel->setWordWrap(true);
    statusLayout->addWidget(performanceLabel);
    layout->addWidget(statusCard);

    auto *pageCard = new QFrame(this);
    pageCard->setObjectName("Card");
    auto *pageCardLayout = new QVBoxLayout(pageCard);
    pageCardLayout->setContentsMargins(18, 18, 18, 18);
    pageCardLayout->setSpacing(16);

    auto *pageButtonBar = new QWidget(pageCard);
    pageButtonBar->setObjectName("PageSwitchBar");
    pageButtonLayout = new QGridLayout(pageButtonBar);
    pageButtonLayout->setContentsMargins(0, 0, 0, 0);
    pageButtonLayout->setHorizontalSpacing(10);
    pageButtonLayout->setVerticalSpacing(10);
    pageButtonLayout->addWidget(createPageButton("Image", 0), 0, 0);
    pageButtonLayout->addWidget(createPageButton("Capture", 1), 0, 1);
    pageButtonLayout->addWidget(createPageButton("Network", 2), 1, 0);
    pageButtonLayout->addWidget(createPageButton("AI", 3), 1, 1);
    pageCardLayout->addWidget(pageButtonBar);

    pageStack = new QStackedWidget(pageCard);
    pageStack->addWidget(createImagePage());
    pageStack->addWidget(createCapturePage());
    pageStack->addWidget(createNetworkPage());
    pageStack->addWidget(createAiPage());
    pageCardLayout->addWidget(pageStack, 1);

    layout->addWidget(pageCard, 1);
    setCurrentPage(0);
}

QWidget *ControlUI::createImagePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *tuningCard = createCard(page, "Image Tuning");
    auto *tuningLayout = qobject_cast<QVBoxLayout *>(tuningCard->layout());
    addSliderControl(tuningLayout, "Brightness", &brightnessValueLabel, &brightnessSlider, 0, 100, 50, SLOT(onBrightnessChanged(int)));
    addSliderControl(tuningLayout, "Gamma", &gammaValueLabel, &gammaSlider, -100, 100, 0, SLOT(onGammaChanged(int)));
    addSliderControl(tuningLayout, "Sharpness", &sharpnessValueLabel, &sharpnessSlider, 0, 100, 0, SLOT(onSharpnessChanged(int)));
    addSliderControl(tuningLayout, "Denoise", &denoiseValueLabel, &denoiseSlider, 0, 100, 0, SLOT(onDenoiseChanged(int)));
    layout->addWidget(tuningCard);

    auto *geometryCard = createCard(page, "Orientation");
    auto *geometryLayout = qobject_cast<QVBoxLayout *>(geometryCard->layout());
    geometryLayout->setSpacing(12);

    horizontalFlip = new QCheckBox("Mirror Horizontally", geometryCard);
    connect(horizontalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipHorizontalChanged);
    geometryLayout->addWidget(horizontalFlip);

    verticalFlip = new QCheckBox("Mirror Vertically", geometryCard);
    connect(verticalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipVerticalChanged);
    geometryLayout->addWidget(verticalFlip);

    auto *orientationHint = new QLabel("Applied locally to the displayed, captured, and recorded frame.", geometryCard);
    orientationHint->setObjectName("HintLabel");
    orientationHint->setWordWrap(true);
    geometryLayout->addWidget(orientationHint);

    layout->addWidget(geometryCard);
    layout->addStretch(1);
    return page;
}

QPushButton *ControlUI::createPageButton(const QString &text, int pageIndex) {
    auto *button = new QPushButton(text, this);
    button->setObjectName("PageButton");
    button->setCheckable(false);
    button->setProperty("active", false);
    connect(button, &QPushButton::clicked, this, [this, pageIndex]() { setCurrentPage(pageIndex); });
    pageButtons.append(button);
    return button;
}

QWidget *ControlUI::createCapturePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *captureCard = createCard(page, "Capture Output");
    auto *captureLayout = qobject_cast<QVBoxLayout *>(captureCard->layout());

    browseButton = new QPushButton("Choose Save Directory", captureCard);
    browseButton->setObjectName("SecondaryButton");
    connect(browseButton, &QPushButton::clicked, this, &ControlUI::onBrowseSaveDirectory);
    captureLayout->addWidget(browseButton);

    saveDirectoryLabel = new QLabel("Save Directory: Not Selected", captureCard);
    saveDirectoryLabel->setObjectName("DirLabel");
    saveDirectoryLabel->setWordWrap(true);
    captureLayout->addWidget(saveDirectoryLabel);

    auto *formatLabel = new QLabel("Video Format", captureCard);
    formatLabel->setObjectName("ControlLabel");
    captureLayout->addWidget(formatLabel);

    formatComboBox = new QComboBox(captureCard);
    formatComboBox->addItem("mp4");
    formatComboBox->addItem("avi");
    captureLayout->addWidget(formatComboBox);

    snapshotButton = new QPushButton("Take Snapshot", captureCard);
    snapshotButton->setObjectName("PrimaryButton");
    connect(snapshotButton, &QPushButton::clicked, this, &ControlUI::onTakeSnapshot);
    captureLayout->addWidget(snapshotButton);

    recordButton = new QPushButton("Start Recording", captureCard);
    recordButton->setObjectName("RecordButton");
    recordButton->setProperty("recording", false);
    connect(recordButton, &QPushButton::clicked, this, &ControlUI::onRecordVideo);
    captureLayout->addWidget(recordButton);

    auto *hint = new QLabel("Recording uses the processed display frame, including local image tuning and flip state.", captureCard);
    hint->setObjectName("HintLabel");
    hint->setWordWrap(true);
    captureLayout->addWidget(hint);

    layout->addWidget(captureCard);
    layout->addStretch(1);
    return page;
}

QWidget *ControlUI::createNetworkPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *networkCard = createCard(page, "Receiver Endpoint");
    auto *networkLayout = qobject_cast<QVBoxLayout *>(networkCard->layout());

    auto *addressLabel = new QLabel("Bind Address", networkCard);
    addressLabel->setObjectName("ControlLabel");
    networkLayout->addWidget(addressLabel);

    addressEdit = new QLineEdit("0.0.0.0", networkCard);
    addressEdit->setPlaceholderText("0.0.0.0");
    networkLayout->addWidget(addressEdit);

    auto *portLabel = new QLabel("Bind Port", networkCard);
    portLabel->setObjectName("ControlLabel");
    networkLayout->addWidget(portLabel);

    portSpinBox = new QSpinBox(networkCard);
    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(8080);
    networkLayout->addWidget(portSpinBox);

    applyReceiverButton = new QPushButton("Apply And Rebind", networkCard);
    applyReceiverButton->setObjectName("PrimaryButton");
    connect(applyReceiverButton, &QPushButton::clicked, this, &ControlUI::onApplyReceiverSettings);
    networkLayout->addWidget(applyReceiverButton);

    receiverStatusLabel = new QLabel("Receiver: waiting for bind status", networkCard);
    receiverStatusLabel->setObjectName("StatusText");
    receiverStatusLabel->setWordWrap(true);
    networkLayout->addWidget(receiverStatusLabel);

    auto *networkHint = new QLabel("Applying settings clears pending receiver-side data and rebinds the UDP socket without restarting the whole app.", networkCard);
    networkHint->setObjectName("HintLabel");
    networkHint->setWordWrap(true);
    networkLayout->addWidget(networkHint);

    auto *demoCard = createCard(page, "Local Demo");
    auto *demoLayout = qobject_cast<QVBoxLayout *>(demoCard->layout());

    demoModeCheckBox = new QCheckBox("Enable Built-in UDP Demo", demoCard);
    connect(demoModeCheckBox, &QCheckBox::toggled, this, &ControlUI::onDemoModeChanged);
    demoLayout->addWidget(demoModeCheckBox);

    demoStatusLabel = new QLabel("Demo is disabled.", demoCard);
    demoStatusLabel->setObjectName("StatusText");
    demoStatusLabel->setWordWrap(true);
    demoLayout->addWidget(demoStatusLabel);

    auto *demoHint = new QLabel("Use this to inject protocol-compatible local UDP traffic without restarting the app.", demoCard);
    demoHint->setObjectName("HintLabel");
    demoHint->setWordWrap(true);
    demoLayout->addWidget(demoHint);

    layout->addWidget(networkCard);
    layout->addWidget(demoCard);
    layout->addStretch(1);
    return page;
}

QWidget *ControlUI::createAiPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *aiCard = createCard(page, "AI Detection");
    auto *aiLayout = qobject_cast<QVBoxLayout *>(aiCard->layout());

    aiEnableCheckBox = new QCheckBox("Enable AI Detection", aiCard);
    connect(aiEnableCheckBox, &QCheckBox::toggled, this, &ControlUI::onAiDetectionChanged);
    aiLayout->addWidget(aiEnableCheckBox);

    aiStatusLabel = new QLabel("AI detection is disabled.", aiCard);
    aiStatusLabel->setObjectName("StatusText");
    aiStatusLabel->setWordWrap(true);
    aiLayout->addWidget(aiStatusLabel);

    auto *aiHint = new QLabel("This page manages the AI path separately so inference can stay explicitly opt-in and isolated from the receive hot path.", aiCard);
    aiHint->setObjectName("HintLabel");
    aiHint->setWordWrap(true);
    aiLayout->addWidget(aiHint);

    layout->addWidget(aiCard);
    layout->addStretch(1);
    return page;
}

void ControlUI::addSliderControl(QVBoxLayout *parentLayout,
                                 const QString &labelText,
                                 QLabel **valueLabelOut,
                                 QSlider **sliderOut,
                                 int minValue,
                                 int maxValue,
                                 int defaultValue,
                                 const char *slot) {
    auto *rowContainer = new QWidget(this);
    auto *rowLayout = new QVBoxLayout(rowContainer);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto *label = new QLabel(labelText, rowContainer);
    label->setObjectName("ControlLabel");

    auto *valueLabel = new QLabel(QString::number(defaultValue), rowContainer);
    valueLabel->setObjectName("ValuePill");
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setMinimumWidth(58);

    auto *slider = new QSlider(Qt::Horizontal, rowContainer);
    slider->setRange(minValue, maxValue);
    slider->setValue(defaultValue);
    connect(slider, SIGNAL(valueChanged(int)), this, slot);

    headerLayout->addWidget(label);
    headerLayout->addStretch();
    headerLayout->addWidget(valueLabel);
    rowLayout->addLayout(headerLayout);
    rowLayout->addWidget(slider);

    parentLayout->addWidget(rowContainer);
    *valueLabelOut = valueLabel;
    *sliderOut = slider;
}

void ControlUI::setCurrentPage(int pageIndex) {
    pageStack->setCurrentIndex(pageIndex);

    for (int i = 0; i < pageButtons.size(); ++i) {
        QPushButton *button = pageButtons[i];
        button->setProperty("active", i == pageIndex);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void ControlUI::onFPSChanged(int fps) {
    fpsLabel->setText(QString::number(fps));
}

void ControlUI::onPerformanceStatsChanged(const QString &statsText) {
    performanceLabel->setText(statsText);
}

void ControlUI::onReceiverStatusChanged(const QString &statusText) {
    receiverStatusLabel->setText(statusText);
}

void ControlUI::onReceiverSettingsChanged(const QString &address, quint16 port) {
    addressEdit->setText(address);
    portSpinBox->setValue(static_cast<int>(port));
}

void ControlUI::onAiStatusChanged(const QString &statusText) {
    aiStatusLabel->setText(statusText);
}

void ControlUI::onDemoStateChanged(bool enabled, const QString &statusText) {
    if (demoModeCheckBox->isChecked() != enabled) {
        demoModeCheckBox->blockSignals(true);
        demoModeCheckBox->setChecked(enabled);
        demoModeCheckBox->blockSignals(false);
    }
    demoStatusLabel->setText(statusText);
}

void ControlUI::onBrightnessChanged(int value) {
    brightnessValueLabel->setText(QString::number(value));
    emit brightnessChanged(value);
}

void ControlUI::onGammaChanged(int value) {
    gammaValueLabel->setText(QString::number(value));
    emit gammaChanged(value);
}

void ControlUI::onSharpnessChanged(int value) {
    sharpnessValueLabel->setText(QString::number(value));
    emit sharpnessChanged(value);
}

void ControlUI::onDenoiseChanged(int value) {
    denoiseValueLabel->setText(QString::number(value));
    emit denoiseChanged(value);
}

void ControlUI::onTakeSnapshot() {
    if (saveDirectory.isEmpty()) {
        QMessageBox::warning(this, "Save Directory Not Set", "Please select a save directory first.");
        return;
    }

    emit snapshotRequested(saveDirectory);
}

void ControlUI::onRecordVideo() {
    if (saveDirectory.isEmpty()) {
        QMessageBox::warning(this, "Save Directory Not Set", "Please select a save directory before recording.");
        return;
    }

    const QString format = formatComboBox->currentText();
    if (format != "mp4" && format != "avi") {
        QMessageBox::warning(this, "Unsupported Format", QString("Format %1 is not supported.").arg(format));
        return;
    }

    emit recordingRequested(saveDirectory, format);
}

void ControlUI::onRecordingStateChanged(bool recording) {
    const bool wasRecording = isRecording;
    isRecording = recording;
    recordButton->setProperty("recording", recording);
    recordButton->style()->unpolish(recordButton);
    recordButton->style()->polish(recordButton);

    if (recording) {
        recordButton->setText("Stop Recording");
        recordingElapsedTimer.start();

        if (!recordingTimer) {
            recordingTimer = new QTimer(this);
            connect(recordingTimer, &QTimer::timeout, this, [this]() {
                recordButton->setText(QString("Stop Recording (%1 s)").arg(recordingElapsedTimer.elapsed() / 1000));
            });
            recordingTimer->start(1000);
        }
    } else {
        recordButton->setText("Start Recording");

        if (recordingTimer) {
            recordingTimer->stop();
            delete recordingTimer;
            recordingTimer = nullptr;
        }

        if (!wasRecording) {
            QMessageBox::warning(this, "Recording Failed", "Failed to start or save the video. Please check the save directory and format.");
        }
    }
}

void ControlUI::onBrowseSaveDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          tr("Select Save Directory"),
                                                          "",
                                                          QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        saveDirectory = dir;
        saveDirectoryLabel->setText("Save Directory: " + saveDirectory);
    }
}

void ControlUI::onFlipHorizontalChanged(bool checked) {
    emit flipHorizontalRequested(checked);
}

void ControlUI::onFlipVerticalChanged(bool checked) {
    emit flipVerticalRequested(checked);
}

void ControlUI::onApplyReceiverSettings() {
    const QString address = addressEdit->text().trimmed();
    if (address.isEmpty()) {
        QMessageBox::warning(this, "Bind Address Required", "Please enter a bind address before applying receiver settings.");
        return;
    }

    receiverStatusLabel->setText(QString("Receiver: applying %1:%2 ...").arg(address).arg(portSpinBox->value()));
    emit receiverSettingsRequested(address, static_cast<quint16>(portSpinBox->value()));
}

void ControlUI::onAiDetectionChanged(bool checked) {
    emit aiDetectionToggled(checked);
}

void ControlUI::onDemoModeChanged(bool checked) {
    if (demoStatusLabel) {
        demoStatusLabel->setText(checked ? "Starting local demo traffic..." : "Stopping local demo traffic...");
    }
    emit demoModeRequested(checked);
}
