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
It allows users to modify brightness, gamma, sharpness,
and denoise settings, control image flipping, take snapshots,
and manage video recording functionality. The UI interacts
with a backend video processing system using Qt signals
and slots, making it a flexible and extensible tool for
UDP-based image processing.
===================================================
*/


#include "ControlUI.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>

ControlUI::ControlUI(QWidget *parent) : QWidget(parent), isRecording(false), recordingTimer(nullptr) {
    // Set up layout
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(25);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setAlignment(Qt::AlignTop);

    // FPS display
    fpsLabel = new QLabel("FPS: 0", this);
    layout->addWidget(fpsLabel);

    // Brightness slider
    QLabel *brightnessLabel = new QLabel("Brightness", this);
    brightnessValueLabel = new QLabel(QString::number(50), this);
    QHBoxLayout *brightnessLayout = new QHBoxLayout();
    brightnessLayout->addWidget(brightnessLabel);
    brightnessLayout->addWidget(brightnessValueLabel);
    layout->addLayout(brightnessLayout);

    brightnessSlider = new QSlider(Qt::Horizontal, this);
    brightnessSlider->setRange(0, 100);
    brightnessSlider->setValue(50);
    connect(brightnessSlider, &QSlider::valueChanged, this, &ControlUI::onBrightnessChanged);
    layout->addWidget(brightnessSlider);

    // Gamma slider
    QLabel *gammaLabel = new QLabel("Gamma", this);
    gammaValueLabel = new QLabel("0", this);
    QHBoxLayout *gammaLayout = new QHBoxLayout();
    gammaLayout->addWidget(gammaLabel);
    gammaLayout->addWidget(gammaValueLabel);
    layout->addLayout(gammaLayout);

    gammaSlider = new QSlider(Qt::Horizontal, this);
    gammaSlider->setRange(-100, 100);
    connect(gammaSlider, &QSlider::valueChanged, this, &ControlUI::onGammaChanged);
    layout->addWidget(gammaSlider);

    // Sharpness slider
    QLabel *sharpnessLabel = new QLabel("Sharpness", this);
    sharpnessValueLabel = new QLabel("0", this);
    QHBoxLayout *sharpnessLayout = new QHBoxLayout();
    sharpnessLayout->addWidget(sharpnessLabel);
    sharpnessLayout->addWidget(sharpnessValueLabel);
    layout->addLayout(sharpnessLayout);

    sharpnessSlider = new QSlider(Qt::Horizontal, this);
    sharpnessSlider->setRange(0, 100);
    connect(sharpnessSlider, &QSlider::valueChanged, this, &ControlUI::onSharpnessChanged);
    layout->addWidget(sharpnessSlider);

    // Denoise slider
    QLabel *denoiseLabel = new QLabel("Denoise", this);
    denoiseValueLabel = new QLabel("0", this);
    QHBoxLayout *denoiseLayout = new QHBoxLayout();
    denoiseLayout->addWidget(denoiseLabel);
    denoiseLayout->addWidget(denoiseValueLabel);
    layout->addLayout(denoiseLayout);

    denoiseSlider = new QSlider(Qt::Horizontal, this);
    denoiseSlider->setRange(0, 100);
    connect(denoiseSlider, &QSlider::valueChanged, this, &ControlUI::onDenoiseChanged);
    layout->addWidget(denoiseSlider);

    // Horizontal flip checkbox
    horizontalFlip = new QCheckBox("Horizontal Flip", this);
    connect(horizontalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipHorizontalChanged);
    layout->addWidget(horizontalFlip);

    // Vertical flip checkbox
    verticalFlip = new QCheckBox("Vertical Flip", this);
    connect(verticalFlip, &QCheckBox::toggled, this, &ControlUI::onFlipVerticalChanged);
    layout->addWidget(verticalFlip);

    // Snapshot button
    snapshotButton = new QPushButton("Take Snapshot", this);
    connect(snapshotButton, &QPushButton::clicked, this, &ControlUI::onTakeSnapshot);
    layout->addWidget(snapshotButton);

    // Recording button
    recordButton = new QPushButton("Start Recording", this);
    connect(recordButton, &QPushButton::clicked, this, &ControlUI::onRecordVideo);
    layout->addWidget(recordButton);

    // Browse save directory button
    browseButton = new QPushButton("Browse Save Directory", this);
    connect(browseButton, &QPushButton::clicked, this, &ControlUI::onBrowseSaveDirectory);
    layout->addWidget(browseButton);

    // Save directory label
    saveDirectoryLabel = new QLabel("Save Directory: Not Selected", this);
    layout->addWidget(saveDirectoryLabel);

    // Video format selection
    QLabel *formatLabel = new QLabel("Video Format", this);
    layout->addWidget(formatLabel);

    formatComboBox = new QComboBox(this);
    formatComboBox->addItem("mp4");
    formatComboBox->addItem("avi");
    layout->addWidget(formatComboBox);

    setLayout(layout);
}

void ControlUI::onFPSChanged(int fps) {
    // Update FPS display
    fpsLabel->setText(QString("FPS: %1").arg(fps));
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

    // Emit signal to toggle recording state
    emit recordingRequested(saveDirectory, format);
}

void ControlUI::onRecordingStateChanged(bool isRecording) {
    this->isRecording = isRecording;

    if (isRecording) {
        // Start recording
        recordButton->setText("Stop Recording");
        recordButton->setStyleSheet("background-color: red; color: white;");
        recordingElapsedTimer.start();

        if (!recordingTimer) {
            recordingTimer = new QTimer(this);
            connect(recordingTimer, &QTimer::timeout, this, [this]() {
                recordButton->setText(QString("Stop Recording (%1 s)").arg(recordingElapsedTimer.elapsed() / 1000));
            });
            recordingTimer->start(1000);
        }
    } else {
        // Stop recording
        recordButton->setText("Start Recording");
        recordButton->setStyleSheet("");

        // Stop timer
        if (recordingTimer) {
            recordingTimer->stop();
            delete recordingTimer;
            recordingTimer = nullptr;
        }

        // Show error message only when recording fails
        if (!this->isRecording) {
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
