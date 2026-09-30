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
      sourceModeComboBox(nullptr),
      udpSettingsContainer(nullptr),
      usbSettingsContainer(nullptr),
      addressEdit(nullptr),
      portSpinBox(nullptr),
      npcapModeCheckBox(nullptr),
      npcapInterfaceEdit(nullptr),
      usbDeviceMatchEdit(nullptr),
      usbPipeSpinBox(nullptr),
      usbTransferSpinBox(nullptr),
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
            min-height: 42px;
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
    pageCardLayout->setSpacing(14);

    auto *pageButtonBar = new QWidget(pageCard);
    pageButtonBar->setObjectName("PageSwitchBar");
    pageButtonLayout = new QGridLayout(pageButtonBar);
    pageButtonLayout->setContentsMargins(0, 0, 0, 0);
    pageButtonLayout->setHorizontalSpacing(8);
    pageButtonLayout->setVerticalSpacing(8);
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
    layout->setSpacing(12);
    networkUdpWidgets.clear();
    networkUsbWidgets.clear();

    auto *networkCard = createCard(page, "Receiver Setup");
    auto *networkLayout = qobject_cast<QVBoxLayout *>(networkCard->layout());
    networkLayout->setSpacing(12);

    auto *routingHint = new QLabel("Select one active input source. Npcap remains an optional diagnostic capture assist for the UDP path only.", networkCard);
    routingHint->setObjectName("HintLabel");
    routingHint->setWordWrap(true);
    networkLayout->addWidget(routingHint);

    auto *sourceLabel = new QLabel("Input Source", networkCard);
    sourceLabel->setObjectName("ControlLabel");
    networkLayout->addWidget(sourceLabel);

    sourceModeComboBox = new QComboBox(networkCard);
    sourceModeComboBox->addItem("UDP Socket", 0);
    sourceModeComboBox->addItem("FT601 USB", 2);
    connect(sourceModeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onSourceModeChanged(int)));
    networkLayout->addWidget(sourceModeComboBox);

    udpSettingsContainer = new QWidget(networkCard);
    auto *udpLayout = new QVBoxLayout(udpSettingsContainer);
    udpLayout->setContentsMargins(0, 0, 0, 0);
    udpLayout->setSpacing(12);
    networkLayout->addWidget(udpSettingsContainer);
    networkUdpWidgets.append(udpSettingsContainer);

    auto *udpSectionTitle = new QLabel("UDP Options", udpSettingsContainer);
    udpSectionTitle->setObjectName("ControlLabel");
    udpLayout->addWidget(udpSectionTitle);
    networkUdpWidgets.append(udpSectionTitle);

    addressEdit = new QLineEdit("0.0.0.0", udpSettingsContainer);
    addressEdit->setPlaceholderText("0.0.0.0");
    addressEdit->setMinimumHeight(46);

    auto *addressLabel = new QLabel("Bind Address", udpSettingsContainer);
    addressLabel->setObjectName("ControlLabel");
    udpLayout->addWidget(addressLabel);
    udpLayout->addWidget(addressEdit);
    networkUdpWidgets.append(addressLabel);
    networkUdpWidgets.append(addressEdit);

    portSpinBox = new QSpinBox(udpSettingsContainer);
    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(8080);
    portSpinBox->setMinimumHeight(46);

    auto *portRow = new QHBoxLayout();
    portRow->setContentsMargins(0, 0, 0, 0);
    portRow->setSpacing(12);

    auto *portColumn = new QVBoxLayout();
    portColumn->setContentsMargins(0, 0, 0, 0);
    portColumn->setSpacing(8);

    auto *portLabel = new QLabel("Bind Port", udpSettingsContainer);
    portLabel->setObjectName("ControlLabel");
    portColumn->addWidget(portLabel);
    portColumn->addWidget(portSpinBox);
    portRow->addLayout(portColumn);
    networkUdpWidgets.append(portLabel);
    networkUdpWidgets.append(portSpinBox);
    portRow->addStretch(1);
    udpLayout->addLayout(portRow);

    npcapModeCheckBox = new QCheckBox("Enable Npcap Diagnostic Capture", udpSettingsContainer);
    connect(npcapModeCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onSourceModeChanged(sourceModeComboBox ? sourceModeComboBox->currentIndex() : 0);
    });
    udpLayout->addWidget(npcapModeCheckBox);
    networkUdpWidgets.append(npcapModeCheckBox);

    auto *npcapLabel = new QLabel("Npcap Interface", udpSettingsContainer);
    npcapLabel->setObjectName("ControlLabel");
    udpLayout->addWidget(npcapLabel);
    networkUdpWidgets.append(npcapLabel);

    npcapInterfaceEdit = new QLineEdit(QString::fromUtf8("以太网 4"), udpSettingsContainer);
    npcapInterfaceEdit->setPlaceholderText(QString::fromUtf8("以太网 4"));
    npcapInterfaceEdit->setMinimumHeight(46);
    udpLayout->addWidget(npcapInterfaceEdit);

    auto *npcapHint = new QLabel("Use this when the FPGA stream is only visible in promiscuous or capture mode. The bind address becomes informational, while the UDP destination port filter still applies.", udpSettingsContainer);
    npcapHint->setObjectName("HintLabel");
    npcapHint->setWordWrap(true);
    udpLayout->addWidget(npcapHint);

    networkUdpWidgets.append(npcapInterfaceEdit);
    networkUdpWidgets.append(npcapHint);

    usbSettingsContainer = new QWidget(networkCard);
    auto *usbLayout = new QVBoxLayout(usbSettingsContainer);
    usbLayout->setContentsMargins(0, 0, 0, 0);
    usbLayout->setSpacing(12);
    networkLayout->addWidget(usbSettingsContainer);
    networkUsbWidgets.append(usbSettingsContainer);

    auto *usbSectionTitle = new QLabel("FT601 USB Options", usbSettingsContainer);
    usbSectionTitle->setObjectName("ControlLabel");
    usbLayout->addWidget(usbSectionTitle);
    networkUsbWidgets.append(usbSectionTitle);

    auto *usbLabel = new QLabel("FT601 Device Match", usbSettingsContainer);
    usbLabel->setObjectName("ControlLabel");
    usbLayout->addWidget(usbLabel);

    usbDeviceMatchEdit = new QLineEdit("FT601", usbSettingsContainer);
    usbDeviceMatchEdit->setPlaceholderText("FT601");
    usbDeviceMatchEdit->setMinimumHeight(46);
    usbLayout->addWidget(usbDeviceMatchEdit);
    networkUsbWidgets.append(usbLabel);
    networkUsbWidgets.append(usbDeviceMatchEdit);

    auto *usbRow = new QHBoxLayout();
    usbRow->setContentsMargins(0, 0, 0, 0);
    usbRow->setSpacing(12);

    auto *usbPipeColumn = new QVBoxLayout();
    usbPipeColumn->setContentsMargins(0, 0, 0, 0);
    usbPipeColumn->setSpacing(8);
    auto *usbPipeLabel = new QLabel("FT601 Pipe", usbSettingsContainer);
    usbPipeLabel->setObjectName("ControlLabel");
    usbPipeSpinBox = new QSpinBox(usbSettingsContainer);
    usbPipeSpinBox->setRange(0x80, 0x8F);
    usbPipeSpinBox->setDisplayIntegerBase(16);
    usbPipeSpinBox->setPrefix("0x");
    usbPipeSpinBox->setValue(0x82);
    usbPipeSpinBox->setMinimumHeight(46);
    usbPipeColumn->addWidget(usbPipeLabel);
    usbPipeColumn->addWidget(usbPipeSpinBox);
    usbRow->addLayout(usbPipeColumn, 1);
    networkUsbWidgets.append(usbPipeLabel);
    networkUsbWidgets.append(usbPipeSpinBox);

    auto *usbTransferColumn = new QVBoxLayout();
    usbTransferColumn->setContentsMargins(0, 0, 0, 0);
    usbTransferColumn->setSpacing(8);
    auto *usbTransferLabel = new QLabel("Read Chunk Bytes", usbSettingsContainer);
    usbTransferLabel->setObjectName("ControlLabel");
    usbTransferSpinBox = new QSpinBox(usbSettingsContainer);
    usbTransferSpinBox->setRange(804, 1 << 20);
    usbTransferSpinBox->setSingleStep(804);
    usbTransferSpinBox->setValue(16384);
    usbTransferSpinBox->setMinimumHeight(46);
    usbTransferColumn->addWidget(usbTransferLabel);
    usbTransferColumn->addWidget(usbTransferSpinBox);
    usbRow->addLayout(usbTransferColumn, 1);
    networkUsbWidgets.append(usbTransferLabel);
    networkUsbWidgets.append(usbTransferSpinBox);

    usbLayout->addLayout(usbRow);

    auto *usbHint = new QLabel("FT601 mode expects a continuous byte stream that can be sliced into 804-byte logical packets: 4-byte sync header + 800-byte payload with the same AA / line / BB semantics as UDP.", usbSettingsContainer);
    usbHint->setObjectName("HintLabel");
    usbHint->setWordWrap(true);
    usbLayout->addWidget(usbHint);
    networkUsbWidgets.append(usbHint);

    auto *udpDemoDivider = new QFrame(udpSettingsContainer);
    udpDemoDivider->setFrameShape(QFrame::HLine);
    udpDemoDivider->setStyleSheet("color: #26262a; background: #26262a; min-height: 1px; max-height: 1px;");
    udpLayout->addWidget(udpDemoDivider);
    networkUdpWidgets.append(udpDemoDivider);

    auto *demoHeader = new QHBoxLayout();
    demoHeader->setContentsMargins(0, 0, 0, 0);
    demoHeader->setSpacing(12);

    auto *demoLabel = new QLabel("Local Demo", udpSettingsContainer);
    demoLabel->setObjectName("ControlLabel");
    demoHeader->addWidget(demoLabel);
    demoHeader->addStretch();

    demoModeCheckBox = new QCheckBox("Enable Built-in UDP Demo", udpSettingsContainer);
    connect(demoModeCheckBox, &QCheckBox::toggled, this, &ControlUI::onDemoModeChanged);
    demoHeader->addWidget(demoModeCheckBox, 0, Qt::AlignRight);
    udpLayout->addLayout(demoHeader);
    networkUdpWidgets.append(demoLabel);
    networkUdpWidgets.append(demoModeCheckBox);

    demoStatusLabel = new QLabel("Demo is disabled.", udpSettingsContainer);
    demoStatusLabel->setObjectName("StatusText");
    demoStatusLabel->setWordWrap(true);
    udpLayout->addWidget(demoStatusLabel);
    networkUdpWidgets.append(demoStatusLabel);

    auto *demoHint = new QLabel("Use this to inject protocol-compatible local UDP traffic without restarting the app.", udpSettingsContainer);
    demoHint->setObjectName("HintLabel");
    demoHint->setWordWrap(true);
    udpLayout->addWidget(demoHint);
    networkUdpWidgets.append(demoHint);

    auto *footerDivider = new QFrame(networkCard);
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setStyleSheet("color: #26262a; background: #26262a; min-height: 1px; max-height: 1px;");
    networkLayout->addWidget(footerDivider);

    applyReceiverButton = new QPushButton("Apply Input Settings", networkCard);
    applyReceiverButton->setObjectName("PrimaryButton");
    applyReceiverButton->setMinimumHeight(46);
    connect(applyReceiverButton, &QPushButton::clicked, this, &ControlUI::onApplyReceiverSettings);
    networkLayout->addWidget(applyReceiverButton);

    receiverStatusLabel = new QLabel("Receiver: waiting for input status", networkCard);
    receiverStatusLabel->setObjectName("StatusText");
    receiverStatusLabel->setWordWrap(true);
    networkLayout->addWidget(receiverStatusLabel);

    auto *networkHint = new QLabel("Applying settings clears pending receiver-side data and switches the active input path without restarting the whole application.", networkCard);
    networkHint->setObjectName("HintLabel");
    networkHint->setWordWrap(true);
    networkLayout->addWidget(networkHint);

    layout->addWidget(networkCard);
    layout->addStretch(1);
    onSourceModeChanged(sourceModeComboBox->currentIndex());
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

void ControlUI::onReceiverSettingsChanged(const QString &address,
                                          quint16 port,
                                          int mode,
                                          const QString &npcapInterface,
                                          const QString &usbDeviceMatch,
                                          int usbPipeId,
                                          int usbTransferBytes) {
    addressEdit->setText(address);
    portSpinBox->setValue(static_cast<int>(port));
    const int uiMode = (mode == 2) ? 2 : 0;
    const int comboIndex = sourceModeComboBox->findData(uiMode);
    if (comboIndex >= 0) {
        sourceModeComboBox->setCurrentIndex(comboIndex);
    }
    npcapModeCheckBox->setChecked(mode == 1);
    npcapInterfaceEdit->setText(npcapInterface);
    usbDeviceMatchEdit->setText(usbDeviceMatch);
    usbPipeSpinBox->setValue(usbPipeId);
    usbTransferSpinBox->setValue(usbTransferBytes);
    onSourceModeChanged(sourceModeComboBox->currentIndex());
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
    const int sourceMode = sourceModeComboBox->currentData().toInt();
    const bool usingFt601 = (sourceMode == 2);
    const bool usingNpcap = (!usingFt601 && npcapModeCheckBox->isChecked());
    const int mode = usingFt601 ? 2 : (usingNpcap ? 1 : 0);

    const QString address = addressEdit->text().trimmed();
    if (!usingFt601 && address.isEmpty()) {
        QMessageBox::warning(this, "Bind Address Required", "Please enter a bind address before applying UDP receiver settings.");
        return;
    }

    const QString npcapInterface = npcapInterfaceEdit->text().trimmed();
    if (usingNpcap && npcapInterface.isEmpty()) {
        QMessageBox::warning(this, "Npcap Interface Required", "Please enter the Npcap interface name when diagnostic capture is enabled.");
        return;
    }
    const QString usbDeviceMatch = usbDeviceMatchEdit->text().trimmed();
    if (usingFt601 && usbDeviceMatch.isEmpty()) {
        QMessageBox::warning(this, "FT601 Device Match Required", "Please enter the FT601 device match string before applying USB receiver settings.");
        return;
    }

    if (usingNpcap) {
        receiverStatusLabel->setText(QString("Receiver: applying Npcap capture on %1 | UDP dport=%2 ...").arg(npcapInterface).arg(portSpinBox->value()));
    } else if (usingFt601) {
        receiverStatusLabel->setText(QString("Receiver: applying FT601 USB mode | device=%1 | pipe=0x%2 ...")
                                         .arg(usbDeviceMatch)
                                         .arg(usbPipeSpinBox->value(), 2, 16, QLatin1Char('0')));
    } else {
        receiverStatusLabel->setText(QString("Receiver: applying %1:%2 ...").arg(address).arg(portSpinBox->value()));
    }
    emit receiverSettingsRequested(address,
                                   static_cast<quint16>(portSpinBox->value()),
                                   mode,
                                   npcapInterface,
                                   usbDeviceMatch,
                                   usbPipeSpinBox->value(),
                                   usbTransferSpinBox->value());
}

void ControlUI::onSourceModeChanged(int index) {
    Q_UNUSED(index);

    const bool usingFt601 = (sourceModeComboBox->currentData().toInt() == 2);
    const bool usingNpcap = (!usingFt601 && npcapModeCheckBox->isChecked());

    for (int i = 0; i < networkUdpWidgets.size(); ++i) {
        if (networkUdpWidgets[i] != nullptr) {
            networkUdpWidgets[i]->setVisible(!usingFt601);
        }
    }
    for (int i = 0; i < networkUsbWidgets.size(); ++i) {
        if (networkUsbWidgets[i] != nullptr) {
            networkUsbWidgets[i]->setVisible(usingFt601);
        }
    }

    if (usbSettingsContainer != nullptr) {
        usbSettingsContainer->setVisible(usingFt601);
    }

    npcapModeCheckBox->setEnabled(!usingFt601);
    npcapInterfaceEdit->setEnabled(usingNpcap);
    addressEdit->setEnabled(!usingFt601);
    usbDeviceMatchEdit->setEnabled(usingFt601);
    usbPipeSpinBox->setEnabled(usingFt601);
    usbTransferSpinBox->setEnabled(usingFt601);
    if (demoModeCheckBox != nullptr) {
        demoModeCheckBox->setEnabled(!usingFt601);
    }
    if (usingFt601 && demoStatusLabel != nullptr) {
        demoStatusLabel->setText("Demo traffic is only meaningful when UDP is the selected input source.");
    }
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
