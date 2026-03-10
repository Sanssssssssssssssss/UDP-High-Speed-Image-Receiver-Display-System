/*
===================================================
Created on: 13-11-2024
Author: Chang Xu
File: ControlUI.cpp
Version: 1.7
Language: C++ (Qt Framework)
Description:
This file implements the ControlUI class, which provides
a user interface for adjusting video processing parameters.
===================================================
*/

#include "ControlUI.h"
#include <QDebug>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>

ControlUI::ControlUI(QWidget *parent) : QWidget(parent), isRecording(false), recordingTimer(nullptr) {
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
            text-transform: uppercase;
        }
        QLabel#PanelTitle {
            color: #fafafa;
            font-size: 32px;
            font-weight: 700;
        }
        QLabel#PanelSubtitle {
            color: #b0b0b6;
            font-size: 14px;
            line-height: 1.35;
        }
        QLabel#CardTitle {
            color: #f4f4f5;
            font-size: 20px;
            font-weight: 650;
        }
        QLabel#MetricValue {
            color: #ffffff;
            font-size: 46px;
            font-weight: 700;
        }
        QLabel#MetricUnit {
            color: #8a8a92;
            font-size: 17px;
            font-weight: 600;
        }
        QLabel#PerfText,
        QLabel#DirLabel {
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
            min-height: 56px;
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
        QPushButton#SecondaryButton {
            background: #1d1d20;
            color: #f1f1f3;
        }
        QPushButton#SecondaryButton:hover {
            background: #232327;
        }
        QPushButton#RecordButton {
            background: #1d1d20;
            color: #f1f1f3;
        }
        QPushButton#RecordButton:hover {
            background: #26262b;
        }
        QPushButton#RecordButton[recording=\"true\"] {
            background: #d44d3f;
            border-color: #d44d3f;
            color: #ffffff;
        }
        QPushButton#RecordButton[recording=\"true\"]:hover {
            background: #e15848;
        }
        QComboBox {
            min-height: 52px;
            border-radius: 14px;
            border: 1px solid #303036;
            padding: 0 14px;
            color: #f1f1f4;
            background: #1a1a1d;
            font-size: 16px;
        }
        QComboBox::drop-down {
            border: none;
            width: 32px;
        }
        QComboBox QAbstractItemView {
            background: #161618;
            color: #f1f1f4;
            border: 1px solid #303036;
            selection-background-color: #2a2a30;
        }
    )");

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(20);
    layout->setContentsMargins(26, 26, 26, 26);
    layout->setAlignment(Qt::AlignTop);

    auto *eyebrowLabel = new QLabel("POST-TRAIN CONSOLE", this);
    eyebrowLabel->setObjectName("Eyebrow");
    layout->addWidget(eyebrowLabel);

    auto *titleLabel = new QLabel("Receiver Control Surface", this);
    titleLabel->setObjectName("PanelTitle");
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel("Live tuning, capture control and runtime health for the UDP image path.", this);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    auto *statusCard = new QFrame(this);
    statusCard->setObjectName("Card");
    auto *statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(24, 24, 24, 24);
    statusLayout->setSpacing(14);

    auto *statusHeader = new QLabel("Stream Status", statusCard);
    statusHeader->setObjectName("CardTitle");
    statusLayout->addWidget(statusHeader);

    auto *fpsRow = new QHBoxLayout();
    fpsRow->setContentsMargins(0, 0, 0, 0);
    fpsRow->setSpacing(8);
    fpsLabel = new QLabel("0", statusCard);
    fpsLabel->setObjectName("MetricValue");
    auto *fpsUnitLabel = new QLabel("FPS", statusCard);
    fpsUnitLabel->setObjectName("MetricUnit");
    fpsUnitLabel->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
    fpsRow->addWidget(fpsLabel);
    fpsRow->addWidget(fpsUnitLabel);
    fpsRow->addStretch();
    statusLayout->addLayout(fpsRow);

    performanceLabel = new QLabel("Perf: waiting for frames", statusCard);
    performanceLabel->setObjectName("PerfText");
    performanceLabel->setWordWrap(true);
    statusLayout->addWidget(performanceLabel);
    layout->addWidget(statusCard);

    auto *tuningCard = new QFrame(this);
    tuningCard->setObjectName("Card");
    auto *tuningLayout = new QVBoxLayout(tuningCard);
    tuningLayout->setContentsMargins(24, 24, 24, 24);
    tuningLayout->setSpacing(18);

    auto *tuningHeader = new QLabel("Image Tuning", tuningCard);
    tuningHeader->setObjectName("CardTitle");
    tuningLayout->addWidget(tuningHeader);

    auto addSliderControl = [this, tuningLayout](const QString &labelText, QLabel **valueLabelOut, QSlider **sliderOut, int minValue, int maxValue, int defaultValue, const char *slot) {
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

        tuningLayout->addWidget(rowContainer);
        *valueLabelOut = valueLabel;
        *sliderOut = slider;
    };

    addSliderControl("Brightness", &brightnessValueLabel, &brightnessSlider, 0, 100, 50, SLOT(onBrightnessChanged(int)));
    addSliderControl("Gamma", &gammaValueLabel, &gammaSlider, -100, 100, 0, SLOT(onGammaChanged(int)));
    addSliderControl("Sharpness", &sharpnessValueLabel, &sharpnessSlider, 0, 100, 0, SLOT(onSharpnessChanged(int)));
    addSliderControl("Denoise", &denoiseValueLabel, &denoiseSlider, 0, 100, 0, SLOT(onDenoiseChanged(int)));
    layout->addWidget(tuningCard);

    auto *geometryCard = new QFrame(this);
    geometryCard->setObjectName("Card");
    auto *geometryLayout = new QVBoxLayout(geometryCard);
    geometryLayout->setContentsMargins(24, 24, 24, 24);
    geometryLayout->setSpacing(14);

    auto *geometryHeader = new QLabel("Orientation", geometryCard);
    geometryHeader->setObjectName("CardTitle");
    geometryLayout->addWidget(geometryHeader);

    horizontalFlip = new QCheckBox("Mirror Horizontally", geometryCard);
    connect(horizontalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipHorizontalChanged);
    geometryLayout->addWidget(horizontalFlip);

    verticalFlip = new QCheckBox("Mirror Vertically", geometryCard);
    connect(verticalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipVerticalChanged);
    geometryLayout->addWidget(verticalFlip);

    auto *orientationHint = new QLabel("Applied locally to the displayed, captured and recorded frame.", geometryCard);
    orientationHint->setObjectName("HintLabel");
    orientationHint->setWordWrap(true);
    geometryLayout->addWidget(orientationHint);
    layout->addWidget(geometryCard);

    auto *captureCard = new QFrame(this);
    captureCard->setObjectName("Card");
    auto *captureLayout = new QVBoxLayout(captureCard);
    captureLayout->setContentsMargins(24, 24, 24, 24);
    captureLayout->setSpacing(16);

    auto *captureHeader = new QLabel("Capture Output", captureCard);
    captureHeader->setObjectName("CardTitle");
    captureLayout->addWidget(captureHeader);

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
    layout->addWidget(captureCard);

    layout->addStretch(1);
    setLayout(layout);
}

void ControlUI::onFPSChanged(int fps) {
    fpsLabel->setText(QString::number(fps));
}

void ControlUI::onPerformanceStatsChanged(const QString &statsText) {
    performanceLabel->setText(statsText);
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

    QString format = formatComboBox->currentText();
    if (format != "mp4" && format != "avi") {
        QMessageBox::warning(this, "Unsupported Format", QString("Format %1 is not supported.").arg(format));
        return;
    }

    emit recordingRequested(saveDirectory, format);
}

void ControlUI::onRecordingStateChanged(bool isRecording) {
    const bool wasRecording = this->isRecording;
    this->isRecording = isRecording;
    recordButton->setProperty("recording", isRecording);
    recordButton->style()->unpolish(recordButton);
    recordButton->style()->polish(recordButton);

    if (isRecording) {
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
        } else {
            qDebug() << "Recording stopped successfully.";
        }
    }
}

void ControlUI::onBrowseSaveDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Save Directory"), "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
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
